#include "UHybridPresentationPass.h"

#include "FRenderQuality.h"
#include "FRenderTarget.h"
#include "Shaders/HybridPresentationShaders.h"
#include "UHardwareGBuffer.h"

#include <GL/glew.h>

#include <algorithm>
#include <limits>
#include <sstream>

namespace
{
constexpr int TextureUnitCount = 6;
constexpr int DrawBufferCount = 8;

bool CompileStage(GLenum stage, const char* source, const char* label,
                  GLuint& shader, std::string& diagnostic)
{
    shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) return true;
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    GLsizei written = 0;
    glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), &written, log.data());
    log.resize(static_cast<std::size_t>(written));
    diagnostic = std::string("Hybrid presentation ") + label +
        " shader compile failed: " + log;
    return false;
}

bool LinkProgram(const char* fragmentSource, const char* label,
                 GLuint& program, std::string& diagnostic)
{
    GLuint vertex = 0;
    GLuint fragment = 0;
    if (!CompileStage(GL_VERTEX_SHADER, HybridPresentationShaders::FullscreenVertex,
                      "fullscreen vertex", vertex, diagnostic) ||
        !CompileStage(GL_FRAGMENT_SHADER, fragmentSource, label, fragment, diagnostic))
    {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        return false;
    }
    program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE) return true;
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    GLsizei written = 0;
    glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), &written, log.data());
    log.resize(static_cast<std::size_t>(written));
    diagnostic = std::string("Hybrid presentation ") + label +
        " program link failed: " + log;
    glDeleteProgram(program);
    program = 0;
    return false;
}

struct FFullscreenState
{
    GLint drawFramebuffer = 0;
    GLint readFramebuffer = 0;
    GLint viewport[4] = {};
    GLint program = 0;
    GLint vao = 0;
    GLint activeTexture = GL_TEXTURE0;
    GLint textures[TextureUnitCount] = {};
    GLint samplers[TextureUnitCount] = {};
    GLint drawBuffers[DrawBufferCount] = {};
    GLint depthFunction = GL_LESS;
    GLint frontFace = GL_CCW;
    GLint scissorBox[4] = {};
    GLint polygonMode[2] = {GL_FILL, GL_FILL};
    GLdouble depthRange[2] = {0.0, 1.0};
    GLboolean colorMasks[DrawBufferCount][4] = {};
    GLboolean blends[DrawBufferCount] = {};
    GLboolean depthMask = GL_TRUE;
    GLboolean depthTest = GL_FALSE;
    GLboolean cull = GL_FALSE;
    GLboolean scissor = GL_FALSE;
    GLboolean framebufferSRGB = GL_FALSE;
    GLboolean rasterizerDiscard = GL_FALSE;
    GLboolean stencil = GL_FALSE;
    GLboolean sampleAlphaToCoverage = GL_FALSE;
    GLboolean sampleCoverage = GL_FALSE;
    GLboolean dither = GL_FALSE;
    GLboolean primitiveRestart = GL_FALSE;
    GLboolean depthClamp = GL_FALSE;
    GLboolean polygonOffsetFill = GL_FALSE;
};

FFullscreenState CaptureState()
{
    FFullscreenState state;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &state.drawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &state.readFramebuffer);
    glGetIntegerv(GL_VIEWPORT, state.viewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state.vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &state.activeTexture);
    for (int i = 0; i < DrawBufferCount; ++i)
        glGetIntegerv(GL_DRAW_BUFFER0 + i, &state.drawBuffers[i]);
    for (int unit = 0; unit < TextureUnitCount; ++unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.textures[unit]);
        glGetIntegeri_v(GL_SAMPLER_BINDING, unit, &state.samplers[unit]);
    }
    glActiveTexture(static_cast<GLenum>(state.activeTexture));
    glGetIntegerv(GL_DEPTH_FUNC, &state.depthFunction);
    glGetIntegerv(GL_FRONT_FACE, &state.frontFace);
    glGetIntegerv(GL_SCISSOR_BOX, state.scissorBox);
    glGetIntegerv(GL_POLYGON_MODE, state.polygonMode);
    glGetDoublev(GL_DEPTH_RANGE, state.depthRange);
    for (int i = 0; i < DrawBufferCount; ++i)
    {
        glGetBooleani_v(GL_COLOR_WRITEMASK, i, state.colorMasks[i]);
        state.blends[i] = glIsEnabledi(GL_BLEND, i);
    }
    glGetBooleanv(GL_DEPTH_WRITEMASK, &state.depthMask);
    state.depthTest = glIsEnabled(GL_DEPTH_TEST);
    state.cull = glIsEnabled(GL_CULL_FACE);
    state.scissor = glIsEnabled(GL_SCISSOR_TEST);
    state.framebufferSRGB = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    state.rasterizerDiscard = glIsEnabled(GL_RASTERIZER_DISCARD);
    state.stencil = glIsEnabled(GL_STENCIL_TEST);
    state.sampleAlphaToCoverage = glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);
    state.sampleCoverage = glIsEnabled(GL_SAMPLE_COVERAGE);
    state.dither = glIsEnabled(GL_DITHER);
    state.primitiveRestart = glIsEnabled(GL_PRIMITIVE_RESTART);
    state.depthClamp = glIsEnabled(GL_DEPTH_CLAMP);
    state.polygonOffsetFill = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    return state;
}

void RestoreState(const FFullscreenState& state)
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(state.drawFramebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(state.readFramebuffer));
    if (state.drawFramebuffer == 0)
        glDrawBuffer(static_cast<GLenum>(state.drawBuffers[0]));
    else
    {
        GLenum buffers[DrawBufferCount] = {};
        for (int i = 0; i < DrawBufferCount; ++i)
            buffers[i] = static_cast<GLenum>(state.drawBuffers[i]);
        glDrawBuffers(DrawBufferCount, buffers);
    }
    glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
    glUseProgram(static_cast<GLuint>(state.program));
    glBindVertexArray(static_cast<GLuint>(state.vao));
    for (int unit = 0; unit < TextureUnitCount; ++unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(state.textures[unit]));
        glBindSampler(unit, static_cast<GLuint>(state.samplers[unit]));
    }
    glActiveTexture(static_cast<GLenum>(state.activeTexture));
    if (state.depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    for (int i = 0; i < DrawBufferCount; ++i)
    {
        if (state.blends[i]) glEnablei(GL_BLEND, i); else glDisablei(GL_BLEND, i);
        glColorMaski(i, state.colorMasks[i][0], state.colorMasks[i][1],
                    state.colorMasks[i][2], state.colorMasks[i][3]);
    }
    if (state.cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (state.scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (state.framebufferSRGB) glEnable(GL_FRAMEBUFFER_SRGB); else glDisable(GL_FRAMEBUFFER_SRGB);
    if (state.rasterizerDiscard) glEnable(GL_RASTERIZER_DISCARD); else glDisable(GL_RASTERIZER_DISCARD);
    if (state.stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    if (state.sampleAlphaToCoverage) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE); else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    if (state.sampleCoverage) glEnable(GL_SAMPLE_COVERAGE); else glDisable(GL_SAMPLE_COVERAGE);
    if (state.dither) glEnable(GL_DITHER); else glDisable(GL_DITHER);
    if (state.primitiveRestart) glEnable(GL_PRIMITIVE_RESTART); else glDisable(GL_PRIMITIVE_RESTART);
    if (state.depthClamp) glEnable(GL_DEPTH_CLAMP); else glDisable(GL_DEPTH_CLAMP);
    if (state.polygonOffsetFill) glEnable(GL_POLYGON_OFFSET_FILL); else glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthFunc(static_cast<GLenum>(state.depthFunction));
    glDepthMask(state.depthMask);
    glDepthRange(state.depthRange[0], state.depthRange[1]);
    glScissor(state.scissorBox[0], state.scissorBox[1], state.scissorBox[2], state.scissorBox[3]);
    glPolygonMode(GL_FRONT, static_cast<GLenum>(state.polygonMode[0]));
    glPolygonMode(GL_BACK, static_cast<GLenum>(state.polygonMode[1]));
    glFrontFace(static_cast<GLenum>(state.frontFace));
}

class FStateGuard
{
public:
    FStateGuard() : state_(CaptureState()) {}
    ~FStateGuard() { RestoreState(state_); }
private:
    FFullscreenState state_;
};

void ConfigureFullscreenState()
{
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    for (int i = 0; i < DrawBufferCount; ++i)
    {
        glDisablei(GL_BLEND, i);
        glColorMaski(i, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_RASTERIZER_DISCARD);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable(GL_SAMPLE_COVERAGE);
    glDisable(GL_DITHER);
    glDisable(GL_PRIMITIVE_RESTART);
    glDisable(GL_DEPTH_CLAMP);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthRange(0.0, 1.0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

bool CollectError(const char* operation, std::string* diagnostic)
{
    const GLenum error = glGetError();
    if (error == GL_NO_ERROR) return true;
    if (diagnostic)
    {
        std::ostringstream stream;
        stream << operation << " produced OpenGL error 0x" << std::hex << error;
        *diagnostic = stream.str();
    }
    return false;
}

bool MatchesSize(const FRenderOutputView& view, int width, int height)
{
    return view.valid && view.identity != 0 && view.width == width && view.height == height;
}
} // namespace

FInternalRenderSize ResolveInternalRenderSize(
    int outputWidth, int outputHeight, int ssaa, int maxTextureSize)
{
    if (outputWidth <= 0 || outputHeight <= 0 || (ssaa != 1 && ssaa != 2) ||
        maxTextureSize <= 0)
        return {};
    const std::int64_t width = static_cast<std::int64_t>(outputWidth) * ssaa;
    const std::int64_t height = static_cast<std::int64_t>(outputHeight) * ssaa;
    if (width > maxTextureSize || height > maxTextureSize ||
        width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max())
        return {};
    return {static_cast<int>(width), static_cast<int>(height)};
}

UHybridPresentationPass::~UHybridPresentationPass() noexcept
{
    Shutdown();
}

bool UHybridPresentationPass::Init(std::uint64_t contextGeneration,
                                   std::string* diagnostic)
{
    if (contextGeneration == 0 ||
        contextGeneration != ActiveRenderTargetContextGeneration())
    {
        if (diagnostic) *diagnostic = "Hybrid presentation context generation does not match the active context";
        return false;
    }
    if (IsReady() && contextGeneration_ == contextGeneration) return true;
    if (contextGeneration_ != 0 && contextGeneration_ != contextGeneration)
        ForgetCurrentResources();

    FStateGuard restore;
    std::string error;
    GLuint candidateComposite = 0;
    GLuint candidatePresentation = 0;
    if (!LinkProgram(HybridPresentationShaders::CompositeFragment, "HDR composite fragment",
                     candidateComposite, error) ||
        !LinkProgram(HybridPresentationShaders::PresentationFragment, "presentation fragment",
                     candidatePresentation, error))
    {
        if (candidateComposite) glDeleteProgram(candidateComposite);
        if (candidatePresentation) glDeleteProgram(candidatePresentation);
        if (diagnostic) *diagnostic = error;
        return false;
    }
    GLuint candidateVAO = 0;
    glGenVertexArrays(1, &candidateVAO);
    if (!candidateVAO || !CollectError("Hybrid presentation initialization", &error))
    {
        if (candidateVAO) glDeleteVertexArrays(1, &candidateVAO);
        glDeleteProgram(candidateComposite);
        glDeleteProgram(candidatePresentation);
        if (diagnostic) *diagnostic = error.empty()
            ? "Hybrid presentation failed to create fullscreen VAO" : error;
        return false;
    }
    compositeProgram_ = candidateComposite;
    presentationProgram_ = candidatePresentation;
    fullscreenVAO_ = candidateVAO;
    resourceAllocations_ += 3u;
    contextGeneration_ = contextGeneration;
    glUseProgram(compositeProgram_);
    const char* compositeSamplers[] = {"uPositionCoverage", "uEnvironmentAmbient",
        "uSelectedDirect", "uGIRadiance", "uOpticalContribution", "uEmissive"};
    for (int unit = 0; unit < TextureUnitCount; ++unit)
        glUniform1i(glGetUniformLocation(compositeProgram_, compositeSamplers[unit]), unit);
    glUseProgram(presentationProgram_);
    glUniform1i(glGetUniformLocation(presentationProgram_, "uHDRInput"), 0);
    if (!CollectError("Hybrid presentation uniforms", diagnostic))
    {
        DeleteCurrentResources();
        return false;
    }
    if (diagnostic) diagnostic->clear();
    return true;
}

bool UHybridPresentationPass::Resize(int internalWidth, int internalHeight,
                                     std::uint64_t contextGeneration,
                                     std::string* diagnostic)
{
    if (internalWidth <= 0 || internalHeight <= 0 ||
        !Init(contextGeneration, diagnostic))
        return false;
    if (framebuffer_ && width_ == internalWidth && height_ == internalHeight &&
        contextGeneration_ == contextGeneration)
        return true;

    FStateGuard restore;
    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    if (internalWidth > maxTextureSize || internalHeight > maxTextureSize)
    {
        if (diagnostic) *diagnostic = "Hybrid HDR output exceeds GL_MAX_TEXTURE_SIZE";
        return false;
    }
    GLuint candidateFramebuffer = 0;
    GLuint candidateTexture = 0;
    glGenFramebuffers(1, &candidateFramebuffer);
    glGenTextures(1, &candidateTexture);
    resourceAllocations_ += (candidateFramebuffer ? 1u : 0u) +
        (candidateTexture ? 1u : 0u);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, candidateTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, internalWidth, internalHeight,
                 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, candidateFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, candidateTexture, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    const bool complete = candidateFramebuffer && candidateTexture &&
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!complete || !CollectError("Hybrid HDR resize", diagnostic))
    {
        if (candidateTexture) { glDeleteTextures(1, &candidateTexture); ++releasedResources_; }
        if (candidateFramebuffer) { glDeleteFramebuffers(1, &candidateFramebuffer); ++releasedResources_; }
        if (!complete && diagnostic) *diagnostic = "Hybrid HDR framebuffer is incomplete";
        return false;
    }
    if (hdrTexture_) { glDeleteTextures(1, &hdrTexture_); ++releasedResources_; }
    if (framebuffer_) { glDeleteFramebuffers(1, &framebuffer_); ++releasedResources_; }
    hdrTexture_ = candidateTexture;
    framebuffer_ = candidateFramebuffer;
    width_ = internalWidth;
    height_ = internalHeight;
    ++resourceRevision_;
    if (diagnostic) diagnostic->clear();
    return true;
}

bool UHybridPresentationPass::CompositeHDR(
    const UHardwareGBuffer& gbuffer,
    const FRasterLightingOutput& raster,
    const FRayEffectOutputs& rayEffects,
    const FRenderQuality& quality,
    FRenderOutputView& hdrOutput,
    std::string* diagnostic)
{
    hdrOutput = {};
    if (!IsReady() || !framebuffer_ || !gbuffer.IsComplete() ||
        gbuffer.ContextGeneration() != contextGeneration_ ||
        gbuffer.Width() != width_ || gbuffer.Height() != height_ || !raster.valid ||
        !MatchesSize(raster.environmentAmbientTarget, width_, height_) ||
        !MatchesSize(raster.unshadowedDirectTarget, width_, height_) ||
        !MatchesSize(raster.emissiveTarget, width_, height_))
    {
        if (diagnostic) *diagnostic = "Hybrid HDR composite rejected invalid inputs/context";
        return false;
    }
    auto validEffect = [&](const std::optional<FRenderOutputView>& view)
    {
        return view.has_value() && MatchesSize(*view, width_, height_);
    };
    const bool hasShadowedDirect = validEffect(rayEffects.shadowedDirectTarget);
    const bool hasGI = validEffect(rayEffects.globalIlluminationTarget);
    const bool hasOptical = validEffect(rayEffects.opticalContributionTarget);
    const GLuint textures[TextureUnitCount] = {
        gbuffer.Texture(EHardwareGBufferSemantic::PositionCoverage),
        static_cast<GLuint>(raster.environmentAmbientTarget.identity),
        hasShadowedDirect ? static_cast<GLuint>(rayEffects.shadowedDirectTarget->identity)
                          : static_cast<GLuint>(raster.unshadowedDirectTarget.identity),
        hasGI ? static_cast<GLuint>(rayEffects.globalIlluminationTarget->identity) : 0u,
        hasOptical ? static_cast<GLuint>(rayEffects.opticalContributionTarget->identity) : 0u,
        static_cast<GLuint>(raster.emissiveTarget.identity),
    };
    FStateGuard restore;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, width_, height_);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    ConfigureFullscreenState();
    glUseProgram(compositeProgram_);
    glBindVertexArray(fullscreenVAO_);
    for (int unit = 0; unit < TextureUnitCount; ++unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, textures[unit]);
        glBindSampler(unit, 0);
    }
    glUniform1i(glGetUniformLocation(compositeProgram_, "uHasGIRadiance"), hasGI ? 1 : 0);
    glUniform1i(glGetUniformLocation(compositeProgram_, "uHasOpticalContribution"), hasOptical ? 1 : 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (!CollectError("Hybrid HDR composite", diagnostic)) return false;
    (void)quality;
    hdrOutput = {static_cast<std::uint64_t>(hdrTexture_), width_, height_, true};
    if (diagnostic) diagnostic->clear();
    return true;
}

bool UHybridPresentationPass::Present(const FRenderOutputView& hdrInput,
                                      FRenderTarget& outputTarget,
                                      const FRenderQuality& quality,
                                      std::string* diagnostic)
{
    if (!IsReady() || contextGeneration_ != outputTarget.ContextGeneration() ||
        !outputTarget.IsValidForContext(contextGeneration_) ||
        !MatchesSize(hdrInput, width_, height_))
    {
        if (diagnostic) *diagnostic = "Hybrid presentation rejected invalid input/target/context";
        return false;
    }
    FStateGuard restore;
    glViewport(0, 0, outputTarget.Width(), outputTarget.Height());
    glDrawBuffer(outputTarget.Kind() == ERenderTargetKind::DefaultFramebuffer
        ? GL_BACK : GL_COLOR_ATTACHMENT0);
    ConfigureFullscreenState();
    glUseProgram(presentationProgram_);
    glBindVertexArray(fullscreenVAO_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(hdrInput.identity));
    glBindSampler(0, 0);
    glUniform1f(glGetUniformLocation(presentationProgram_, "uExposureEV"), quality.exposureEV);
    glUniform1i(glGetUniformLocation(presentationProgram_, "uDepthView"), quality.depthView ? 1 : 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (!CollectError("Hybrid presentation", diagnostic)) return false;
    if (diagnostic) diagnostic->clear();
    return true;
}

void UHybridPresentationPass::Shutdown() noexcept
{
    if (contextGeneration_ != 0 &&
        contextGeneration_ == ActiveRenderTargetContextGeneration())
        DeleteCurrentResources();
    else
        ForgetCurrentResources();
}

void UHybridPresentationPass::DeleteCurrentResources() noexcept
{
    if (hdrTexture_) { glDeleteTextures(1, &hdrTexture_); ++releasedResources_; }
    if (framebuffer_) { glDeleteFramebuffers(1, &framebuffer_); ++releasedResources_; }
    if (fullscreenVAO_) { glDeleteVertexArrays(1, &fullscreenVAO_); ++releasedResources_; }
    if (compositeProgram_) { glDeleteProgram(compositeProgram_); ++releasedResources_; }
    if (presentationProgram_) { glDeleteProgram(presentationProgram_); ++releasedResources_; }
    ForgetCurrentResources();
}

std::uint64_t UHybridPresentationPass::ResourceIdentity() const
{
    std::uint64_t value = 1469598103934665603ull;
    for (unsigned name : {compositeProgram_, presentationProgram_, fullscreenVAO_,
                          framebuffer_, hdrTexture_})
        value = (value ^ name) * 1099511628211ull;
    return value;
}

void UHybridPresentationPass::ForgetCurrentResources() noexcept
{
    compositeProgram_ = 0;
    presentationProgram_ = 0;
    fullscreenVAO_ = 0;
    framebuffer_ = 0;
    hdrTexture_ = 0;
    width_ = height_ = 0;
    contextGeneration_ = 0;
}
