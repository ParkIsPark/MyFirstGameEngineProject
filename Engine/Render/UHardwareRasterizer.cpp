#include "UHardwareRasterizer.h"

#include "FRenderScene.h"
#include "FRenderQuality.h"
#include "FRenderTarget.h"
#include "FPixelUnpackGuard.h"
#include "FTransform.h"
#include "FTextureSamplingPolicy.h"
#include "Material.h"
#include "Shaders/HardwareRasterShaders.h"
#include "Shaders/SharedLightingShaderSource.h"
#include "UGPUMeshCache.h"
#include "UHardwareGBuffer.h"
#include "UMesh.h"
#include "Vertex.h"
#include "ACamera.h"

#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>

#include <cstddef>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace
{
constexpr GLuint GeometryDrawBufferCount = 8;
constexpr GLuint GeometryTextureUnitCount = 2;
constexpr int ShaderPointLightCapacity = 16;

struct FGeometryState
{
    GLint program = 0;
    GLint vertexArray = 0;
    GLint arrayBuffer = 0;
    GLint activeTexture = 0;
    GLint textures2D[GeometryTextureUnitCount] = {};
    GLint samplers[GeometryTextureUnitCount] = {};
    GLint depthFunction = GL_LESS;
    GLint frontFace = GL_CCW;
    GLint scissorBox[4] = {};
    GLint polygonMode[2] = {GL_FILL, GL_FILL};
    GLdouble depthRange[2] = {0.0, 1.0};
    GLboolean colorMasks[GeometryDrawBufferCount][4] = {};
    GLboolean blend[GeometryDrawBufferCount] = {};
    GLboolean depthMask = GL_TRUE;
    GLboolean depthTest = GL_FALSE;
    GLboolean cullFace = GL_FALSE;
    GLboolean scissorTest = GL_FALSE;
    GLboolean rasterizerDiscard = GL_FALSE;
    GLboolean stencilTest = GL_FALSE;
    GLboolean sampleAlphaToCoverage = GL_FALSE;
    GLboolean sampleCoverage = GL_FALSE;
    GLboolean framebufferSRGB = GL_FALSE;
    GLboolean dither = GL_FALSE;
    GLboolean primitiveRestart = GL_FALSE;
    GLboolean depthClamp = GL_FALSE;
    GLboolean polygonOffsetFill = GL_FALSE;
};

FGeometryState CaptureGeometryState()
{
    FGeometryState state;
    glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state.vertexArray);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state.arrayBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &state.activeTexture);
    for (GLuint unit = 0; unit < GeometryTextureUnitCount; ++unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.textures2D[unit]);
        glGetIntegeri_v(GL_SAMPLER_BINDING, unit, &state.samplers[unit]);
    }
    glActiveTexture(static_cast<GLenum>(state.activeTexture));
    glGetIntegerv(GL_DEPTH_FUNC, &state.depthFunction);
    glGetIntegerv(GL_FRONT_FACE, &state.frontFace);
    glGetIntegerv(GL_SCISSOR_BOX, state.scissorBox);
    glGetIntegerv(GL_POLYGON_MODE, state.polygonMode);
    glGetDoublev(GL_DEPTH_RANGE, state.depthRange);
    for (GLuint drawBuffer = 0; drawBuffer < GeometryDrawBufferCount; ++drawBuffer)
    {
        glGetBooleani_v(GL_COLOR_WRITEMASK, drawBuffer,
                        state.colorMasks[drawBuffer]);
        state.blend[drawBuffer] = glIsEnabledi(GL_BLEND, drawBuffer);
    }
    glGetBooleanv(GL_DEPTH_WRITEMASK, &state.depthMask);
    state.depthTest = glIsEnabled(GL_DEPTH_TEST);
    state.cullFace = glIsEnabled(GL_CULL_FACE);
    state.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
    state.rasterizerDiscard = glIsEnabled(GL_RASTERIZER_DISCARD);
    state.stencilTest = glIsEnabled(GL_STENCIL_TEST);
    state.sampleAlphaToCoverage = glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);
    state.sampleCoverage = glIsEnabled(GL_SAMPLE_COVERAGE);
    state.framebufferSRGB = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    state.dither = glIsEnabled(GL_DITHER);
    state.primitiveRestart = glIsEnabled(GL_PRIMITIVE_RESTART);
    state.depthClamp = glIsEnabled(GL_DEPTH_CLAMP);
    state.polygonOffsetFill = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    return state;
}

void RestoreGeometryState(const FGeometryState& state)
{
    if (state.depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (state.cullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    for (GLuint drawBuffer = 0; drawBuffer < GeometryDrawBufferCount; ++drawBuffer)
    {
        if (state.blend[drawBuffer]) glEnablei(GL_BLEND, drawBuffer);
        else glDisablei(GL_BLEND, drawBuffer);
        glColorMaski(drawBuffer,
                     state.colorMasks[drawBuffer][0],
                     state.colorMasks[drawBuffer][1],
                     state.colorMasks[drawBuffer][2],
                     state.colorMasks[drawBuffer][3]);
    }
    if (state.scissorTest) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (state.rasterizerDiscard) glEnable(GL_RASTERIZER_DISCARD); else glDisable(GL_RASTERIZER_DISCARD);
    if (state.stencilTest) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    if (state.sampleAlphaToCoverage) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE); else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    if (state.sampleCoverage) glEnable(GL_SAMPLE_COVERAGE); else glDisable(GL_SAMPLE_COVERAGE);
    if (state.framebufferSRGB) glEnable(GL_FRAMEBUFFER_SRGB); else glDisable(GL_FRAMEBUFFER_SRGB);
    if (state.dither) glEnable(GL_DITHER); else glDisable(GL_DITHER);
    if (state.primitiveRestart) glEnable(GL_PRIMITIVE_RESTART); else glDisable(GL_PRIMITIVE_RESTART);
    if (state.depthClamp) glEnable(GL_DEPTH_CLAMP); else glDisable(GL_DEPTH_CLAMP);
    if (state.polygonOffsetFill) glEnable(GL_POLYGON_OFFSET_FILL); else glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthFunc(static_cast<GLenum>(state.depthFunction));
    glDepthMask(state.depthMask);
    glDepthRange(state.depthRange[0], state.depthRange[1]);
    glScissor(state.scissorBox[0], state.scissorBox[1],
              state.scissorBox[2], state.scissorBox[3]);
    glPolygonMode(GL_FRONT, static_cast<GLenum>(state.polygonMode[0]));
    glPolygonMode(GL_BACK, static_cast<GLenum>(state.polygonMode[1]));
    glFrontFace(static_cast<GLenum>(state.frontFace));
    glUseProgram(static_cast<unsigned>(state.program));
    glBindVertexArray(static_cast<unsigned>(state.vertexArray));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(state.arrayBuffer));
    for (GLuint unit = 0; unit < GeometryTextureUnitCount; ++unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, static_cast<unsigned>(state.textures2D[unit]));
        glBindSampler(unit, static_cast<unsigned>(state.samplers[unit]));
    }
    glActiveTexture(static_cast<GLenum>(state.activeTexture));
}

class FGeometryStateGuard
{
public:
    FGeometryStateGuard(UHardwareGBuffer& gbuffer, const FGeometryState& state)
        : gbuffer_(gbuffer), state_(state) {}
    ~FGeometryStateGuard()
    {
        RestoreGeometryState(state_);
        gbuffer_.EndGeometry();
    }
private:
    UHardwareGBuffer& gbuffer_;
    FGeometryState state_;
};

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
    std::string log(static_cast<std::size_t>(length > 1 ? length : 1), '\0');
    GLsizei written = 0;
    glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), &written, log.data());
    log.resize(static_cast<std::size_t>(written));
    diagnostic = std::string("Hardware raster ") + label + " shader compile failed: " + log;
    return false;
}

constexpr int OpenGLErrorDrainLimit = 16;

bool DrainOpenGLErrors(std::string& diagnostic)
{
    for (int attempt = 0; attempt < OpenGLErrorDrainLimit; ++attempt)
        if (glGetError() == GL_NO_ERROR)
            return true;
    diagnostic = "OpenGL mesh upload could not drain the pre-existing error queue";
    return false;
}

bool CollectOpenGLErrors(const char* operation, std::string& diagnostic)
{
    GLenum first = GL_NO_ERROR;
    bool drained = false;
    for (int attempt = 0; attempt < OpenGLErrorDrainLimit; ++attempt)
    {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR)
        {
            drained = true;
            break;
        }
        if (first == GL_NO_ERROR) first = error;
    }
    if (first == GL_NO_ERROR) return true;
    std::ostringstream stream;
    stream << operation << " produced OpenGL error 0x" << std::hex << first;
    if (!drained) stream << " (error queue did not drain within "
                         << std::dec << OpenGLErrorDrainLimit << " reads)";
    diagnostic = stream.str();
    return false;
}

bool ValidMesh(const UMesh& mesh)
{
    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size() % 3 != 0)
        return false;
    for (std::uint32_t index : mesh.indices)
        if (index >= mesh.vertices.size()) return false;
    return mesh.indices.size() <= static_cast<std::size_t>(std::numeric_limits<GLsizei>::max());
}

const FResolvedRenderMaterial& MaterialFor(const FRenderMeshInstance& instance,
                                           std::uint32_t slot,
                                           std::uint32_t& identity)
{
    static const FResolvedRenderMaterial fallback;
    if (instance.materialOverride)
    {
        identity = instance.materialOverrideIdentity;
        return *instance.materialOverride;
    }
    if (instance.materialSlots.empty())
    {
        identity = 0;
        return fallback;
    }
    const std::size_t resolvedSlot = slot < instance.materialSlots.size() ? slot : 0;
    identity = resolvedSlot < instance.materialSlotIdentities.size()
        ? instance.materialSlotIdentities[resolvedSlot] : 0;
    return instance.materialSlots[resolvedSlot];
}

void UploadMaterial(GLuint program, const FRenderMeshInstance& instance,
                    const FResolvedRenderMaterial& material,
                    std::uint32_t materialIdentity, unsigned texture)
{
    glUniform3fv(glGetUniformLocation(program, "uAlbedo"), 1,
                 glm::value_ptr(material.albedo));
    glUniform3fv(glGetUniformLocation(program, "uAmbient"), 1,
                 glm::value_ptr(material.ambient));
    glUniform3fv(glGetUniformLocation(program, "uSpecular"), 1,
                 glm::value_ptr(material.specularColor));
    glUniform3fv(glGetUniformLocation(program, "uEmissive"), 1,
                 glm::value_ptr(material.emissive));
    glUniform1f(glGetUniformLocation(program, "uShininess"), material.shininess);
    glUniform1f(glGetUniformLocation(program, "uMirrorFactor"), material.mirrorFactor);
    glUniform1ui(glGetUniformLocation(program, "uMaterialIdentity"), materialIdentity);

    glm::vec2 tiling = instance.uvTiling;
    bool repeat = true;
    if (material.source)
    {
        tiling *= material.source->uvTiling;
        repeat = material.source->wrapMode == EWrapMode::Repeat;
    }
    glUniform2fv(glGetUniformLocation(program, "uUVTiling"), 1, glm::value_ptr(tiling));
    glUniform1i(glGetUniformLocation(program, "uRepeatDiffuseTexture"), repeat ? 1 : 0);
    glUniform1i(glGetUniformLocation(program, "uHasDiffuseTexture"), texture ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
}

std::uint64_t TextureSignature(const Material& material)
{
    std::uint64_t hash = 1469598103934665603ull;
    auto mix = [&](const void* bytes, std::size_t count)
    {
        const unsigned char* data = static_cast<const unsigned char*>(bytes);
        for (std::size_t i = 0; i < count; ++i)
        {
            hash ^= data[i];
            hash *= 1099511628211ull;
        }
    };
    mix(&material.texWidth, sizeof(material.texWidth));
    mix(&material.texHeight, sizeof(material.texHeight));
    mix(&material.texChannels, sizeof(material.texChannels));
    const std::uint64_t runtimeRevision = material.RuntimeRevision();
    mix(&runtimeRevision, sizeof(runtimeRevision));
    const std::size_t size = material.texData.size();
    mix(&size, sizeof(size));
    if (!material.texData.empty()) mix(material.texData.data(), material.texData.size());
    if (!material.diffuseTexPath.empty())
        mix(material.diffuseTexPath.data(), material.diffuseTexPath.size());
    return hash;
}
}

std::uint64_t FOpenGLMeshUploadAdapter::ActiveContextGeneration() const noexcept
{
    return ActiveRenderTargetContextGeneration();
}

FGPUMeshResource FOpenGLMeshUploadAdapter::CreateAndUpload(
    const FMeshGPUUploadView& upload,
    std::uint64_t contextGeneration)
{
    if (contextGeneration == 0 ||
        contextGeneration != ActiveRenderTargetContextGeneration())
        throw std::invalid_argument(
            "GPU mesh upload context generation does not match the active context");
    if (!upload.vertices || !upload.indices || upload.vertexCount == 0 || upload.indexCount == 0)
        throw std::runtime_error("GPU mesh upload rejected empty geometry");

    std::string diagnostic;
    if (!DrainOpenGLErrors(diagnostic)) throw std::runtime_error(diagnostic);

    GLint previousVAO = 0;
    GLint previousArrayBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVAO);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);

    FGPUMeshResource resource;
    resource.contextGeneration = contextGeneration;
    glGenVertexArrays(1, &resource.vao);
    glGenBuffers(1, &resource.vertexBuffer);
    glGenBuffers(1, &resource.indexBuffer);
    if (!resource.vao || !resource.vertexBuffer || !resource.indexBuffer)
    {
        if (resource.indexBuffer) glDeleteBuffers(1, &resource.indexBuffer);
        if (resource.vertexBuffer) glDeleteBuffers(1, &resource.vertexBuffer);
        if (resource.vao) glDeleteVertexArrays(1, &resource.vao);
        throw std::runtime_error("OpenGL failed to allocate VAO/VBO/EBO for a mesh");
    }

    static_assert(std::is_standard_layout<Vertex>::value, "Vertex must support offsetof");
    glBindVertexArray(resource.vao);
    glBindBuffer(GL_ARRAY_BUFFER, resource.vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(upload.vertexCount * sizeof(Vertex)),
                 upload.vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, resource.indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(upload.indexCount * sizeof(std::uint32_t)),
                 upload.indices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, uv)));
    resource.indexCount = upload.indexCount;

    glBindVertexArray(static_cast<unsigned>(previousVAO));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(previousArrayBuffer));
    if (!CollectOpenGLErrors("OpenGL mesh upload", diagnostic))
    {
        if (resource.indexBuffer) glDeleteBuffers(1, &resource.indexBuffer);
        if (resource.vertexBuffer) glDeleteBuffers(1, &resource.vertexBuffer);
        if (resource.vao) glDeleteVertexArrays(1, &resource.vao);
        resource = {};
        throw std::runtime_error(diagnostic);
    }
    return resource;
}

bool FOpenGLMeshUploadAdapter::Reupload(FGPUMeshResource& resource,
                                        const FMeshGPUUploadView& upload,
                                        std::string& diagnostic) noexcept
{
    if (!resource.vao || !resource.vertexBuffer || !resource.indexBuffer ||
        !upload.vertices || !upload.indices || upload.vertexCount == 0 || upload.indexCount == 0)
    {
        diagnostic = "GPU mesh reupload rejected invalid resource or empty geometry";
        return false;
    }
    if (resource.contextGeneration == 0 ||
        resource.contextGeneration != ActiveRenderTargetContextGeneration())
    {
        diagnostic = "GPU mesh reupload rejected a resource from another context generation";
        return false;
    }
    if (!DrainOpenGLErrors(diagnostic)) return false;
    GLint previousVAO = 0;
    GLint previousArrayBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVAO);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
    glBindVertexArray(resource.vao);
    glBindBuffer(GL_ARRAY_BUFFER, resource.vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(upload.vertexCount * sizeof(Vertex)),
                 upload.vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, resource.indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(upload.indexCount * sizeof(std::uint32_t)),
                 upload.indices, GL_STATIC_DRAW);
    glBindVertexArray(static_cast<unsigned>(previousVAO));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(previousArrayBuffer));
    if (!CollectOpenGLErrors("OpenGL mesh reupload", diagnostic)) return false;
    diagnostic.clear();
    return true;
}

void FOpenGLMeshUploadAdapter::Destroy(FGPUMeshResource& resource) noexcept
{
    if (resource.contextGeneration == 0 ||
        resource.contextGeneration != ActiveRenderTargetContextGeneration())
    {
        Abandon(resource);
        return;
    }
    if (resource.indexBuffer) glDeleteBuffers(1, &resource.indexBuffer);
    if (resource.vertexBuffer) glDeleteBuffers(1, &resource.vertexBuffer);
    if (resource.vao) glDeleteVertexArrays(1, &resource.vao);
    resource = FGPUMeshResource{};
}

void FOpenGLMeshUploadAdapter::Abandon(FGPUMeshResource& resource) noexcept
{
    resource = FGPUMeshResource{};
}

UHardwareRasterizer::~UHardwareRasterizer() noexcept
{
    Shutdown();
}

bool UHardwareRasterizer::Init(std::uint64_t contextGeneration,
                               std::string* diagnostic)
{
    if (contextGeneration == 0 ||
        contextGeneration != ActiveRenderTargetContextGeneration())
    {
        if (diagnostic)
            *diagnostic = "Hardware raster initialization context generation does not match the active context";
        return false;
    }
    if (program_ && contextGeneration_ == contextGeneration) return true;
    if (program_ && contextGeneration_ != contextGeneration)
    {
        materialTextures_.clear(); // names belonged to the destroyed context
        program_ = 0;
        contextGeneration_ = 0;
    }
    GLint previousProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    std::string error;
    GLuint vertex = 0;
    GLuint geometry = 0;
    GLuint fragment = 0;
    const std::string geometrySource =
        SharedLightingShaderSource::BuildHardwareGeometryShader();
    if (!CompileStage(GL_VERTEX_SHADER, HardwareRasterShaders::Vertex, "vertex", vertex, error) ||
        !CompileStage(GL_GEOMETRY_SHADER, geometrySource.c_str(), "geometry", geometry, error) ||
        !CompileStage(GL_FRAGMENT_SHADER, HardwareRasterShaders::Fragment, "fragment", fragment, error))
    {
        if (vertex) glDeleteShader(vertex);
        if (geometry) glDeleteShader(geometry);
        if (fragment) glDeleteShader(fragment);
        if (diagnostic) *diagnostic = error;
        return false;
    }

    const GLuint createdProgram = glCreateProgram();
    glAttachShader(createdProgram, vertex);
    glAttachShader(createdProgram, geometry);
    glAttachShader(createdProgram, fragment);
    glLinkProgram(createdProgram);
    glDeleteShader(vertex);
    glDeleteShader(geometry);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(createdProgram, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE)
    {
        GLint length = 0;
        glGetProgramiv(createdProgram, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length > 1 ? length : 1), '\0');
        GLsizei written = 0;
        glGetProgramInfoLog(createdProgram, static_cast<GLsizei>(log.size()), &written, log.data());
        log.resize(static_cast<std::size_t>(written));
        error = "Hardware raster program link failed: " + log;
        glDeleteProgram(createdProgram);
        if (diagnostic) *diagnostic = error;
        return false;
    }
    program_ = createdProgram;
    ++resourceAllocations_;
    contextGeneration_ = contextGeneration;
    GLint geometryUniformComponents = 0;
    GLint geometryTextureUnits = 0;
    glGetIntegerv(GL_MAX_GEOMETRY_UNIFORM_COMPONENTS, &geometryUniformComponents);
    glGetIntegerv(GL_MAX_GEOMETRY_TEXTURE_IMAGE_UNITS, &geometryTextureUnits);
    if (geometryTextureUnits < 2 || geometryUniformComponents < 160)
    {
        glDeleteProgram(program_);
        ++releasedResources_;
        program_ = 0;
        contextGeneration_ = 0;
        if (diagnostic)
            *diagnostic = "Hardware raster Gouraud/Flat lighting exceeds geometry-stage GL3.3 limits";
        return false;
    }
    pointLightLimit_ = std::min(ShaderPointLightCapacity,
        std::max(1, (geometryUniformComponents - 64) / 6));
    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "uDiffuseTexture"), 0);
    glUniform1i(glGetUniformLocation(program_, "uEnvironmentTexture"), 1);
    glUseProgram(static_cast<unsigned>(previousProgram));
    if (diagnostic) diagnostic->clear();
    return true;
}

void UHardwareRasterizer::Shutdown() noexcept
{
    ClearMaterialTextures();
    if (program_ && contextGeneration_ != 0 &&
        contextGeneration_ == ActiveRenderTargetContextGeneration())
    {
        glDeleteProgram(program_);
        ++releasedResources_;
    }
    program_ = 0;
    contextGeneration_ = 0;
    pointLightLimit_ = 0;
}

bool UHardwareRasterizer::ResolveMaterialTexture(
    const Material* material, std::uint64_t contextGeneration,
    float requestedAnisotropy,
    unsigned& texture, std::string& diagnostic)
{
    texture = 0;
    if (!material) return true;
    // Legacy externally-owned handles have no reliable color-space or mip
    // contract. Never sample them after removing manual gamma decode; use CPU
    // source bytes below when available, otherwise the scalar albedo fallback.
    const int channels = material->texChannels > 0 ? material->texChannels : 3;
    const std::size_t required = material->texWidth > 0 && material->texHeight > 0
        ? static_cast<std::size_t>(material->texWidth) * material->texHeight * channels : 0;
    if (required == 0 || material->texData.size() < required || channels > 4)
        return true;

    auto signatureIt = materialSignaturesThisFrame_.find(material);
    if (signatureIt == materialSignaturesThisFrame_.end())
    {
        signatureIt = materialSignaturesThisFrame_.emplace(
            material, TextureSignature(*material)).first;
        ++materialTextureHashComputations_;
    }
    const std::uint64_t signature = signatureIt->second;
    auto resourceIt = materialTextures_.find(material);
    if (resourceIt != materialTextures_.end())
    {
        resourceIt->second.lastUsedFrame = materialTextureFrame_;
        texture = resourceIt->second.texture;
        if (texture && resourceIt->second.signature == signature)
        {
            // A committed texture implies this context's capability probe has
            // already completed. Cache hits may therefore update only the
            // sampler parameter without entering the upload error boundary.
            const FTextureSamplingPolicy sampling = TextureSamplingPolicyForContext(
                contextGeneration, requestedAnisotropy);
            const float effectiveAnisotropy = sampling.EffectiveAnisotropy();
            if (sampling.anisotropySupported &&
                resourceIt->second.effectiveAnisotropy != effectiveAnisotropy)
            {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, texture);
                glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                                effectiveAnisotropy);
                if (!CollectOpenGLErrors("Material texture anisotropy update",
                                         diagnostic))
                    return false;
                resourceIt->second.effectiveAnisotropy = effectiveAnisotropy;
            }
            return true;
        }
    }

    if (!DrainOpenGLErrors(diagnostic))
    {
        ++materialTextureUploadFailures_;
        return false;
    }

    // Drain caller errors before the first-context capability query. The query
    // validates only its own glGetFloatv error and cannot consume or cache a
    // fallback because of unrelated state left by the caller.
    const FTextureSamplingPolicy sampling = TextureSamplingPolicyForContext(
        contextGeneration, requestedAnisotropy);
    const float effectiveAnisotropy = sampling.EffectiveAnisotropy();

    FPixelUnpackGuard unpack;
    glActiveTexture(GL_TEXTURE0);
    unsigned candidate = 0;
    glGenTextures(1, &candidate);
    resourceAllocations_ += candidate ? 1u : 0u;
    glBindTexture(GL_TEXTURE_2D, candidate);
    const GLenum format = channels == 4 ? GL_RGBA : channels == 2 ? GL_RG :
                          channels == 1 ? GL_RED : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8,
                 material->texWidth, material->texHeight, 0, format,
                 GL_UNSIGNED_BYTE, material->texData.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (sampling.anisotropySupported)
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                        sampling.EffectiveAnisotropy());

    const bool injectedFailure = failNextMaterialTextureUploadForTesting_;
    failNextMaterialTextureUploadForTesting_ = false;
    if (injectedFailure || !candidate ||
        !CollectOpenGLErrors("Material texture upload", diagnostic))
    {
        if (candidate) { glDeleteTextures(1, &candidate); ++releasedResources_; }
        if (injectedFailure) diagnostic = "Injected material texture upload failure";
        ++materialTextureUploadFailures_;
        return false;
    }

    const unsigned previous = resourceIt == materialTextures_.end()
        ? 0u : resourceIt->second.texture;
    FMaterialTextureResource committed;
    committed.texture = candidate;
    committed.signature = signature;
    committed.lastUsedFrame = materialTextureFrame_;
    committed.effectiveAnisotropy = effectiveAnisotropy;
    materialTextures_[material] = committed;
    if (previous) { glDeleteTextures(1, &previous); ++releasedResources_; }
    texture = candidate;
    ++materialTextureUploads_;
    (void)contextGeneration;
    diagnostic.clear();
    return true;
}

void UHardwareRasterizer::ReleaseUnusedMaterialTextures() noexcept
{
    for (auto it = materialTextures_.begin(); it != materialTextures_.end();)
    {
        if (it->second.lastUsedFrame == materialTextureFrame_)
        {
            ++it;
            continue;
        }
        if (it->second.texture) { glDeleteTextures(1, &it->second.texture); ++releasedResources_; }
        it = materialTextures_.erase(it);
    }
}

void UHardwareRasterizer::ClearMaterialTextures() noexcept
{
    if (contextGeneration_ != 0 &&
        contextGeneration_ == ActiveRenderTargetContextGeneration())
        for (auto& pair : materialTextures_)
            if (pair.second.texture) { glDeleteTextures(1, &pair.second.texture); ++releasedResources_; }
    materialTextures_.clear();
    materialSignaturesThisFrame_.clear();
}

std::uint64_t UHardwareRasterizer::ResourceIdentity() const
{
    std::uint64_t value = (1469598103934665603ull ^ program_) * 1099511628211ull;
    for (const auto& entry : materialTextures_)
    {
        value = (value ^ entry.second.texture) * 1099511628211ull;
        value = (value ^ entry.second.signature) * 1099511628211ull;
    }
    return value;
}

bool UHardwareRasterizer::RenderGeometry(const FRenderScene& scene,
                                         UGPUMeshCache& meshCache,
                                         UHardwareGBuffer& gbuffer,
                                         std::uint64_t contextGeneration,
                                         std::string* diagnostic)
{
    const FRenderQuality quality;
    return RenderGeometry(scene, quality, meshCache, gbuffer, contextGeneration,
                          diagnostic);
}

bool UHardwareRasterizer::RenderGeometry(const FRenderScene& scene,
                                         const FRenderQuality& quality,
                                         UGPUMeshCache& meshCache,
                                         UHardwareGBuffer& gbuffer,
                                         std::uint64_t contextGeneration,
                                         std::string* diagnostic)
{
    if (contextGeneration == 0 ||
        contextGeneration != ActiveRenderTargetContextGeneration())
    {
        if (diagnostic)
            *diagnostic = "Hardware raster geometry context generation does not match the active context";
        return false;
    }
    if (!program_ || contextGeneration_ != contextGeneration)
    {
        if (!Init(contextGeneration, diagnostic)) return false;
    }
    if (!gbuffer.IsComplete() || gbuffer.ContextGeneration() != contextGeneration)
    {
        if (diagnostic) *diagnostic = "Hardware raster G-buffer is invalid for this context";
        return false;
    }

    const FGeometryState state = CaptureGeometryState();
    if (!gbuffer.BindForGeometry())
    {
        if (diagnostic) *diagnostic = "Hardware raster G-buffer binding failed";
        return false;
    }
    FGeometryStateGuard restore(gbuffer, state);
    for (GLuint drawBuffer = 0; drawBuffer < GeometryDrawBufferCount; ++drawBuffer)
    {
        glDisablei(GL_BLEND, drawBuffer);
        glColorMaski(drawBuffer, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_RASTERIZER_DISCARD);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable(GL_SAMPLE_COVERAGE);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_DITHER);
    glDisable(GL_PRIMITIVE_RESTART);
    glDisable(GL_DEPTH_CLAMP);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthRange(0.0, 1.0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE); // mirrored meshes remain visible; winding may vary in imported assets.
    gbuffer.Clear();
    glUseProgram(program_);
    ++materialTextureFrame_;
    materialSignaturesThisFrame_.clear();
    if (materialTextureFrame_ == 0)
    {
        ClearMaterialTextures();
        materialTextureFrame_ = 1;
    }

    glUniform3fv(glGetUniformLocation(program_, "uEye"), 1,
                 glm::value_ptr(scene.camera.eye));
    const int lightCount = std::min<int>(pointLightLimit_,
        static_cast<int>(scene.pointLights.size()));
    glUniform1i(glGetUniformLocation(program_, "uPointLightCount"), lightCount);
    for (int i = 0; i < lightCount; ++i)
    {
        const std::string positionName = "uPointLightPositions[" + std::to_string(i) + "]";
        const std::string sourceName = "uPointLightSources[" + std::to_string(i) + "]";
        glUniform3fv(glGetUniformLocation(program_, positionName.c_str()), 1,
                     glm::value_ptr(scene.pointLights[static_cast<std::size_t>(i)].worldPosition));
        glUniform3fv(glGetUniformLocation(program_, sourceName.c_str()), 1,
                     glm::value_ptr(scene.pointLights[static_cast<std::size_t>(i)].sourceIntensity));
    }

    ACamera camera;
    camera.eye = scene.camera.eye;
    camera.u = scene.camera.right;
    camera.v = scene.camera.up;
    camera.w = scene.camera.backward;
    camera.l = scene.camera.left;
    camera.r = scene.camera.rightPlane;
    camera.b = scene.camera.bottom;
    camera.t = scene.camera.top;
    camera.d = scene.camera.nearDistance > 0.0f ? scene.camera.nearDistance : 0.1f;
    const glm::mat4 view = FTransform::MakeView(camera);
    const glm::mat4 projection = FTransform::MakeProjFCG(
        camera.l, camera.r, camera.b, camera.t, -camera.d, -1000.0f);
    glUniformMatrix4fv(glGetUniformLocation(program_, "uView"), 1, GL_FALSE,
                       glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(program_, "uProjection"), 1, GL_FALSE,
                       glm::value_ptr(projection));

    try
    {
        for (const FRenderMeshInstance& instance : scene.meshes)
        {
            if (!instance.mesh || !ValidMesh(*instance.mesh)) continue;
            const FGPUMeshResource& resource = meshCache.Acquire(
                *instance.mesh, contextGeneration);
            if (!resource.vao || resource.indexCount == 0) continue;

            glBindVertexArray(resource.vao);
            glUniformMatrix4fv(glGetUniformLocation(program_, "uModel"), 1, GL_FALSE,
                               glm::value_ptr(instance.modelTransform));
            glUniformMatrix3fv(glGetUniformLocation(program_, "uNormalTransform"), 1,
                               GL_FALSE, glm::value_ptr(instance.normalTransform));
            glUniform1i(glGetUniformLocation(program_, "uShadingModel"),
                        static_cast<int>(instance.shadingModel));
            glUniform1ui(glGetUniformLocation(program_, "uObjectIdentity"),
                         instance.objectIdentity);

            const std::size_t triangleCount = resource.indexCount / 3;
            std::size_t firstTriangle = 0;
            while (firstTriangle < triangleCount)
            {
                const bool hasMapping = instance.triangleMaterialSlots &&
                    instance.triangleMaterialSlotCount >= triangleCount;
                const std::uint32_t slot = hasMapping
                    ? (*instance.triangleMaterialSlots)[firstTriangle] : 0u;
                std::size_t endTriangle = firstTriangle + 1;
                if (hasMapping)
                    while (endTriangle < triangleCount &&
                           (*instance.triangleMaterialSlots)[endTriangle] == slot)
                        ++endTriangle;
                else
                    endTriangle = triangleCount;

                std::uint32_t materialIdentity = 0;
                const FResolvedRenderMaterial& material =
                    MaterialFor(instance, slot, materialIdentity);
                unsigned texture = 0;
                std::string textureDiagnostic;
                if (!ResolveMaterialTexture(material.source, contextGeneration,
                                            quality.anisotropy,
                                            texture, textureDiagnostic))
                {
                    ReleaseUnusedMaterialTextures();
                    if (diagnostic) *diagnostic = textureDiagnostic;
                    return false;
                }
                UploadMaterial(program_, instance, material, materialIdentity, texture);
                const std::size_t firstIndex = firstTriangle * 3;
                const std::size_t indexCount = (endTriangle - firstTriangle) * 3;
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount),
                               GL_UNSIGNED_INT,
                               reinterpret_cast<const void*>(firstIndex * sizeof(std::uint32_t)));
                ++indexedDrawCalls_;
                firstTriangle = endTriangle;
            }
        }
    }
    catch (const std::exception& error)
    {
        if (diagnostic) *diagnostic = std::string("Hardware raster geometry failed: ") + error.what();
        return false;
    }
    ReleaseUnusedMaterialTextures();

    const GLenum glError = glGetError();
    if (glError != GL_NO_ERROR)
    {
        if (diagnostic)
        {
            std::ostringstream stream;
            stream << "Hardware raster geometry produced OpenGL error 0x" << std::hex << glError;
            *diagnostic = stream.str();
        }
        return false;
    }
    if (diagnostic) diagnostic->clear();
    return true;
}
