#include "URayEffectsReconstruction.h"

#include "FRenderQuality.h"
#include "FRenderTarget.h"
#include "Shaders/RayEffectsReconstructionShaders.h"
#include "UHardwareGBuffer.h"

#include <GL/glew.h>

#include <algorithm>
#include <sstream>

namespace
{
constexpr int TextureUnits = 4;
constexpr int DrawBuffers = 8;

struct FState
{
    GLint drawFbo=0,readFbo=0,viewport[4]={},program=0,vao=0,active=GL_TEXTURE0;
    GLint textures[TextureUnits]={},samplers[TextureUnits]={},drawBuffers[DrawBuffers]={};
    GLint unpackAlignment=4,unpackRowLength=0,unpackImageHeight=0;
    GLint unpackSkipPixels=0,unpackSkipRows=0,unpackSkipImages=0;
    GLboolean depthMask=GL_TRUE,depth=GL_FALSE,cull=GL_FALSE,scissor=GL_FALSE,srgb=GL_FALSE;
    GLint depthFunction=GL_LESS,frontFace=GL_CCW,scissorBox[4]={},polygonMode[2]={GL_FILL,GL_FILL};
    GLdouble depthRange[2]={0.0,1.0};
    GLboolean rasterizerDiscard=GL_FALSE,stencil=GL_FALSE,sampleAlpha=GL_FALSE;
    GLboolean sampleCoverage=GL_FALSE,dither=GL_FALSE,primitiveRestart=GL_FALSE;
    GLboolean depthClamp=GL_FALSE,polygonOffset=GL_FALSE;
    GLboolean blend[DrawBuffers]={},colorMask[DrawBuffers][4]={};
    FState()
    {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFbo); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo);
        glGetIntegerv(GL_VIEWPORT,viewport); glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao); glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpackAlignment);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&unpackRowLength);glGetIntegerv(GL_UNPACK_IMAGE_HEIGHT,&unpackImageHeight);glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&unpackSkipPixels);glGetIntegerv(GL_UNPACK_SKIP_ROWS,&unpackSkipRows);glGetIntegerv(GL_UNPACK_SKIP_IMAGES,&unpackSkipImages);
        for(int i=0;i<DrawBuffers;++i){glGetIntegerv(GL_DRAW_BUFFER0+i,&drawBuffers[i]);blend[i]=glIsEnabledi(GL_BLEND,i);glGetBooleani_v(GL_COLOR_WRITEMASK,i,colorMask[i]);}
        for(int i=0;i<TextureUnits;++i){glActiveTexture(GL_TEXTURE0+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i]);glGetIntegeri_v(GL_SAMPLER_BINDING,i,&samplers[i]);}
        glActiveTexture(active);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);depth=glIsEnabled(GL_DEPTH_TEST);cull=glIsEnabled(GL_CULL_FACE);scissor=glIsEnabled(GL_SCISSOR_TEST);srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB);
        glGetIntegerv(GL_DEPTH_FUNC,&depthFunction);glGetIntegerv(GL_FRONT_FACE,&frontFace);glGetIntegerv(GL_SCISSOR_BOX,scissorBox);glGetIntegerv(GL_POLYGON_MODE,polygonMode);glGetDoublev(GL_DEPTH_RANGE,depthRange);
        rasterizerDiscard=glIsEnabled(GL_RASTERIZER_DISCARD);stencil=glIsEnabled(GL_STENCIL_TEST);sampleAlpha=glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);sampleCoverage=glIsEnabled(GL_SAMPLE_COVERAGE);dither=glIsEnabled(GL_DITHER);primitiveRestart=glIsEnabled(GL_PRIMITIVE_RESTART);depthClamp=glIsEnabled(GL_DEPTH_CLAMP);polygonOffset=glIsEnabled(GL_POLYGON_OFFSET_FILL);
    }
    ~FState()
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,drawFbo);glBindFramebuffer(GL_READ_FRAMEBUFFER,readFbo);
        if(drawFbo==0)glDrawBuffer(drawBuffers[0]);else{GLenum b[DrawBuffers];for(int i=0;i<DrawBuffers;++i)b[i]=drawBuffers[i];glDrawBuffers(DrawBuffers,b);}
        glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glUseProgram(program);glBindVertexArray(vao);
        for(int i=0;i<TextureUnits;++i){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,textures[i]);glBindSampler(i,samplers[i]);}
        glActiveTexture(active);if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);glDepthMask(depthMask);
        glPixelStorei(GL_UNPACK_ALIGNMENT,unpackAlignment);glPixelStorei(GL_UNPACK_ROW_LENGTH,unpackRowLength);glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,unpackImageHeight);glPixelStorei(GL_UNPACK_SKIP_PIXELS,unpackSkipPixels);glPixelStorei(GL_UNPACK_SKIP_ROWS,unpackSkipRows);glPixelStorei(GL_UNPACK_SKIP_IMAGES,unpackSkipImages);
        if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);if(srgb)glEnable(GL_FRAMEBUFFER_SRGB);else glDisable(GL_FRAMEBUFFER_SRGB);
        if(rasterizerDiscard)glEnable(GL_RASTERIZER_DISCARD);else glDisable(GL_RASTERIZER_DISCARD);if(stencil)glEnable(GL_STENCIL_TEST);else glDisable(GL_STENCIL_TEST);if(sampleAlpha)glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);if(sampleCoverage)glEnable(GL_SAMPLE_COVERAGE);else glDisable(GL_SAMPLE_COVERAGE);if(dither)glEnable(GL_DITHER);else glDisable(GL_DITHER);if(primitiveRestart)glEnable(GL_PRIMITIVE_RESTART);else glDisable(GL_PRIMITIVE_RESTART);if(depthClamp)glEnable(GL_DEPTH_CLAMP);else glDisable(GL_DEPTH_CLAMP);if(polygonOffset)glEnable(GL_POLYGON_OFFSET_FILL);else glDisable(GL_POLYGON_OFFSET_FILL);
        glDepthFunc(depthFunction);glDepthRange(depthRange[0],depthRange[1]);glScissor(scissorBox[0],scissorBox[1],scissorBox[2],scissorBox[3]);glPolygonMode(GL_FRONT,polygonMode[0]);glPolygonMode(GL_BACK,polygonMode[1]);glFrontFace(frontFace);
        for(int i=0;i<DrawBuffers;++i){if(blend[i])glEnablei(GL_BLEND,i);else glDisablei(GL_BLEND,i);glColorMaski(i,colorMask[i][0],colorMask[i][1],colorMask[i][2],colorMask[i][3]);}
    }
};

bool Compile(GLenum kind,const char* source,GLuint& shader,std::string& diagnostic)
{
    shader=glCreateShader(kind);glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);
    GLint okay=GL_FALSE;glGetShaderiv(shader,GL_COMPILE_STATUS,&okay);if(okay==GL_TRUE)return true;
    GLint size=0;glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&size);std::string log(std::max(size,1),'\0');GLsizei written=0;glGetShaderInfoLog(shader,size,&written,log.data());log.resize(written);diagnostic="Ray reconstruction shader compile failed: "+log;return false;
}
bool Link(const char* fragment,GLuint& program,std::string& diagnostic)
{
    GLuint vs=0,fs=0;if(!Compile(GL_VERTEX_SHADER,RayEffectsReconstructionShaders::FullscreenVertex,vs,diagnostic)||!Compile(GL_FRAGMENT_SHADER,fragment,fs,diagnostic)){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return false;}
    program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);glDeleteShader(vs);glDeleteShader(fs);
    GLint okay=GL_FALSE;glGetProgramiv(program,GL_LINK_STATUS,&okay);if(okay==GL_TRUE)return true;
    GLint size=0;glGetProgramiv(program,GL_INFO_LOG_LENGTH,&size);std::string log(std::max(size,1),'\0');GLsizei written=0;glGetProgramInfoLog(program,size,&written,log.data());log.resize(written);diagnostic="Ray reconstruction program link failed: "+log;return false;
}
bool Valid(const std::optional<FRenderOutputView>& view,int w,int h)
{return !view|| (view->valid&&view->identity&&view->width==w&&view->height==h);}
}

URayEffectsReconstruction::~URayEffectsReconstruction() noexcept { Shutdown(); }

bool URayEffectsReconstruction::EnsureResources(int width,int height,std::uint64_t generation,std::string* diagnostic)
{
    if(width<=0||height<=0){if(diagnostic)*diagnostic="Ray reconstruction rejected invalid dimensions";return false;}
    if(contextGeneration_&&contextGeneration_!=generation) ForgetCurrentResources();
    if(accumulateProgram_&&width_==width&&height_==height&&contextGeneration_==generation)return true;
    FState restore;
    GLuint newAccumulate=accumulateProgram_,newFilter=filterProgram_,newVAO=fullscreenVAO_;
    GLuint newFbo=0,newTextures[8]={};std::string message;bool newPrograms=!accumulateProgram_;
    if(newPrograms)
    {
        if(!Link(RayEffectsReconstructionShaders::AccumulateFragment,newAccumulate,message)||!Link(RayEffectsReconstructionShaders::EdgeAwareFragment,newFilter,message))goto fail;
        glGenVertexArrays(1,&newVAO);
    }
    glActiveTexture(GL_TEXTURE0);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_IMAGES,0);
    glGenTextures(8,newTextures);
    for(GLuint texture:newTextures){glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,width,height,0,GL_RGBA,GL_HALF_FLOAT,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
    glGenFramebuffers(1,&newFbo);glBindFramebuffer(GL_FRAMEBUFFER,newFbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,newTextures[0],0);glDrawBuffer(GL_COLOR_ATTACHMENT0);
    if(!newAccumulate||!newFilter||!newVAO||!newFbo||glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE||glGetError()!=GL_NO_ERROR){message="Ray reconstruction resource allocation failed";goto fail;}
    if(framebuffer_){glDeleteTextures(2,shadowHistory_);glDeleteTextures(2,giHistory_);glDeleteTextures(4,scratch_);glDeleteFramebuffers(1,&framebuffer_);stats_.releasedResources+=9;}
    if(newPrograms){accumulateProgram_=newAccumulate;filterProgram_=newFilter;fullscreenVAO_=newVAO;stats_.resourceAllocations+=3;}
    framebuffer_=newFbo;shadowHistory_[0]=newTextures[0];shadowHistory_[1]=newTextures[1];giHistory_[0]=newTextures[2];giHistory_[1]=newTextures[3];for(int i=0;i<4;++i)scratch_[i]=newTextures[4+i];
    width_=width;height_=height;contextGeneration_=generation;historyValid_=false;stats_.resourceAllocations+=9;stats_.ownedTextures=8;stats_.ownedFramebuffers=1;if(diagnostic)diagnostic->clear();return true;
fail:
    for(GLuint texture:newTextures)if(texture)glDeleteTextures(1,&texture);if(newFbo)glDeleteFramebuffers(1,&newFbo);
    if(newPrograms){if(newAccumulate)glDeleteProgram(newAccumulate);if(newFilter)glDeleteProgram(newFilter);if(newVAO)glDeleteVertexArrays(1,&newVAO);}
    if(diagnostic)*diagnostic=message.empty()?"Ray reconstruction initialization failed":message;return false;
}

bool URayEffectsReconstruction::Reconstruct(const UHardwareGBuffer& gbuffer,const FRayEffectOutputs& raw,const FTemporalFrame& frame,const FRenderQuality& quality,std::uint64_t generation,FRayEffectOutputs& out,std::string* diagnostic)
{
    out={};if(!gbuffer.IsComplete()||gbuffer.ContextGeneration()!=generation||!Valid(raw.shadowedDirectTarget,gbuffer.Width(),gbuffer.Height())||!Valid(raw.globalIlluminationTarget,gbuffer.Width(),gbuffer.Height())||!Valid(raw.opticalContributionTarget,gbuffer.Width(),gbuffer.Height())){if(diagnostic)*diagnostic="Ray reconstruction rejected invalid inputs";return false;}
    if(!raw.shadowedDirectTarget&&!raw.globalIlluminationTarget){out=raw;if(diagnostic)diagnostic->clear();return true;}
    if(!EnsureResources(gbuffer.Width(),gbuffer.Height(),generation,diagnostic))return false;
    FState restore;glBindFramebuffer(GL_FRAMEBUFFER,framebuffer_);glViewport(0,0,width_,height_);glDrawBuffer(GL_COLOR_ATTACHMENT0);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_RASTERIZER_DISCARD);glDisable(GL_STENCIL_TEST);glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);glDisable(GL_SAMPLE_COVERAGE);glDisable(GL_DITHER);glDisable(GL_PRIMITIVE_RESTART);glDisable(GL_DEPTH_CLAMP);glDisable(GL_POLYGON_OFFSET_FILL);glDepthRange(0.0,1.0);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);glFrontFace(GL_CCW);for(int i=0;i<DrawBuffers;++i){glDisablei(GL_BLEND,i);glColorMaski(i,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);}glBindVertexArray(fullscreenVAO_);
    const bool reset=frame.reset||!historyValid_;if(reset){const GLfloat zero[4]={0,0,0,0};for(GLuint texture:{shadowHistory_[0],shadowHistory_[1],giHistory_[0],giHistory_[1]}){glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);glClearBufferfv(GL_COLOR,0,zero);}}
    const unsigned write=TemporalHistorySlot(frame),read=1u-write;
    auto bindTexture=[](int unit,GLenum target,GLuint texture){glActiveTexture(GL_TEXTURE0+unit);glBindTexture(target,texture);glBindSampler(unit,0);};
    auto drawTo=[&](GLuint texture){glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);glDrawArrays(GL_TRIANGLES,0,3);++stats_.reconstructionPasses;};
    auto accumulate=[&](const FRenderOutputView& current,GLuint previous,GLuint target){glUseProgram(accumulateProgram_);bindTexture(0,GL_TEXTURE_2D,static_cast<GLuint>(current.identity));bindTexture(1,GL_TEXTURE_2D,previous);glUniform1i(glGetUniformLocation(accumulateProgram_,"uCurrent"),0);glUniform1i(glGetUniformLocation(accumulateProgram_,"uHistory"),1);glUniform1f(glGetUniformLocation(accumulateProgram_,"uCurrentWeight"),TemporalCurrentWeight(frame,quality.temporalFrames));glUniform1i(glGetUniformLocation(accumulateProgram_,"uReset"),reset?1:0);drawTo(target);};
    auto filter=[&](GLuint input,GLuint target,int step,int x,int y,bool atrous){glUseProgram(filterProgram_);bindTexture(0,GL_TEXTURE_2D,input);bindTexture(1,GL_TEXTURE_2D,gbuffer.DepthTexture());bindTexture(2,GL_TEXTURE_2D,gbuffer.Texture(EHardwareGBufferSemantic::GeometricNormal));bindTexture(3,GL_TEXTURE_2D,gbuffer.Texture(EHardwareGBufferSemantic::Identity));glUniform1i(glGetUniformLocation(filterProgram_,"uInput"),0);glUniform1i(glGetUniformLocation(filterProgram_,"uDepth"),1);glUniform1i(glGetUniformLocation(filterProgram_,"uGeometricNormal"),2);glUniform1i(glGetUniformLocation(filterProgram_,"uIdentity"),3);glUniform2i(glGetUniformLocation(filterProgram_,"uDirection"),x,y);glUniform1i(glGetUniformLocation(filterProgram_,"uStep"),step);glUniform1i(glGetUniformLocation(filterProgram_,"uAtrous"),atrous?1:0);drawTo(target);};
    if(raw.shadowedDirectTarget){accumulate(*raw.shadowedDirectTarget,shadowHistory_[read],shadowHistory_[write]);filter(shadowHistory_[write],scratch_[0],1,1,0,false);filter(scratch_[0],scratch_[1],2,0,1,false);out.shadowedDirectTarget=FRenderOutputView{scratch_[1],width_,height_,true};}
    if(raw.globalIlluminationTarget){accumulate(*raw.globalIlluminationTarget,giHistory_[read],giHistory_[write]);filter(giHistory_[write],scratch_[2],1,0,0,true);filter(scratch_[2],scratch_[3],2,0,0,true);filter(scratch_[3],scratch_[2],4,0,0,true);out.globalIlluminationTarget=FRenderOutputView{scratch_[2],width_,height_,true};}
    out.opticalContributionTarget=raw.opticalContributionTarget;historyValid_=true;
    const GLenum error=glGetError();if(error!=GL_NO_ERROR){Reset();if(diagnostic){std::ostringstream s;s<<"Ray reconstruction produced OpenGL error 0x"<<std::hex<<error;*diagnostic=s.str();}return false;}if(diagnostic)diagnostic->clear();return true;
}

void URayEffectsReconstruction::Reset() noexcept { historyValid_=false; }
void URayEffectsReconstruction::Shutdown() noexcept
{
    if(contextGeneration_&&contextGeneration_==ActiveRenderTargetContextGeneration())DeleteCurrentResources();else ForgetCurrentResources();
}
void URayEffectsReconstruction::DeleteCurrentResources() noexcept
{
    if(shadowHistory_[0]){glDeleteTextures(2,shadowHistory_);stats_.releasedResources+=2;}if(giHistory_[0]){glDeleteTextures(2,giHistory_);stats_.releasedResources+=2;}if(scratch_[0]){glDeleteTextures(4,scratch_);stats_.releasedResources+=4;}if(framebuffer_){glDeleteFramebuffers(1,&framebuffer_);++stats_.releasedResources;}if(fullscreenVAO_){glDeleteVertexArrays(1,&fullscreenVAO_);++stats_.releasedResources;}if(accumulateProgram_){glDeleteProgram(accumulateProgram_);++stats_.releasedResources;}if(filterProgram_){glDeleteProgram(filterProgram_);++stats_.releasedResources;}ForgetCurrentResources();
}
void URayEffectsReconstruction::ForgetCurrentResources() noexcept
{
    accumulateProgram_=filterProgram_=fullscreenVAO_=framebuffer_=0;shadowHistory_[0]=shadowHistory_[1]=giHistory_[0]=giHistory_[1]=0;for(auto& texture:scratch_)texture=0;width_=height_=0;contextGeneration_=0;historyValid_=false;stats_.ownedTextures=0;stats_.ownedFramebuffers=0;
}
