#include "UGL33RayTracingBackend.h"

#include "FRenderScene.h"
#include "FRenderTarget.h"
#include "FPixelUnpackGuard.h"
#include "FTextureSamplingPolicy.h"
#include "Shaders/RayEffectsFragmentShaders.h"
#include "FRenderHistory.h"
#include "Shaders/SharedLightingShaderSource.h"
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
                   std::string& diagnostic, FRayTracingBackendStats& stats)
{
    result = glCreateShader(stage);
    stats.resourceAllocations += result ? 1u : 0u;
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

bool BuildProgram(GLuint& program, std::string& diagnostic, FRayTracingBackendStats& stats)
{
    GLuint vertex = 0, fragment = 0;
    const std::string fragmentSource =
        SharedLightingShaderSource::BuildRayEffectsFragmentShader();
    if (!CompileShader(GL_VERTEX_SHADER, RayEffectsFragmentShaders::FullscreenVertex,
                       vertex, diagnostic, stats) ||
        !CompileShader(GL_FRAGMENT_SHADER, fragmentSource.c_str(),
                       fragment, diagnostic, stats))
    {
        if (vertex) { glDeleteShader(vertex); ++stats.releasedResources; }
        if (fragment) { glDeleteShader(fragment); ++stats.releasedResources; }
        return false;
    }
    program = glCreateProgram();
    stats.resourceAllocations += program ? 1u : 0u;
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex); stats.releasedResources += vertex ? 1u : 0u;
    glDeleteShader(fragment); stats.releasedResources += fragment ? 1u : 0u;
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
    glDeleteProgram(program); stats.releasedResources += program ? 1u : 0u;
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
               GLuint& buffer, GLuint& texture, FRayTracingBackendStats& stats)
{
    const T zero{};
    glGenBuffers(1, &buffer);
    stats.resourceAllocations += buffer ? 1u : 0u;
    glBindBuffer(GL_TEXTURE_BUFFER, buffer);
    glBufferData(GL_TEXTURE_BUFFER,
        static_cast<GLsizeiptr>(values.empty() ? sizeof(T) : values.size() * sizeof(T)),
        values.empty() ? &zero : values.data(), GL_STATIC_DRAW);
    ++stats.bufferUploadCalls;
    glGenTextures(1, &texture);
    stats.resourceAllocations += texture ? 1u : 0u;
    glBindTexture(GL_TEXTURE_BUFFER, texture);
    glTexBuffer(GL_TEXTURE_BUFFER, internalFormat, buffer);
    return buffer != 0 && texture != 0;
}

void DeletePair(GLuint& buffer, GLuint& texture, FRayTracingBackendStats& stats)
{
    if (texture) { glDeleteTextures(1, &texture); ++stats.releasedResources; }
    if (buffer) { glDeleteBuffers(1, &buffer); ++stats.releasedResources; }
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
    auto& stats = stats_;
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
    if (!BuildProgram(candidateProgram, error, stats))
    {
        if (diagnostic) *diagnostic = error;
        return false;
    }
    glGenVertexArrays(1, &candidateVAO);
    stats.resourceAllocations += candidateVAO ? 1u : 0u;
    if (!candidateVAO || !CollectError("GL33 ray-effects initialization", &error))
    {
        if (candidateVAO) { glDeleteVertexArrays(1, &candidateVAO); ++stats.releasedResources; }
        glDeleteProgram(candidateProgram); stats.releasedResources += candidateProgram ? 1u : 0u;
        if (diagnostic) *diagnostic = error.empty()
            ? "GL33 ray effects failed to create fullscreen VAO" : error;
        return false;
    }
    program_ = candidateProgram;
    fullscreenVAO_ = candidateVAO;
    contextGeneration_ = contextGeneration;

    glUseProgram(program_);
    const char* names[] = {"uPositionCoverage", "uGeometricNormal",
        "uShadingNormalModel", "uAlbedoShininess", "uSpecularMirror",
        "uIdentity", "uUnshadowedDirect", "uSky", "uTriangles", "uBLASNodes",
        "uBLASIndices", "uInstances", "uTLASNodes", "uTLASIndices",
        "uMaterials", "uMaterialAtlas"};
    for (int unit = 0; unit < UsedTextureUnits; ++unit)
        glUniform1i(glGetUniformLocation(program_, names[unit]), unit);
    if (diagnostic) diagnostic->clear();
    return CollectError("GL33 ray-effects sampler setup", diagnostic);
}

std::size_t UGL33RayTracingBackend::OwnedOutputTextureCount() const
{
    return (shadowedDirectTexture_ ? 1u : 0u) + (giTexture_ ? 1u : 0u) +
        (reflectionTexture_ ? 1u : 0u);
}

bool UGL33RayTracingBackend::ResizeOutputs(int width, int height, unsigned mask,
                                           std::uint64_t contextGeneration,
                                           std::string* diagnostic)
{
    auto& stats = stats_;
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
    ++stats.outputAllocationAttempts;
    GLuint candidateFBO = 0, candidateShadow = 0, candidateGI = 0, candidateReflection = 0;
    glGenFramebuffers(1, &candidateFBO);
    stats.resourceAllocations += candidateFBO ? 1u : 0u;
    glBindFramebuffer(GL_FRAMEBUFFER, candidateFBO);
    auto makeTexture = [&](GLuint& texture, GLenum internalFormat, GLenum format,
                           GLenum attachment)
    {
        glGenTextures(1, &texture);
        stats.resourceAllocations += texture ? 1u : 0u;
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
                     format, GL_FLOAT, nullptr);
        ++stats.textureUploadCalls;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, texture, 0);
    };
    if (mask & ShadowBit) makeTexture(candidateShadow, GL_RGBA16F, GL_RGBA, GL_COLOR_ATTACHMENT0);
    if (mask & GIBit) makeTexture(candidateGI, GL_RGBA16F, GL_RGBA, GL_COLOR_ATTACHMENT1);
    if (mask & ReflectionBit) makeTexture(candidateReflection, GL_RGBA16F, GL_RGBA, GL_COLOR_ATTACHMENT2);
    const GLenum drawBuffers[3] = {
        static_cast<GLenum>(mask & ShadowBit ? GL_COLOR_ATTACHMENT0 : GL_NONE),
        static_cast<GLenum>(mask & GIBit ? GL_COLOR_ATTACHMENT1 : GL_NONE),
        static_cast<GLenum>(mask & ReflectionBit ? GL_COLOR_ATTACHMENT2 : GL_NONE),
    };
    glDrawBuffers(3, drawBuffers);
    glReadBuffer(GL_NONE); // No readback: valid for every effect mask on baseline GL 3.3.
    const bool complete = candidateFBO && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
        CollectError("GL33 ray-effects output creation", diagnostic);
    if (!complete)
    {
        if (candidateShadow) { glDeleteTextures(1, &candidateShadow); ++stats.releasedResources; }
        if (candidateGI) { glDeleteTextures(1, &candidateGI); ++stats.releasedResources; }
        if (candidateReflection) { glDeleteTextures(1, &candidateReflection); ++stats.releasedResources; }
        if (candidateFBO) { glDeleteFramebuffers(1, &candidateFBO); ++stats.releasedResources; }
        if (diagnostic && diagnostic->empty()) *diagnostic = "GL33 ray-effects framebuffer is incomplete";
        return false;
    }
    if (shadowedDirectTexture_) { glDeleteTextures(1, &shadowedDirectTexture_); ++stats.releasedResources; }
    if (giTexture_) { glDeleteTextures(1, &giTexture_); ++stats.releasedResources; }
    if (reflectionTexture_) { glDeleteTextures(1, &reflectionTexture_); ++stats.releasedResources; }
    if (framebuffer_) { glDeleteFramebuffers(1, &framebuffer_); ++stats.releasedResources; }
    framebuffer_ = candidateFBO;
    shadowedDirectTexture_ = candidateShadow;
    giTexture_ = candidateGI;
    reflectionTexture_ = candidateReflection;
    width_ = width;
    height_ = height;
    outputMask_ = mask;
    ++stats_.outputAllocations;

    stats_.ownedOutputTextures = OwnedOutputTextureCount();
    return true;
}

bool UGL33RayTracingBackend::UploadScene(const FPackedRayScene& packed,
                                         float requestedAnisotropy,
                                         std::string* diagnostic)
{
    auto& stats = stats_;
    const FTextureSamplingPolicy sampling = TextureSamplingPolicyForContext(
        contextGeneration_, requestedAnisotropy);
    if (packed.maximumBLASDepth > 60 || packed.tlasDepth > 28)
    {
        if (diagnostic)
            *diagnostic = "Ray scene BVH exceeds bounded GL3.3 traversal stacks; split the mesh or scene";
        return false;
    }
    const bool uploadBLAS = packed.blasRevision != uploadedBLASRevision_;
    const bool uploadInstances = packed.instanceRevision != uploadedInstanceRevision_;
    const bool uploadMaterials = packed.materialRevision != uploadedMaterialRevision_;
    const float effectiveAnisotropy = sampling.EffectiveAnisotropy();
    auto fits = [&](std::size_t texels) {
        return texels <= static_cast<std::size_t>(maxTextureBufferTexels_);
    };
    if (!fits(packed.triangleTexels.size()) || !fits(packed.blasNodeTexels.size()) ||
        !fits(packed.blasTriangleIndices.size()) || !fits(packed.instanceTexels.size()) ||
        !fits(packed.tlasNodeTexels.size()) || !fits(packed.tlasInstanceIndices.size()) ||
        !fits(packed.materialTexels.size() + packed.primaryOpticsTexels.size()))
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
    if (!uploadMaterials && materialAtlasTexture_ && sampling.anisotropySupported &&
        materialAtlasAnisotropy_ != effectiveAnisotropy)
    {
        glBindTexture(GL_TEXTURE_2D_ARRAY, materialAtlasTexture_);
        glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                        effectiveAnisotropy);
        if (!CollectError("GL33 ray material anisotropy update", diagnostic))
            return false;
        materialAtlasAnisotropy_ = effectiveAnisotropy;
    }
    if (!uploadBLAS && !uploadInstances && !uploadMaterials) return true;
    ++stats.sceneUploadAttempts;
    GLuint tb = 0, tt = 0, nb = 0, nt = 0, ib = 0, it = 0;
    GLuint xb = 0, xt = 0, tnb = 0, tnt = 0, tib = 0, tit = 0;
    GLuint mb = 0, mt = 0, atlas = 0;
    bool okay = true;
    if (uploadBLAS)
    {
        ++stats.blasUploadAttempts;
        okay = UploadTBO(GL_RGBA32F, packed.triangleTexels, tb, tt, stats) &&
            UploadTBO(GL_RGBA32F, packed.blasNodeTexels, nb, nt, stats) &&
            UploadTBO(GL_R32F, packed.blasTriangleIndices, ib, it, stats);
    }
    if (okay && uploadInstances)
    {
        ++stats.instanceUploadAttempts;
        okay = UploadTBO(GL_RGBA32F, packed.instanceTexels, xb, xt, stats) &&
            UploadTBO(GL_RGBA32F, packed.tlasNodeTexels, tnb, tnt, stats) &&
            UploadTBO(GL_R32F, packed.tlasInstanceIndices, tib, tit, stats);
    }
    if (okay && uploadMaterials)
    {
        ++stats.materialUploadAttempts;
        std::vector<glm::vec4> opticalMaterials = packed.materialTexels;
        opticalMaterials.insert(opticalMaterials.end(), packed.primaryOpticsTexels.begin(),
                                packed.primaryOpticsTexels.end());
        okay = UploadTBO(GL_RGBA32F, opticalMaterials, mb, mt, stats);
        if (okay)
        {
            const unsigned char white[4] = {255, 255, 255, 255};
            const int width = std::max(1, packed.textureWidth);
            const int height = std::max(1, packed.textureHeight);
            const int layers = std::max(1, packed.textureLayerCount);
            glGenTextures(1, &atlas);
            stats.resourceAllocations += atlas ? 1u : 0u;
            glBindTexture(GL_TEXTURE_2D_ARRAY, atlas);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_SRGB8_ALPHA8, width, height, layers,
                0, GL_RGBA, GL_UNSIGNED_BYTE,
                packed.textureArrayRGBA.empty() ? white : packed.textureArrayRGBA.data());
            ++stats.textureUploadCalls;
            glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                            GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            if (sampling.anisotropySupported)
                glTexParameterf(GL_TEXTURE_2D_ARRAY,
                    GL_TEXTURE_MAX_ANISOTROPY_EXT,
                    sampling.EffectiveAnisotropy());
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
        DeletePair(tb, tt, stats); DeletePair(nb, nt, stats); DeletePair(ib, it, stats);
        DeletePair(xb, xt, stats); DeletePair(tnb, tnt, stats); DeletePair(tib, tit, stats);
        DeletePair(mb, mt, stats);
        if (atlas) { glDeleteTextures(1, &atlas); ++stats.releasedResources; }
        return false;
    }
    if (uploadBLAS)
    {
        DeletePair(triangleBuffer_, triangleTexture_, stats);
        DeletePair(blasNodeBuffer_, blasNodeTexture_, stats);
        DeletePair(blasIndexBuffer_, blasIndexTexture_, stats);
        triangleBuffer_ = tb; triangleTexture_ = tt;
        blasNodeBuffer_ = nb; blasNodeTexture_ = nt;
        blasIndexBuffer_ = ib; blasIndexTexture_ = it;
        ++stats_.blasUploads;

        uploadedBLASRevision_ = packed.blasRevision;
    }
    if (uploadInstances)
    {
        DeletePair(instanceBuffer_, instanceTexture_, stats);
        DeletePair(tlasNodeBuffer_, tlasNodeTexture_, stats);
        DeletePair(tlasIndexBuffer_, tlasIndexTexture_, stats);
        instanceBuffer_ = xb; instanceTexture_ = xt;
        tlasNodeBuffer_ = tnb; tlasNodeTexture_ = tnt;
        tlasIndexBuffer_ = tib; tlasIndexTexture_ = tit;
        ++stats_.instanceUploads;

        uploadedInstanceRevision_ = packed.instanceRevision;
    }
    if (uploadMaterials)
    {
        DeletePair(materialBuffer_, materialTexture_, stats);
        if (materialAtlasTexture_) { glDeleteTextures(1, &materialAtlasTexture_); ++stats.releasedResources; }
        materialBuffer_ = mb; materialTexture_ = mt;
        materialAtlasTexture_ = atlas;
        materialAtlasAnisotropy_ = effectiveAnisotropy;
        ++stats_.materialUploads;

        uploadedMaterialRevision_ = packed.materialRevision;
    }
    ++stats_.sceneUploads;
    stats_.residentBLAS = sceneCache_.Stats().residentBLAS;
    return true;
}

bool UGL33RayTracingBackend::RenderEffects(const FRayEffectInputs& inputs,
                                           FRayEffectOutputs& outputs,
                                           std::string* diagnostic)
{
    outputs = {};
    const unsigned mask = (inputs.features.rayTracedShadows ? ShadowBit : 0u) |
        (inputs.features.rayTracedGI && inputs.quality.giSamples > 0 ? GIBit : 0u) |
        ((inputs.features.rayTracedReflections && inputs.quality.reflStrength > 0.0f) ||
         inputs.features.rayTracedTranslucency
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
    FPixelUnpackGuard unpack;
    // All temporary texture object creation/upload below is confined to a
    // texture unit captured by FState. A caller may legally leave unit 16+
    // active; touching that uncaptured unit would leak bindings on return.
    glActiveTexture(GL_TEXTURE0);
    const FPackedRayScene& packed = sceneCache_.Prepare(*inputs.scene);
    sceneWarnings_ = packed.warnings;
    if (!packed.valid)
    {
        if (diagnostic) *diagnostic = packed.diagnostic;
        return false;
    }
    if (!ResizeOutputs(inputs.width, inputs.height, mask,
                       inputs.contextGeneration, diagnostic)) return false;
    if (!UploadScene(packed, inputs.quality.anisotropy, diagnostic)) return false;

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
        static_cast<GLuint>(inputs.rasterLighting->unshadowedDirectTarget.identity),
        inputs.environmentTexture,
    };
    for (int unit = 0; unit < 8; ++unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, textures2D[unit]);
        glBindSampler(unit, 0);
    }
    const GLuint bufferTextures[] = {triangleTexture_, blasNodeTexture_,
        blasIndexTexture_, instanceTexture_, tlasNodeTexture_, tlasIndexTexture_,
        materialTexture_};
    for (int index = 0; index < 7; ++index)
    {
        const int unit = 8 + index;
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_BUFFER, bufferTextures[index]);
        glBindSampler(unit, 0);
    }
    glActiveTexture(GL_TEXTURE15);
    glBindTexture(GL_TEXTURE_2D_ARRAY, materialAtlasTexture_);
    glBindSampler(15, 0);
    glUniform1i(glGetUniformLocation(program_, "uDoShadows"), mask & ShadowBit ? 1 : 0);
    glUniform1i(glGetUniformLocation(program_, "uDoGI"), mask & GIBit ? 1 : 0);
    glUniform1i(glGetUniformLocation(program_, "uDoReflections"),
        inputs.features.rayTracedReflections && inputs.quality.reflStrength > 0.0f ? 1 : 0);
    glUniform1i(glGetUniformLocation(program_, "uDoTranslucency"),
        inputs.features.rayTracedTranslucency ? 1 : 0);
    glUniform1i(glGetUniformLocation(program_, "uPrimaryOpticsBase"),
        static_cast<int>(packed.materialTexels.size()));
    glUniform1i(glGetUniformLocation(program_, "uPrimaryMaterialCount"),
        static_cast<int>(packed.primaryOpticsTexels.size() / 2u));
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
    glUniform1i(glGetUniformLocation(program_, "uFrameIndex"),
        TemporalShaderFrameIndex({inputs.historySignature, inputs.frameIndex,
                                  false, 1}));
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
        const std::string sourceName = "uLightSources[" + std::to_string(i) + "]";
        glUniform3fv(glGetUniformLocation(program_, sourceName.c_str()), 1,
            glm::value_ptr(inputs.scene->pointLights[static_cast<std::size_t>(i)].sourceIntensity));
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);
    ++stats_.rayDraws;
    if (!CollectError("GL33 ray-effects draw", diagnostic)) return false;
    ++stats_.renderCalls;
    if (shadowedDirectTexture_) outputs.shadowedDirectTarget = FRenderOutputView{
        shadowedDirectTexture_, width_, height_, true};
    if (giTexture_) outputs.globalIlluminationTarget = FRenderOutputView{
        giTexture_, width_, height_, true};
    if (reflectionTexture_) outputs.opticalContributionTarget = FRenderOutputView{
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
    auto& stats = stats_;
    if (shadowedDirectTexture_) { glDeleteTextures(1, &shadowedDirectTexture_); ++stats.releasedResources; }
    if (giTexture_) { glDeleteTextures(1, &giTexture_); ++stats.releasedResources; }
    if (reflectionTexture_) { glDeleteTextures(1, &reflectionTexture_); ++stats.releasedResources; }
    if (framebuffer_) { glDeleteFramebuffers(1, &framebuffer_); ++stats.releasedResources; }
    DeletePair(triangleBuffer_, triangleTexture_, stats);
    DeletePair(blasNodeBuffer_, blasNodeTexture_, stats);
    DeletePair(blasIndexBuffer_, blasIndexTexture_, stats);
    DeletePair(instanceBuffer_, instanceTexture_, stats);
    DeletePair(tlasNodeBuffer_, tlasNodeTexture_, stats);
    DeletePair(tlasIndexBuffer_, tlasIndexTexture_, stats);
    DeletePair(materialBuffer_, materialTexture_, stats);
    if (materialAtlasTexture_) { glDeleteTextures(1, &materialAtlasTexture_); ++stats.releasedResources; }
    if (fullscreenVAO_) { glDeleteVertexArrays(1, &fullscreenVAO_); ++stats.releasedResources; }
    if (program_) { glDeleteProgram(program_); ++stats.releasedResources; }
    ForgetCurrentResources();
}

void UGL33RayTracingBackend::ForgetCurrentResources() noexcept
{
    const std::size_t abandoned = (program_ ? 1u : 0u) +
        (fullscreenVAO_ ? 1u : 0u) + (framebuffer_ ? 1u : 0u) +
        OwnedOutputTextureCount() +
        (triangleBuffer_ ? 2u : 0u) + (blasNodeBuffer_ ? 2u : 0u) +
        (blasIndexBuffer_ ? 2u : 0u) + (instanceBuffer_ ? 2u : 0u) +
        (tlasNodeBuffer_ ? 2u : 0u) + (tlasIndexBuffer_ ? 2u : 0u) +
        (materialBuffer_ ? 2u : 0u) + (materialAtlasTexture_ ? 1u : 0u);
    if (contextGeneration_ && contextGeneration_ != ActiveRenderTargetContextGeneration())
        stats_.abandonedResources += abandoned;
    program_ = fullscreenVAO_ = framebuffer_ = 0;
    shadowedDirectTexture_ = giTexture_ = reflectionTexture_ = 0;
    triangleBuffer_ = triangleTexture_ = 0;
    blasNodeBuffer_ = blasNodeTexture_ = 0;
    blasIndexBuffer_ = blasIndexTexture_ = 0;
    instanceBuffer_ = instanceTexture_ = 0;
    tlasNodeBuffer_ = tlasNodeTexture_ = 0;
    tlasIndexBuffer_ = tlasIndexTexture_ = 0;
    materialBuffer_ = materialTexture_ = materialAtlasTexture_ = 0;
    materialAtlasAnisotropy_ = 1.0f;
    width_ = height_ = 0;
    outputMask_ = 0;
    contextGeneration_ = 0;
    uploadedBLASRevision_ = 0;
    uploadedInstanceRevision_ = 0;
    uploadedMaterialRevision_ = 0;
    stats_.ownedOutputTextures = 0;
    stats_.residentBLAS = 0;
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
