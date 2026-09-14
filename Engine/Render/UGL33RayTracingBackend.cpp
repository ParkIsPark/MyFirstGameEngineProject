#include "UGL33RayTracingBackend.h"

#include "FRenderScene.h"
#include "FRenderTarget.h"
#include "Shaders/RayEffectsFragmentShaders.h"
#include "UHardwareGBuffer.h"
#include "UGL43RayTracingBackend.h"

#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <utility>
#include <vector>

namespace
{
constexpr unsigned ShadowBit = 1u;
constexpr unsigned GIBit = 2u;
constexpr unsigned ReflectionBit = 4u;
constexpr int UsedTextureUnits = 16;
constexpr int CapturedDrawBuffers = 8;
constexpr int MaxShaderLights = 16;
constexpr std::size_t ExactFloatIntegerLimit = 16777216u;
// GL_TEXTURE_BUFFER_BINDING is the generic target binding query in GL 3.1+;
// older bundled GLEW headers do not expose its name.
constexpr GLenum TextureBufferBindingQuery = 0x8C2A;

bool CompileShader(GLenum stage, const char* source, GLuint& result,
                   std::string& diagnostic)
{
    result = glCreateShader(stage);
    glShaderSource(result, 1, &source, nullptr);
    glCompileShader(result);
    GLint compiled = GL_FALSE;
    glGetShaderiv(result, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) return true;
    GLint length = 0;
    glGetShaderiv(result, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    GLsizei written = 0;
    glGetShaderInfoLog(result, static_cast<GLsizei>(log.size()), &written, log.data());
    log.resize(static_cast<std::size_t>(written));
    diagnostic = "GL33 ray-effects shader compile failed: " + log;
    return false;
}

bool BuildProgram(GLuint& program, std::string& diagnostic)
{
    GLuint vertex = 0, fragment = 0;
    if (!CompileShader(GL_VERTEX_SHADER, RayEffectsFragmentShaders::FullscreenVertex,
                       vertex, diagnostic) ||
        !CompileShader(GL_FRAGMENT_SHADER, RayEffectsFragmentShaders::EffectsFragment,
                       fragment, diagnostic))
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
    diagnostic = "GL33 ray-effects program link failed: " + log;
    glDeleteProgram(program);
    program = 0;
    return false;
}

struct FState
{
    GLint drawFramebuffer = 0, readFramebuffer = 0, viewport[4] = {};
    GLint program = 0, vao = 0, activeTexture = GL_TEXTURE0;
    GLint packAlignment = 4, unpackAlignment = 4;
    GLint textures2D[UsedTextureUnits] = {};
    GLint textures2DArray[UsedTextureUnits] = {};
    GLint texturesBuffer[UsedTextureUnits] = {};
    GLint textureBufferBinding = 0;
    GLint samplers[UsedTextureUnits] = {};
    GLint drawBuffers[CapturedDrawBuffers] = {};
    GLint depthFunction = GL_LESS, frontFace = GL_CCW;
    GLint scissorBox[4] = {}, polygonMode[2] = {GL_FILL, GL_FILL};
    GLdouble depthRange[2] = {0.0, 1.0};
    GLboolean colorMasks[CapturedDrawBuffers][4] = {};
    GLboolean indexedBlend[CapturedDrawBuffers] = {};
    GLboolean depth = GL_FALSE, blend = GL_FALSE, cull = GL_FALSE;
    GLboolean scissor = GL_FALSE, srgb = GL_FALSE, rasterDiscard = GL_FALSE;
    GLboolean stencil = GL_FALSE, sampleAlpha = GL_FALSE, sampleCoverage = GL_FALSE;
    GLboolean dither = GL_FALSE, primitiveRestart = GL_FALSE, depthClamp = GL_FALSE;
    GLboolean polygonOffset = GL_FALSE;
    GLboolean depthMask = GL_TRUE;
};

FState CaptureState()
{
    FState state;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &state.drawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &state.readFramebuffer);
    glGetIntegerv(GL_VIEWPORT, state.viewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state.vao);
    glGetIntegerv(GL_PACK_ALIGNMENT, &state.packAlignment);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &state.unpackAlignment);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &state.activeTexture);
    glGetIntegerv(TextureBufferBindingQuery, &state.textureBufferBinding);
    for (int unit = 0; unit < UsedTextureUnits; ++unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.textures2D[unit]);
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &state.textures2DArray[unit]);
        glGetIntegerv(GL_TEXTURE_BINDING_BUFFER, &state.texturesBuffer[unit]);
        glGetIntegeri_v(GL_SAMPLER_BINDING, unit, &state.samplers[unit]);
    }
    glActiveTexture(static_cast<GLenum>(state.activeTexture));
    for (int i = 0; i < CapturedDrawBuffers; ++i)
    {
        glGetIntegerv(GL_DRAW_BUFFER0 + i, &state.drawBuffers[i]);
        glGetBooleani_v(GL_COLOR_WRITEMASK, i, state.colorMasks[i]);
        state.indexedBlend[i] = glIsEnabledi(GL_BLEND, i);
    }
    glGetIntegerv(GL_DEPTH_FUNC, &state.depthFunction);
    glGetIntegerv(GL_FRONT_FACE, &state.frontFace);
    glGetIntegerv(GL_SCISSOR_BOX, state.scissorBox);
    glGetIntegerv(GL_POLYGON_MODE, state.polygonMode);
    glGetDoublev(GL_DEPTH_RANGE, state.depthRange);
    state.depth = glIsEnabled(GL_DEPTH_TEST);
    state.blend = glIsEnabled(GL_BLEND);
    state.cull = glIsEnabled(GL_CULL_FACE);
    state.scissor = glIsEnabled(GL_SCISSOR_TEST);
    state.srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    state.rasterDiscard = glIsEnabled(GL_RASTERIZER_DISCARD);
    state.stencil = glIsEnabled(GL_STENCIL_TEST);
    state.sampleAlpha = glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);
    state.sampleCoverage = glIsEnabled(GL_SAMPLE_COVERAGE);
    state.dither = glIsEnabled(GL_DITHER);
    state.primitiveRestart = glIsEnabled(GL_PRIMITIVE_RESTART);
    state.depthClamp = glIsEnabled(GL_DEPTH_CLAMP);
    state.polygonOffset = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &state.depthMask);
    return state;
}

void RestoreState(const FState& state)
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(state.drawFramebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(state.readFramebuffer));
    if (state.drawFramebuffer == 0)
        glDrawBuffer(static_cast<GLenum>(state.drawBuffers[0]));
    else
    {
        GLenum buffers[CapturedDrawBuffers];
        for (int i = 0; i < CapturedDrawBuffers; ++i)
            buffers[i] = static_cast<GLenum>(state.drawBuffers[i]);
        glDrawBuffers(CapturedDrawBuffers, buffers);
    }
    glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
    glUseProgram(static_cast<GLuint>(state.program));
    glBindVertexArray(static_cast<GLuint>(state.vao));
    for (int unit = 0; unit < UsedTextureUnits; ++unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(state.textures2D[unit]));
        glBindTexture(GL_TEXTURE_2D_ARRAY, static_cast<GLuint>(state.textures2DArray[unit]));
        glBindTexture(GL_TEXTURE_BUFFER, static_cast<GLuint>(state.texturesBuffer[unit]));
        glBindSampler(unit, static_cast<GLuint>(state.samplers[unit]));
    }
    glActiveTexture(static_cast<GLenum>(state.activeTexture));
    glBindBuffer(GL_TEXTURE_BUFFER, static_cast<GLuint>(state.textureBufferBinding));
    glPixelStorei(GL_PACK_ALIGNMENT, state.packAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, state.unpackAlignment);
    if (state.depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (state.blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    for (int i = 0; i < CapturedDrawBuffers; ++i)
    {
        if (state.indexedBlend[i]) glEnablei(GL_BLEND, i); else glDisablei(GL_BLEND, i);
        glColorMaski(i, state.colorMasks[i][0], state.colorMasks[i][1],
                     state.colorMasks[i][2], state.colorMasks[i][3]);
    }
    if (state.cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (state.scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (state.srgb) glEnable(GL_FRAMEBUFFER_SRGB); else glDisable(GL_FRAMEBUFFER_SRGB);
    if (state.rasterDiscard) glEnable(GL_RASTERIZER_DISCARD); else glDisable(GL_RASTERIZER_DISCARD);
    if (state.stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    if (state.sampleAlpha) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE); else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    if (state.sampleCoverage) glEnable(GL_SAMPLE_COVERAGE); else glDisable(GL_SAMPLE_COVERAGE);
    if (state.dither) glEnable(GL_DITHER); else glDisable(GL_DITHER);
    if (state.primitiveRestart) glEnable(GL_PRIMITIVE_RESTART); else glDisable(GL_PRIMITIVE_RESTART);
    if (state.depthClamp) glEnable(GL_DEPTH_CLAMP); else glDisable(GL_DEPTH_CLAMP);
    if (state.polygonOffset) glEnable(GL_POLYGON_OFFSET_FILL); else glDisable(GL_POLYGON_OFFSET_FILL);
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
    FState state_;
};

bool CollectError(const char* operation, std::string* diagnostic)
{
    const GLenum error = glGetError();
    if (error == GL_NO_ERROR) return true;
    if (diagnostic)
    {
        std::ostringstream message;
        message << operation << " produced OpenGL error 0x" << std::hex << error;
        *diagnostic = message.str();
    }
    return false;
}

template <typename T>
bool UploadTBO(GLenum internalFormat, const std::vector<T>& values,
               GLuint& buffer, GLuint& texture)
{
    const T zero{};
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_TEXTURE_BUFFER, buffer);
    glBufferData(GL_TEXTURE_BUFFER,
        static_cast<GLsizeiptr>(values.empty() ? sizeof(T) : values.size() * sizeof(T)),
        values.empty() ? &zero : values.data(), GL_STATIC_DRAW);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_BUFFER, texture);
    glTexBuffer(GL_TEXTURE_BUFFER, internalFormat, buffer);
    return buffer != 0 && texture != 0;
}

void DeletePair(GLuint& buffer, GLuint& texture)
{
    if (texture) glDeleteTextures(1, &texture);
    if (buffer) glDeleteBuffers(1, &buffer);
    buffer = texture = 0;
}
} // namespace

UGL33RayTracingBackend::~UGL33RayTracingBackend() noexcept
{
    Shutdown();
}

bool UGL33RayTracingBackend::Init(std::uint64_t contextGeneration,
                                  std::string* diagnostic)
{
    if (contextGeneration == 0 ||
        contextGeneration != ActiveRenderTargetContextGeneration())
    {
        if (diagnostic) *diagnostic = "GL33 ray effects require the active render context";
        return false;
    }
    if (program_ && contextGeneration_ == contextGeneration) return true;
    if (contextGeneration_ && contextGeneration_ != contextGeneration)
        ForgetCurrentResources();
    FStateGuard restore;
    glActiveTexture(GL_TEXTURE0);
    GLint textureUnits = 0, uniformComponents = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &textureUnits);
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, &uniformComponents);
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maxTextureBufferTexels_);
    if (textureUnits < UsedTextureUnits || uniformComponents < 192 ||
        maxTextureBufferTexels_ <= 0)
    {
        if (diagnostic)
            *diagnostic = "GL33 ray effects need 16 fragment texture units, 192 uniforms, and texture buffers";
        return false;
    }
    maxLights_ = MaxShaderLights;
    std::string error;
    GLuint candidateProgram = 0, candidateVAO = 0;
    if (!BuildProgram(candidateProgram, error))
    {
        if (diagnostic) *diagnostic = error;
        return false;
    }
    glGenVertexArrays(1, &candidateVAO);
    if (!candidateVAO || !CollectError("GL33 ray-effects initialization", &error))
    {
        if (candidateVAO) glDeleteVertexArrays(1, &candidateVAO);
        glDeleteProgram(candidateProgram);
        if (diagnostic) *diagnostic = error.empty()
            ? "GL33 ray effects failed to create fullscreen VAO" : error;
        return false;
    }
    program_ = candidateProgram;
    fullscreenVAO_ = candidateVAO;
    contextGeneration_ = contextGeneration;
    stats_.resourceAllocations += 2;
    glUseProgram(program_);
    const char* names[] = {"uPositionCoverage", "uGeometricNormal",
        "uShadingNormalModel", "uAlbedoShininess", "uSpecularMirror",
        "uIdentity", "uSky", "uTriangles", "uBLASNodes", "uBLASIndices",
        "uInstances", "uTLASNodes", "uTLASIndices", "uInstanceIdentities",
        "uMaterials", "uMaterialAtlas"};
    for (int unit = 0; unit < UsedTextureUnits; ++unit)
        glUniform1i(glGetUniformLocation(program_, names[unit]), unit);
    if (diagnostic) diagnostic->clear();
    return CollectError("GL33 ray-effects sampler setup", diagnostic);
}

std::size_t UGL33RayTracingBackend::OwnedOutputTextureCount() const
{
    return (shadowTexture_ ? 1u : 0u) + (giTexture_ ? 1u : 0u) +
        (reflectionTexture_ ? 1u : 0u);
}

bool UGL33RayTracingBackend::ResizeOutputs(int width, int height, unsigned mask,
                                           std::uint64_t contextGeneration,
                                           std::string* diagnostic)
{
    if (framebuffer_ && width_ == width && height_ == height &&
        outputMask_ == mask && contextGeneration_ == contextGeneration) return true;
    GLint maxTextureSize = 0, maxAttachments = 0, maxDrawBuffers = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxAttachments);
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
    if (width <= 0 || height <= 0 || width > maxTextureSize || height > maxTextureSize ||
        maxAttachments < 3 || maxDrawBuffers < 3)
    {
        if (diagnostic) *diagnostic = "GL33 ray-effects output exceeds framebuffer/texture limits";
        return false;
    }
    GLuint candidateFBO = 0, candidateShadow = 0, candidateGI = 0, candidateReflection = 0;
    glGenFramebuffers(1, &candidateFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, candidateFBO);
    auto makeTexture = [&](GLuint& texture, GLenum internalFormat, GLenum format,
                           GLenum attachment)
    {
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
                     format, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, texture, 0);
    };
    if (mask & ShadowBit) makeTexture(candidateShadow, GL_R16F, GL_RED, GL_COLOR_ATTACHMENT0);
    if (mask & GIBit) makeTexture(candidateGI, GL_RGBA16F, GL_RGBA, GL_COLOR_ATTACHMENT1);
    if (mask & ReflectionBit) makeTexture(candidateReflection, GL_RGBA16F, GL_RGBA, GL_COLOR_ATTACHMENT2);
    const GLenum drawBuffers[3] = {
        static_cast<GLenum>(mask & ShadowBit ? GL_COLOR_ATTACHMENT0 : GL_NONE),
        static_cast<GLenum>(mask & GIBit ? GL_COLOR_ATTACHMENT1 : GL_NONE),
        static_cast<GLenum>(mask & ReflectionBit ? GL_COLOR_ATTACHMENT2 : GL_NONE),
    };
    glDrawBuffers(3, drawBuffers);
    const bool complete = candidateFBO && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
        CollectError("GL33 ray-effects output creation", diagnostic);
    if (!complete)
    {
        if (candidateShadow) glDeleteTextures(1, &candidateShadow);
        if (candidateGI) glDeleteTextures(1, &candidateGI);
        if (candidateReflection) glDeleteTextures(1, &candidateReflection);
        if (candidateFBO) glDeleteFramebuffers(1, &candidateFBO);
        if (diagnostic && diagnostic->empty()) *diagnostic = "GL33 ray-effects framebuffer is incomplete";
        return false;
    }
    if (shadowTexture_) glDeleteTextures(1, &shadowTexture_);
    if (giTexture_) glDeleteTextures(1, &giTexture_);
    if (reflectionTexture_) glDeleteTextures(1, &reflectionTexture_);
    if (framebuffer_) glDeleteFramebuffers(1, &framebuffer_);
    framebuffer_ = candidateFBO;
    shadowTexture_ = candidateShadow;
    giTexture_ = candidateGI;
    reflectionTexture_ = candidateReflection;
    width_ = width;
    height_ = height;
    outputMask_ = mask;
    ++stats_.outputAllocations;
    stats_.resourceAllocations += 1 + OwnedOutputTextureCount();
    stats_.ownedOutputTextures = OwnedOutputTextureCount();
    return true;
}

bool UGL33RayTracingBackend::UploadScene(const FPackedRayScene& packed,
                                         std::string* diagnostic)
{
    if (packed.maximumBLASDepth > 60 || packed.tlasDepth > 28)
    {
        if (diagnostic)
            *diagnostic = "Ray scene BVH exceeds bounded GL3.3 traversal stacks; split the mesh or scene";
        return false;
    }
    const bool uploadBLAS = packed.blasRevision != uploadedBLASRevision_;
    const bool uploadInstances = packed.instanceRevision != uploadedInstanceRevision_;
    const bool uploadMaterials = packed.materialRevision != uploadedMaterialRevision_;
    auto fits = [&](std::size_t texels) {
        return texels <= static_cast<std::size_t>(maxTextureBufferTexels_);
    };
    if (!fits(packed.triangleTexels.size()) || !fits(packed.blasNodeTexels.size()) ||
        !fits(packed.blasTriangleIndices.size()) || !fits(packed.instanceTexels.size()) ||
        !fits(packed.instanceIdentityTexels.size()) ||
        !fits(packed.tlasNodeTexels.size()) || !fits(packed.tlasInstanceIndices.size()) ||
        !fits(packed.materialTexels.size()))
    {
        if (diagnostic)
            *diagnostic = "Ray scene exceeds GL_MAX_TEXTURE_BUFFER_SIZE; reduce mesh/instance count";
        return false;
    }
    if (packed.triangleCount < 0 || packed.instanceCount < 0 ||
        packed.triangleTexels.size() > ExactFloatIntegerLimit * 7u ||
        packed.blasNodeTexels.size() > ExactFloatIntegerLimit * 2u ||
        packed.blasTriangleIndices.size() > ExactFloatIntegerLimit ||
        packed.instanceTexels.size() > ExactFloatIntegerLimit * 7u ||
        packed.tlasNodeTexels.size() > ExactFloatIntegerLimit * 2u ||
        packed.tlasInstanceIndices.size() > ExactFloatIntegerLimit)
    {
        if (diagnostic)
            *diagnostic = "Ray scene exceeds the exact 24-bit integer range of GL33 float traversal fields";
        return false;
    }
    GLint maxTextureSize = 0, maxArrayLayers = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxArrayLayers);
    if (packed.textureWidth > maxTextureSize || packed.textureHeight > maxTextureSize ||
        packed.textureLayerCount > maxArrayLayers)
    {
        if (diagnostic)
            *diagnostic = "Ray material atlas exceeds GL3.3 texture size/layer limits";
        return false;
    }
    if (!uploadBLAS && !uploadInstances && !uploadMaterials) return true;
    GLuint tb = 0, tt = 0, nb = 0, nt = 0, ib = 0, it = 0;
    GLuint xb = 0, xt = 0, tnb = 0, tnt = 0, tib = 0, tit = 0;
    GLuint xib = 0, xit = 0;
    GLuint mb = 0, mt = 0, atlas = 0;
    bool okay = true;
    if (uploadBLAS)
        okay = UploadTBO(GL_RGBA32F, packed.triangleTexels, tb, tt) &&
            UploadTBO(GL_RGBA32F, packed.blasNodeTexels, nb, nt) &&
            UploadTBO(GL_R32F, packed.blasTriangleIndices, ib, it);
    if (okay && uploadInstances)
        okay = UploadTBO(GL_RGBA32F, packed.instanceTexels, xb, xt) &&
            UploadTBO(GL_RGBA32UI, packed.instanceIdentityTexels, xib, xit) &&
            UploadTBO(GL_RGBA32F, packed.tlasNodeTexels, tnb, tnt) &&
            UploadTBO(GL_R32F, packed.tlasInstanceIndices, tib, tit);
    if (okay && uploadMaterials)
    {
        okay = UploadTBO(GL_RGBA32F, packed.materialTexels, mb, mt);
        if (okay)
        {
            const unsigned char white[4] = {255, 255, 255, 255};
            const int width = std::max(1, packed.textureWidth);
            const int height = std::max(1, packed.textureHeight);
            const int layers = std::max(1, packed.textureLayerCount);
            glGenTextures(1, &atlas);
            glBindTexture(GL_TEXTURE_2D_ARRAY, atlas);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, width, height, layers,
                0, GL_RGBA, GL_UNSIGNED_BYTE,
                packed.textureArrayRGBA.empty() ? white : packed.textureArrayRGBA.data());
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            okay = atlas != 0;
        }
    }
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    if (failNextUploadForTesting_)
    {
        failNextUploadForTesting_ = false;
        okay = false;
        if (diagnostic) *diagnostic = "Injected GL33 ray-scene upload failure";
    }
    if (!CollectError("GL33 ray-scene upload", diagnostic)) okay = false;
    if (!okay)
    {
        DeletePair(tb, tt); DeletePair(nb, nt); DeletePair(ib, it);
        DeletePair(xb, xt); DeletePair(tnb, tnt); DeletePair(tib, tit);
        DeletePair(xib, xit);
        DeletePair(mb, mt);
        if (atlas) glDeleteTextures(1, &atlas);
        return false;
    }
    if (uploadBLAS)
    {
        DeletePair(triangleBuffer_, triangleTexture_);
        DeletePair(blasNodeBuffer_, blasNodeTexture_);
        DeletePair(blasIndexBuffer_, blasIndexTexture_);
        triangleBuffer_ = tb; triangleTexture_ = tt;
        blasNodeBuffer_ = nb; blasNodeTexture_ = nt;
        blasIndexBuffer_ = ib; blasIndexTexture_ = it;
        ++stats_.blasUploads;
        stats_.resourceAllocations += 6;
        uploadedBLASRevision_ = packed.blasRevision;
    }
    if (uploadInstances)
    {
        DeletePair(instanceBuffer_, instanceTexture_);
        DeletePair(instanceIdentityBuffer_, instanceIdentityTexture_);
        DeletePair(tlasNodeBuffer_, tlasNodeTexture_);
        DeletePair(tlasIndexBuffer_, tlasIndexTexture_);
        instanceBuffer_ = xb; instanceTexture_ = xt;
        instanceIdentityBuffer_ = xib; instanceIdentityTexture_ = xit;
        tlasNodeBuffer_ = tnb; tlasNodeTexture_ = tnt;
        tlasIndexBuffer_ = tib; tlasIndexTexture_ = tit;
        ++stats_.instanceUploads;
        stats_.resourceAllocations += 8;
        uploadedInstanceRevision_ = packed.instanceRevision;
    }
    if (uploadMaterials)
    {
        DeletePair(materialBuffer_, materialTexture_);
        if (materialAtlasTexture_) glDeleteTextures(1, &materialAtlasTexture_);
        materialBuffer_ = mb; materialTexture_ = mt;
        materialAtlasTexture_ = atlas;
        ++stats_.materialUploads;
        stats_.resourceAllocations += 3;
        uploadedMaterialRevision_ = packed.materialRevision;
    }
    ++stats_.sceneUploads;
    return true;
}

bool UGL33RayTracingBackend::RenderEffects(const FRayEffectInputs& inputs,
                                           FRayEffectOutputs& outputs,
                                           std::string* diagnostic)
{
    outputs = {};
    const unsigned mask = (inputs.features.rayTracedShadows ? ShadowBit : 0u) |
        (inputs.features.rayTracedGI && inputs.quality.giSamples > 0 ? GIBit : 0u) |
        (inputs.features.rayTracedReflections && inputs.quality.reflStrength > 0.0f
            ? ReflectionBit : 0u);
    if (!inputs.features.rayTracing || !mask) return true;
    if (!inputs.gbuffer || !inputs.scene || !inputs.rasterLighting ||
        !inputs.gbuffer->IsComplete() || !inputs.rasterLighting->valid ||
        inputs.gbuffer->ContextGeneration() != inputs.contextGeneration)
    {
        if (diagnostic) *diagnostic = "GL33 ray effects rejected invalid immutable inputs";
        return false;
    }
    if (!Init(inputs.contextGeneration, diagnostic)) return false;
    FStateGuard restore;
    // All temporary texture object creation/upload below is confined to a
    // texture unit captured by FState. A caller may legally leave unit 16+
    // active; touching that uncaptured unit would leak bindings on return.
    glActiveTexture(GL_TEXTURE0);
    const FPackedRayScene& packed = sceneCache_.Prepare(*inputs.scene);
    stats_.residentBLAS = sceneCache_.Stats().residentBLAS;
    if (!packed.valid)
    {
        if (diagnostic) *diagnostic = packed.diagnostic;
        return false;
    }
    if (!ResizeOutputs(inputs.width, inputs.height, mask,
                       inputs.contextGeneration, diagnostic)) return false;
    if (!UploadScene(packed, diagnostic)) return false;

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, width_, height_);
    const GLenum drawBuffers[3] = {
        static_cast<GLenum>(mask & ShadowBit ? GL_COLOR_ATTACHMENT0 : GL_NONE),
        static_cast<GLenum>(mask & GIBit ? GL_COLOR_ATTACHMENT1 : GL_NONE),
        static_cast<GLenum>(mask & ReflectionBit ? GL_COLOR_ATTACHMENT2 : GL_NONE),
    };
    glDrawBuffers(3, drawBuffers);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND);
    for (int i = 0; i < CapturedDrawBuffers; ++i)
    {
        glDisablei(GL_BLEND, i);
        glColorMaski(i, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB); glDisable(GL_RASTERIZER_DISCARD);
    glDisable(GL_STENCIL_TEST); glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable(GL_SAMPLE_COVERAGE); glDisable(GL_DITHER);
    glDisable(GL_PRIMITIVE_RESTART); glDisable(GL_DEPTH_CLAMP);
    glDisable(GL_POLYGON_OFFSET_FILL); glDepthRange(0.0, 1.0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glFrontFace(GL_CCW);
    glUseProgram(program_);
    glBindVertexArray(fullscreenVAO_);
    const GLuint textures2D[] = {
        inputs.gbuffer->Texture(EHardwareGBufferSemantic::PositionCoverage),
        inputs.gbuffer->Texture(EHardwareGBufferSemantic::GeometricNormal),
        inputs.gbuffer->Texture(EHardwareGBufferSemantic::ShadingNormalModel),
        inputs.gbuffer->Texture(EHardwareGBufferSemantic::AlbedoShininess),
        inputs.gbuffer->Texture(EHardwareGBufferSemantic::SpecularMirror),
        inputs.gbuffer->Texture(EHardwareGBufferSemantic::Identity),
        inputs.environmentTexture,
    };
    for (int unit = 0; unit < 7; ++unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, textures2D[unit]);
        glBindSampler(unit, 0);
    }
    const GLuint bufferTextures[] = {triangleTexture_, blasNodeTexture_,
        blasIndexTexture_, instanceTexture_, tlasNodeTexture_, tlasIndexTexture_,
        instanceIdentityTexture_, materialTexture_};
    for (int index = 0; index < 8; ++index)
    {
        const int unit = 7 + index;
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_BUFFER, bufferTextures[index]);
        glBindSampler(unit, 0);
    }
    glActiveTexture(GL_TEXTURE15);
    glBindTexture(GL_TEXTURE_2D_ARRAY, materialAtlasTexture_);
    glBindSampler(15, 0);
    glUniform1i(glGetUniformLocation(program_, "uDoShadows"), mask & ShadowBit ? 1 : 0);
    glUniform1i(glGetUniformLocation(program_, "uDoGI"), mask & GIBit ? 1 : 0);
    glUniform1i(glGetUniformLocation(program_, "uDoReflections"), mask & ReflectionBit ? 1 : 0);
    glUniform1i(glGetUniformLocation(program_, "uHasSky"), inputs.environmentTexture ? 1 : 0);
    glUniform1i(glGetUniformLocation(program_, "uInstanceCount"), packed.instanceCount);
    glUniform3fv(glGetUniformLocation(program_, "uEye"), 1,
        glm::value_ptr(inputs.scene->camera.eye));
    glUniform3fv(glGetUniformLocation(program_, "uEnvironmentTint"), 1,
        glm::value_ptr(inputs.scene->environment.tint));
    glUniform3fv(glGetUniformLocation(program_, "uSkyHorizon"), 1,
        glm::value_ptr(inputs.scene->environment.horizon));
    glUniform3fv(glGetUniformLocation(program_, "uSkyZenith"), 1,
        glm::value_ptr(inputs.scene->environment.zenith));
    glUniform1f(glGetUniformLocation(program_, "uSkyExponent"),
        inputs.scene->environment.exponent);
    glUniform1i(glGetUniformLocation(program_, "uGISamples"),
        std::clamp(inputs.quality.giSamples, 0, 32));
    glUniform1i(glGetUniformLocation(program_, "uGIBounces"),
        std::clamp(inputs.quality.giBounces, 0, 4));
    glUniform1f(glGetUniformLocation(program_, "uGIStrength"), inputs.quality.giStrength);
    glUniform1f(glGetUniformLocation(program_, "uReflectionStrength"), inputs.quality.reflStrength);
    glUniform1i(glGetUniformLocation(program_, "uShadowSamples"),
        std::clamp(inputs.quality.shadowSamples, 1, 16));
    glUniform1f(glGetUniformLocation(program_, "uShadowSoftness"),
        std::max(inputs.quality.shadowSoftness, 0.0f));
    const int lightCount = std::min<int>(maxLights_, inputs.scene->pointLights.size());
    glUniform1i(glGetUniformLocation(program_, "uLightCount"), lightCount);
    for (int i = 0; i < lightCount; ++i)
    {
        const std::string name = "uLightPositions[" + std::to_string(i) + "]";
        glUniform3fv(glGetUniformLocation(program_, name.c_str()), 1,
            glm::value_ptr(inputs.scene->pointLights[static_cast<std::size_t>(i)].worldPosition));
        const std::string radianceName = "uLightRadiances[" + std::to_string(i) + "]";
        glUniform3fv(glGetUniformLocation(program_, radianceName.c_str()), 1,
            glm::value_ptr(inputs.scene->pointLights[static_cast<std::size_t>(i)].radiance));
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (!CollectError("GL33 ray-effects draw", diagnostic)) return false;
    ++stats_.renderCalls;
    ++stats_.rayDraws;
    if (shadowTexture_) outputs.shadowVisibilityTarget = FRenderOutputView{
        shadowTexture_, width_, height_, true};
    if (giTexture_) outputs.globalIlluminationTarget = FRenderOutputView{
        giTexture_, width_, height_, true};
    if (reflectionTexture_) outputs.reflectionTarget = FRenderOutputView{
        reflectionTexture_, width_, height_, true};
    if (diagnostic) diagnostic->clear();
    return true;
}

void UGL33RayTracingBackend::Shutdown() noexcept
{
    if (contextGeneration_ &&
        contextGeneration_ == ActiveRenderTargetContextGeneration())
        DeleteCurrentResources();
    else ForgetCurrentResources();
}

void UGL33RayTracingBackend::DeleteCurrentResources() noexcept
{
    const std::size_t before = (program_ ? 1u : 0u) +
        (fullscreenVAO_ ? 1u : 0u) + (framebuffer_ ? 1u : 0u) +
        OwnedOutputTextureCount() +
        (triangleBuffer_ ? 2u : 0u) + (blasNodeBuffer_ ? 2u : 0u) +
        (blasIndexBuffer_ ? 2u : 0u) + (instanceBuffer_ ? 2u : 0u) +
        (instanceIdentityBuffer_ ? 2u : 0u) +
        (tlasNodeBuffer_ ? 2u : 0u) + (tlasIndexBuffer_ ? 2u : 0u) +
        (materialBuffer_ ? 2u : 0u) + (materialAtlasTexture_ ? 1u : 0u);
    if (shadowTexture_) glDeleteTextures(1, &shadowTexture_);
    if (giTexture_) glDeleteTextures(1, &giTexture_);
    if (reflectionTexture_) glDeleteTextures(1, &reflectionTexture_);
    if (framebuffer_) glDeleteFramebuffers(1, &framebuffer_);
    DeletePair(triangleBuffer_, triangleTexture_);
    DeletePair(blasNodeBuffer_, blasNodeTexture_);
    DeletePair(blasIndexBuffer_, blasIndexTexture_);
    DeletePair(instanceBuffer_, instanceTexture_);
    DeletePair(instanceIdentityBuffer_, instanceIdentityTexture_);
    DeletePair(tlasNodeBuffer_, tlasNodeTexture_);
    DeletePair(tlasIndexBuffer_, tlasIndexTexture_);
    DeletePair(materialBuffer_, materialTexture_);
    if (materialAtlasTexture_) glDeleteTextures(1, &materialAtlasTexture_);
    if (fullscreenVAO_) glDeleteVertexArrays(1, &fullscreenVAO_);
    if (program_) glDeleteProgram(program_);
    stats_.releasedResources += before;
    ForgetCurrentResources();
}

void UGL33RayTracingBackend::ForgetCurrentResources() noexcept
{
    const std::size_t abandoned = (program_ ? 1u : 0u) +
        (fullscreenVAO_ ? 1u : 0u) + (framebuffer_ ? 1u : 0u) +
        OwnedOutputTextureCount() +
        (triangleBuffer_ ? 2u : 0u) + (blasNodeBuffer_ ? 2u : 0u) +
        (blasIndexBuffer_ ? 2u : 0u) + (instanceBuffer_ ? 2u : 0u) +
        (instanceIdentityBuffer_ ? 2u : 0u) +
        (tlasNodeBuffer_ ? 2u : 0u) + (tlasIndexBuffer_ ? 2u : 0u) +
        (materialBuffer_ ? 2u : 0u) + (materialAtlasTexture_ ? 1u : 0u);
    if (contextGeneration_ && contextGeneration_ != ActiveRenderTargetContextGeneration())
        stats_.abandonedResources += abandoned;
    program_ = fullscreenVAO_ = framebuffer_ = 0;
    shadowTexture_ = giTexture_ = reflectionTexture_ = 0;
    triangleBuffer_ = triangleTexture_ = 0;
    blasNodeBuffer_ = blasNodeTexture_ = 0;
    blasIndexBuffer_ = blasIndexTexture_ = 0;
    instanceBuffer_ = instanceTexture_ = 0;
    instanceIdentityBuffer_ = instanceIdentityTexture_ = 0;
    tlasNodeBuffer_ = tlasNodeTexture_ = 0;
    tlasIndexBuffer_ = tlasIndexTexture_ = 0;
    materialBuffer_ = materialTexture_ = materialAtlasTexture_ = 0;
    width_ = height_ = 0;
    outputMask_ = 0;
    contextGeneration_ = 0;
    uploadedBLASRevision_ = 0;
    uploadedInstanceRevision_ = 0;
    uploadedMaterialRevision_ = 0;
    stats_.ownedOutputTextures = 0;
    sceneCache_.Clear();
}

FOpenGLRayTracingBackendFactory::FOpenGLRayTracingBackendFactory()
    : compatibleCreator_([] { return std::make_unique<UGL33RayTracingBackend>(); }),
      computeCreator_([] { return std::make_unique<UGL43RayTracingBackend>(); })
{
}

void FStderrRayEffectsWarningSink::Warn(const std::string& warning)
{
    std::fprintf(stderr, "[Renderer] warning: %s\n", warning.c_str());
}
