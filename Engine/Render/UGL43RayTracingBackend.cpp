#include "UGL43RayTracingBackend.h"

#include "FRenderScene.h"
#include "FRenderTarget.h"
#include "Shaders/RayEffectsComputeShaders.h"
#include "UHardwareGBuffer.h"

#define GLFW_DLL
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <limits>
#include <sstream>
#include <utility>

namespace
{
constexpr unsigned ShadowBit = 1u;
constexpr unsigned GIBit = 2u;
constexpr unsigned ReflectionBit = 4u;
constexpr int UsedTextureUnits = 8;
constexpr int UsedSSBOBindings = 8;
constexpr int UsedImageUnits = 3;
constexpr int MaxShaderLights = 16;
constexpr GLuint LocalSizeX = 8;
constexpr GLuint LocalSizeY = 8;

// Missing from the repository's deliberately retained pre-4.3 GLEW header.
constexpr GLenum ComputeShader = 0x91B9;
constexpr GLenum ShaderStorageBuffer = 0x90D2;
constexpr GLenum ShaderStorageBufferBinding = 0x90D3;
constexpr GLenum ShaderStorageBufferStart = 0x90D4;
constexpr GLenum ShaderStorageBufferSize = 0x90D5;
constexpr GLenum MaxComputeShaderStorageBlocks = 0x90DB;
constexpr GLenum MaxCombinedShaderStorageBlocks = 0x90DC;
constexpr GLenum MaxShaderStorageBufferBindings = 0x90DD;
constexpr GLenum MaxShaderStorageBlockSize = 0x90DE;
constexpr GLenum MaxComputeWorkGroupInvocations = 0x90EB;
constexpr GLenum MaxComputeWorkGroupCount = 0x91BE;
constexpr GLenum MaxComputeWorkGroupSize = 0x91BF;
constexpr GLenum MaxComputeTextureImageUnits = 0x91BC;
constexpr GLenum MaxComputeUniformComponents = 0x8263;

class FGLFWProcAddressSource final : public IGL43ProcAddressSource
{
public:
    FGL43GenericProc Resolve(const char* name) const override
    {
        return reinterpret_cast<FGL43GenericProc>(glfwGetProcAddress(name));
    }
};

bool CompileComputeProgram(GLuint& program, std::string& diagnostic)
{
    GLuint shader = glCreateShader(ComputeShader);
    const char* source = RayEffectsComputeShaders::EffectsCompute;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE)
    {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        GLsizei written = 0;
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), &written,
                           log.data());
        log.resize(static_cast<std::size_t>(written));
        diagnostic = "GL43 ray-effects compute shader compile failed: " + log;
        glDeleteShader(shader);
        return false;
    }
    program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE) return true;
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    GLsizei written = 0;
    glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), &written,
                        log.data());
    log.resize(static_cast<std::size_t>(written));
    diagnostic = "GL43 ray-effects compute program link failed: " + log;
    glDeleteProgram(program);
    program = 0;
    return false;
}

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

struct FImageBinding
{
    GLint name = 0;
    GLint level = 0;
    GLint layered = GL_FALSE;
    GLint layer = 0;
    GLint access = GL_READ_ONLY;
    GLint format = GL_R8;
};

struct FState
{
    GLint program = 0;
    GLint activeTexture = GL_TEXTURE0;
    GLint packAlignment = 4;
    GLint unpackAlignment = 4;
    GLint genericSSBO = 0;
    GLint textures2D[UsedTextureUnits] = {};
    GLint textures2DArray[UsedTextureUnits] = {};
    GLint samplers[UsedTextureUnits] = {};
    GLint indexedSSBO[UsedSSBOBindings] = {};
    GLint64 indexedStart[UsedSSBOBindings] = {};
    GLint64 indexedSize[UsedSSBOBindings] = {};
    FImageBinding images[UsedImageUnits];
};

FState CaptureState()
{
    FState state;
    glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &state.activeTexture);
    glGetIntegerv(GL_PACK_ALIGNMENT, &state.packAlignment);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &state.unpackAlignment);
    glGetIntegerv(ShaderStorageBufferBinding, &state.genericSSBO);
    for (int unit = 0; unit < UsedTextureUnits; ++unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.textures2D[unit]);
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,
                      &state.textures2DArray[unit]);
        glGetIntegeri_v(GL_SAMPLER_BINDING, unit, &state.samplers[unit]);
    }
    glActiveTexture(static_cast<GLenum>(state.activeTexture));
    for (int binding = 0; binding < UsedSSBOBindings; ++binding)
    {
        glGetIntegeri_v(ShaderStorageBufferBinding, binding,
                        &state.indexedSSBO[binding]);
        glGetInteger64i_v(ShaderStorageBufferStart, binding,
                          &state.indexedStart[binding]);
        glGetInteger64i_v(ShaderStorageBufferSize, binding,
                          &state.indexedSize[binding]);
    }
    for (int unit = 0; unit < UsedImageUnits; ++unit)
    {
        glGetIntegeri_v(GL_IMAGE_BINDING_NAME, unit, &state.images[unit].name);
        glGetIntegeri_v(GL_IMAGE_BINDING_LEVEL, unit, &state.images[unit].level);
        glGetIntegeri_v(GL_IMAGE_BINDING_LAYERED, unit,
                        &state.images[unit].layered);
        glGetIntegeri_v(GL_IMAGE_BINDING_LAYER, unit, &state.images[unit].layer);
        glGetIntegeri_v(GL_IMAGE_BINDING_ACCESS, unit, &state.images[unit].access);
        glGetIntegeri_v(GL_IMAGE_BINDING_FORMAT, unit, &state.images[unit].format);
    }
    return state;
}

void RestoreState(const FState& state)
{
    glUseProgram(static_cast<GLuint>(state.program));
    for (int unit = 0; unit < UsedTextureUnits; ++unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(state.textures2D[unit]));
        glBindTexture(GL_TEXTURE_2D_ARRAY,
                      static_cast<GLuint>(state.textures2DArray[unit]));
        glBindSampler(unit, static_cast<GLuint>(state.samplers[unit]));
    }
    glActiveTexture(static_cast<GLenum>(state.activeTexture));
    for (int binding = 0; binding < UsedSSBOBindings; ++binding)
    {
        if (state.indexedSSBO[binding] && state.indexedSize[binding] > 0)
            glBindBufferRange(ShaderStorageBuffer, binding,
                static_cast<GLuint>(state.indexedSSBO[binding]),
                static_cast<GLintptr>(state.indexedStart[binding]),
                static_cast<GLsizeiptr>(state.indexedSize[binding]));
        else
            glBindBufferBase(ShaderStorageBuffer, binding,
                static_cast<GLuint>(state.indexedSSBO[binding]));
    }
    glBindBuffer(ShaderStorageBuffer, static_cast<GLuint>(state.genericSSBO));
    for (int unit = 0; unit < UsedImageUnits; ++unit)
    {
        const FImageBinding& image = state.images[unit];
        glBindImageTexture(unit, static_cast<GLuint>(image.name), image.level,
            static_cast<GLboolean>(image.layered), image.layer,
            static_cast<GLenum>(image.access), static_cast<GLenum>(image.format));
    }
    glPixelStorei(GL_PACK_ALIGNMENT, state.packAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, state.unpackAlignment);
}

class FStateGuard
{
public:
    FStateGuard() : state_(CaptureState()) {}
    ~FStateGuard() { RestoreState(state_); }
private:
    FState state_;
};

template <typename T>
bool ByteSize(const std::vector<T>& values, std::size_t& result)
{
    const std::size_t count = std::max<std::size_t>(values.size(), 1u);
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) return false;
    result = count * sizeof(T);
    return true;
}

template <typename T>
bool UploadSSBO(const std::vector<T>& values, std::int64_t maximumBytes,
                GLuint& buffer, std::string* diagnostic)
{
    std::size_t bytes = 0;
    if (!ByteSize(values, bytes) || bytes > static_cast<std::uint64_t>(maximumBytes) ||
        bytes > static_cast<std::uint64_t>(std::numeric_limits<GLsizeiptr>::max()))
    {
        if (diagnostic)
            *diagnostic = "Ray scene SSBO exceeds GL_MAX_SHADER_STORAGE_BLOCK_SIZE";
        return false;
    }
    const T zero{};
    glGenBuffers(1, &buffer);
    glBindBuffer(ShaderStorageBuffer, buffer);
    glBufferData(ShaderStorageBuffer, static_cast<GLsizeiptr>(bytes),
        values.empty() ? &zero : values.data(), GL_STATIC_DRAW);
    return buffer != 0;
}

void DeleteBuffer(GLuint& buffer)
{
    if (buffer) glDeleteBuffers(1, &buffer);
    buffer = 0;
}
} // namespace

UGL43RayTracingBackend::UGL43RayTracingBackend(
    std::shared_ptr<const IGL43ProcAddressSource> procSource)
    : procSource_(std::move(procSource))
{
    if (!procSource_) procSource_ = std::make_shared<FGLFWProcAddressSource>();
}

UGL43RayTracingBackend::~UGL43RayTracingBackend() noexcept
{
    Shutdown();
}

bool UGL43RayTracingBackend::Init(std::uint64_t contextGeneration,
                                  std::string* diagnostic)
{
    if (contextGeneration == 0 ||
        contextGeneration != ActiveRenderTargetContextGeneration())
    {
        if (diagnostic) *diagnostic = "GL43 ray effects require the active render context";
        return false;
    }
    if (program_ && contextGeneration_ == contextGeneration) return true;
    if (contextGeneration_ && contextGeneration_ != contextGeneration)
        ForgetCurrentResources();

    FGL43ComputeApi candidateApi;
    if (!candidateApi.Load(*procSource_, diagnostic)) return false;
    const struct
    {
        const char* name;
        bool available;
    } requiredGLEW[] = {
        {"glMemoryBarrier", glMemoryBarrier != nullptr},
        {"glBindBufferBase", glBindBufferBase != nullptr},
        {"glBindBufferRange", glBindBufferRange != nullptr},
        {"glBindImageTexture", glBindImageTexture != nullptr},
        {"glGetIntegeri_v", glGetIntegeri_v != nullptr},
        {"glGetInteger64i_v", glGetInteger64i_v != nullptr},
    };
    for (const auto& entryPoint : requiredGLEW)
    {
        if (entryPoint.available) continue;
        if (diagnostic)
            *diagnostic = std::string("OpenGL 4.3 Compute entry point is missing: ") +
                entryPoint.name;
        return false;
    }
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < 4 || (major == 4 && minor < 3))
    {
        if (diagnostic) *diagnostic = "GL43 ray effects require a live OpenGL 4.3 context";
        return false;
    }

    FStateGuard restore;
    GLint textureUnits = 0, computeTextureUnits = 0, imageUnits = 0;
    GLint ssboBindings = 0, computeSSBOBlocks = 0, combinedSSBOBlocks = 0;
    GLint uniformComponents = 0, invocations = 0;
    GLint workGroupSizeX = 0, workGroupSizeY = 0;
    glGetIntegeri_v(MaxComputeWorkGroupCount, 0, &maxWorkGroupCountX_);
    glGetIntegeri_v(MaxComputeWorkGroupCount, 1, &maxWorkGroupCountY_);
    glGetIntegeri_v(MaxComputeWorkGroupSize, 0, &workGroupSizeX);
    glGetIntegeri_v(MaxComputeWorkGroupSize, 1, &workGroupSizeY);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &textureUnits);
    glGetIntegerv(MaxComputeTextureImageUnits, &computeTextureUnits);
    glGetIntegerv(GL_MAX_IMAGE_UNITS, &imageUnits);
    glGetIntegerv(MaxShaderStorageBufferBindings, &ssboBindings);
    glGetIntegerv(MaxComputeShaderStorageBlocks, &computeSSBOBlocks);
    glGetIntegerv(MaxCombinedShaderStorageBlocks, &combinedSSBOBlocks);
    glGetIntegerv(MaxComputeUniformComponents, &uniformComponents);
    glGetIntegerv(MaxComputeWorkGroupInvocations, &invocations);
    GLint64 maxBlock = 0;
    glGetInteger64v(MaxShaderStorageBlockSize, &maxBlock);
    maxShaderStorageBlockSize_ = static_cast<std::int64_t>(maxBlock);
    if (textureUnits < UsedTextureUnits || computeTextureUnits < UsedTextureUnits ||
        imageUnits < UsedImageUnits || ssboBindings < UsedSSBOBindings ||
        computeSSBOBlocks < UsedSSBOBindings ||
        combinedSSBOBlocks < UsedSSBOBindings || uniformComponents < 192 ||
        invocations < static_cast<int>(LocalSizeX * LocalSizeY) ||
        workGroupSizeX < static_cast<int>(LocalSizeX) ||
        workGroupSizeY < static_cast<int>(LocalSizeY) ||
        maxWorkGroupCountX_ <= 0 || maxWorkGroupCountY_ <= 0 || maxBlock <= 0)
    {
        if (diagnostic)
            *diagnostic = "GL43 ray effects need 8 SSBO bindings, 8 compute textures, "
                "3 image units, 192 uniforms, and 8x8 compute work groups";
        return false;
    }

    std::string error;
    GLuint candidateProgram = 0;
    if (!CompileComputeProgram(candidateProgram, error))
    {
        if (diagnostic) *diagnostic = error;
        return false;
    }
    if (failNextInitializationForTesting_)
    {
        failNextInitializationForTesting_ = false;
        glDeleteProgram(candidateProgram);
        if (diagnostic) *diagnostic = "Injected GL43 ray-effects initialization failure";
        return false;
    }
    if (!CollectError("GL43 ray-effects initialization", &error))
    {
        glDeleteProgram(candidateProgram);
        if (diagnostic) *diagnostic = error;
        return false;
    }

    api_ = candidateApi;
    program_ = candidateProgram;
    contextGeneration_ = contextGeneration;
    maxLights_ = MaxShaderLights;
    ++stats_.resourceAllocations;
    glUseProgram(program_);
    const char* names[] = {"uPositionCoverage", "uGeometricNormal",
        "uShadingNormalModel", "uAlbedoShininess", "uSpecularMirror",
        "uIdentity", "uSky", "uMaterialAtlas"};
    for (int unit = 0; unit < UsedTextureUnits; ++unit)
        glUniform1i(glGetUniformLocation(program_, names[unit]), unit);
    if (!CollectError("GL43 ray-effects sampler setup", diagnostic))
    {
        glDeleteProgram(program_);
        program_ = 0;
        api_.Reset();
        contextGeneration_ = 0;
        return false;
    }
    if (diagnostic) diagnostic->clear();
    return true;
}

std::size_t UGL43RayTracingBackend::OwnedOutputTextureCount() const
{
    return (shadowTexture_ ? 1u : 0u) + (giTexture_ ? 1u : 0u) +
        (reflectionTexture_ ? 1u : 0u);
}

bool UGL43RayTracingBackend::ResizeOutputs(int width, int height, unsigned mask,
                                           std::uint64_t contextGeneration,
                                           std::string* diagnostic)
{
    if (width_ == width && height_ == height && outputMask_ == mask &&
        contextGeneration_ == contextGeneration && OwnedOutputTextureCount())
        return true;
    GLint maxTextureSize = 0, maxImageUnits = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    glGetIntegerv(GL_MAX_IMAGE_UNITS, &maxImageUnits);
    if (width <= 0 || height <= 0 || width > maxTextureSize ||
        height > maxTextureSize || maxImageUnits < UsedImageUnits)
    {
        if (diagnostic)
            *diagnostic = "GL43 ray-effects output exceeds texture/image-unit limits";
        return false;
    }
    GLuint candidateShadow = 0, candidateGI = 0, candidateReflection = 0;
    auto makeTexture = [&](GLuint& texture, GLenum internalFormat,
                           GLenum format)
    {
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
                     format, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    if (mask & ShadowBit) makeTexture(candidateShadow, GL_R16F, GL_RED);
    if (mask & GIBit) makeTexture(candidateGI, GL_RGBA16F, GL_RGBA);
    if (mask & ReflectionBit)
        makeTexture(candidateReflection, GL_RGBA16F, GL_RGBA);
    const bool okay = (!((mask & ShadowBit) && !candidateShadow)) &&
        (!((mask & GIBit) && !candidateGI)) &&
        (!((mask & ReflectionBit) && !candidateReflection)) &&
        CollectError("GL43 ray-effects output creation", diagnostic);
    if (!okay)
    {
        if (candidateShadow) glDeleteTextures(1, &candidateShadow);
        if (candidateGI) glDeleteTextures(1, &candidateGI);
        if (candidateReflection) glDeleteTextures(1, &candidateReflection);
        return false;
    }
    if (shadowTexture_) glDeleteTextures(1, &shadowTexture_);
    if (giTexture_) glDeleteTextures(1, &giTexture_);
    if (reflectionTexture_) glDeleteTextures(1, &reflectionTexture_);
    shadowTexture_ = candidateShadow;
    giTexture_ = candidateGI;
    reflectionTexture_ = candidateReflection;
    width_ = width;
    height_ = height;
    outputMask_ = mask;
    ++stats_.outputAllocations;
    stats_.resourceAllocations += OwnedOutputTextureCount();
    stats_.ownedOutputTextures = OwnedOutputTextureCount();
    return true;
}

bool UGL43RayTracingBackend::UploadScene(const FPackedRayScene& packed,
                                         std::string* diagnostic)
{
    if (packed.maximumBLASDepth > 60 || packed.tlasDepth > 28)
    {
        if (diagnostic)
            *diagnostic = "Ray scene BVH exceeds bounded GL4.3 traversal stacks; "
                "split the mesh or scene";
        return false;
    }
    GLint maxTextureSize = 0, maxArrayLayers = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxArrayLayers);
    if (packed.textureWidth > maxTextureSize || packed.textureHeight > maxTextureSize ||
        packed.textureLayerCount > maxArrayLayers)
    {
        if (diagnostic)
            *diagnostic = "Ray material atlas exceeds GL4.3 texture size/layer limits";
        return false;
    }
    const bool uploadBLAS = packed.blasRevision != uploadedBLASRevision_;
    const bool uploadInstances = packed.instanceRevision != uploadedInstanceRevision_;
    const bool uploadMaterials = packed.materialRevision != uploadedMaterialRevision_;
    if (!uploadBLAS && !uploadInstances && !uploadMaterials) return true;

    GLuint triangle = 0, blasNode = 0, blasIndex = 0;
    GLuint instance = 0, tlasNode = 0, tlasIndex = 0, identity = 0;
    GLuint material = 0, atlas = 0;
    bool okay = true;
    if (uploadBLAS)
        okay = UploadSSBO(packed.triangleTexels, maxShaderStorageBlockSize_,
                           triangle, diagnostic) &&
            UploadSSBO(packed.blasNodeTexels, maxShaderStorageBlockSize_,
                       blasNode, diagnostic) &&
            UploadSSBO(packed.blasTriangleIndices, maxShaderStorageBlockSize_,
                       blasIndex, diagnostic);
    if (okay && uploadInstances)
        okay = UploadSSBO(packed.instanceTexels, maxShaderStorageBlockSize_,
                           instance, diagnostic) &&
            UploadSSBO(packed.tlasNodeTexels, maxShaderStorageBlockSize_,
                       tlasNode, diagnostic) &&
            UploadSSBO(packed.tlasInstanceIndices, maxShaderStorageBlockSize_,
                       tlasIndex, diagnostic) &&
            UploadSSBO(packed.instanceIdentityTexels, maxShaderStorageBlockSize_,
                       identity, diagnostic);
    if (okay && uploadMaterials)
    {
        okay = UploadSSBO(packed.materialTexels, maxShaderStorageBlockSize_,
                           material, diagnostic);
        if (okay)
        {
            const unsigned char white[4] = {255, 255, 255, 255};
            const int atlasWidth = std::max(1, packed.textureWidth);
            const int atlasHeight = std::max(1, packed.textureHeight);
            const int layers = std::max(1, packed.textureLayerCount);
            glGenTextures(1, &atlas);
            glBindTexture(GL_TEXTURE_2D_ARRAY, atlas);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, atlasWidth,
                atlasHeight, layers, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                packed.textureArrayRGBA.empty() ? white :
                    packed.textureArrayRGBA.data());
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            okay = atlas != 0;
        }
    }
    glBindBuffer(ShaderStorageBuffer, 0);
    if (failNextUploadForTesting_)
    {
        failNextUploadForTesting_ = false;
        okay = false;
        if (diagnostic) *diagnostic = "Injected GL43 ray-scene upload failure";
    }
    if (!CollectError("GL43 ray-scene upload", diagnostic)) okay = false;
    if (!okay)
    {
        DeleteBuffer(triangle); DeleteBuffer(blasNode); DeleteBuffer(blasIndex);
        DeleteBuffer(instance); DeleteBuffer(tlasNode); DeleteBuffer(tlasIndex);
        DeleteBuffer(identity); DeleteBuffer(material);
        if (atlas) glDeleteTextures(1, &atlas);
        return false;
    }
    if (uploadBLAS)
    {
        DeleteBuffer(triangleBuffer_); DeleteBuffer(blasNodeBuffer_);
        DeleteBuffer(blasIndexBuffer_);
        triangleBuffer_ = triangle; blasNodeBuffer_ = blasNode;
        blasIndexBuffer_ = blasIndex;
        ++stats_.blasUploads;
        stats_.resourceAllocations += 3;
        uploadedBLASRevision_ = packed.blasRevision;
    }
    if (uploadInstances)
    {
        DeleteBuffer(instanceBuffer_); DeleteBuffer(tlasNodeBuffer_);
        DeleteBuffer(tlasIndexBuffer_); DeleteBuffer(instanceIdentityBuffer_);
        instanceBuffer_ = instance; tlasNodeBuffer_ = tlasNode;
        tlasIndexBuffer_ = tlasIndex; instanceIdentityBuffer_ = identity;
        ++stats_.instanceUploads;
        stats_.resourceAllocations += 4;
        uploadedInstanceRevision_ = packed.instanceRevision;
    }
    if (uploadMaterials)
    {
        DeleteBuffer(materialBuffer_);
        if (materialAtlasTexture_) glDeleteTextures(1, &materialAtlasTexture_);
        materialBuffer_ = material;
        materialAtlasTexture_ = atlas;
        ++stats_.materialUploads;
        stats_.resourceAllocations += 2;
        uploadedMaterialRevision_ = packed.materialRevision;
    }
    ++stats_.sceneUploads;
    return true;
}

bool UGL43RayTracingBackend::RenderEffects(const FRayEffectInputs& inputs,
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
        if (diagnostic) *diagnostic = "GL43 ray effects rejected invalid immutable inputs";
        return false;
    }
    if (!Init(inputs.contextGeneration, diagnostic)) return false;
    FStateGuard restore;
    glActiveTexture(GL_TEXTURE0);
    const FPackedRayScene& packed = sceneCache_.Prepare(*inputs.scene);
    stats_.residentBLAS = sceneCache_.Stats().residentBLAS;
    if (!packed.valid)
    {
        if (diagnostic) *diagnostic = packed.diagnostic;
        return false;
    }
    if (!ResizeOutputs(inputs.width, inputs.height, mask,
                       inputs.contextGeneration, diagnostic) ||
        !UploadScene(packed, diagnostic)) return false;

    const std::uint64_t groupsX =
        (static_cast<std::uint64_t>(inputs.width) + LocalSizeX - 1u) / LocalSizeX;
    const std::uint64_t groupsY =
        (static_cast<std::uint64_t>(inputs.height) + LocalSizeY - 1u) / LocalSizeY;
    if (groupsX == 0 || groupsY == 0 ||
        groupsX > static_cast<std::uint64_t>(maxWorkGroupCountX_) ||
        groupsY > static_cast<std::uint64_t>(maxWorkGroupCountY_) ||
        groupsX > std::numeric_limits<GLuint>::max() ||
        groupsY > std::numeric_limits<GLuint>::max())
    {
        if (diagnostic) *diagnostic = "GL43 ray-effects dispatch exceeds work-group limits";
        return false;
    }

    glUseProgram(program_);
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
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D_ARRAY, materialAtlasTexture_);
    glBindSampler(7, 0);

    const GLuint buffers[UsedSSBOBindings] = {triangleBuffer_, blasNodeBuffer_,
        blasIndexBuffer_, instanceBuffer_, tlasNodeBuffer_, tlasIndexBuffer_,
        instanceIdentityBuffer_, materialBuffer_};
    for (int binding = 0; binding < UsedSSBOBindings; ++binding)
        glBindBufferBase(ShaderStorageBuffer, binding, buffers[binding]);
    glBindImageTexture(0, shadowTexture_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F);
    glBindImageTexture(1, giTexture_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glBindImageTexture(2, reflectionTexture_, 0, GL_FALSE, 0,
                       GL_WRITE_ONLY, GL_RGBA16F);

    glUniform2i(glGetUniformLocation(program_, "uOutputSize"), width_, height_);
    glUniform1i(glGetUniformLocation(program_, "uDoShadows"),
                mask & ShadowBit ? 1 : 0);
    glUniform1i(glGetUniformLocation(program_, "uDoGI"), mask & GIBit ? 1 : 0);
    glUniform1i(glGetUniformLocation(program_, "uDoReflections"),
                mask & ReflectionBit ? 1 : 0);
    glUniform1i(glGetUniformLocation(program_, "uHasSky"),
                inputs.environmentTexture ? 1 : 0);
    glUniform1i(glGetUniformLocation(program_, "uInstanceCount"),
                packed.instanceCount);
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
    glUniform1f(glGetUniformLocation(program_, "uGIStrength"),
                inputs.quality.giStrength);
    glUniform1f(glGetUniformLocation(program_, "uReflectionStrength"),
                inputs.quality.reflStrength);
    glUniform1i(glGetUniformLocation(program_, "uShadowSamples"),
                std::clamp(inputs.quality.shadowSamples, 1, 16));
    glUniform1f(glGetUniformLocation(program_, "uShadowSoftness"),
                std::max(inputs.quality.shadowSoftness, 0.0f));
    const int lightCount = std::min<int>(maxLights_,
                                         inputs.scene->pointLights.size());
    glUniform1i(glGetUniformLocation(program_, "uLightCount"), lightCount);
    for (int i = 0; i < lightCount; ++i)
    {
        const std::string positionName =
            "uLightPositions[" + std::to_string(i) + "]";
        glUniform3fv(glGetUniformLocation(program_, positionName.c_str()), 1,
            glm::value_ptr(inputs.scene->pointLights[static_cast<std::size_t>(i)]
                               .worldPosition));
        const std::string radianceName =
            "uLightRadiances[" + std::to_string(i) + "]";
        glUniform3fv(glGetUniformLocation(program_, radianceName.c_str()), 1,
            glm::value_ptr(inputs.scene->pointLights[static_cast<std::size_t>(i)]
                               .radiance));
    }
    if (!CollectError("GL43 ray-effects pre-dispatch setup", diagnostic))
        return false;
    api_.DispatchCompute(static_cast<GLuint>(groupsX),
                         static_cast<GLuint>(groupsY), 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_TEXTURE_FETCH_BARRIER_BIT);
    if (!CollectError("GL43 ray-effects dispatch", diagnostic)) return false;
    ++stats_.renderCalls;
    ++stats_.rayDraws;
    ++stats_.rayDispatches;
    ++stats_.memoryBarriers;
    if (shadowTexture_) outputs.shadowVisibilityTarget = FRenderOutputView{
        shadowTexture_, width_, height_, true};
    if (giTexture_) outputs.globalIlluminationTarget = FRenderOutputView{
        giTexture_, width_, height_, true};
    if (reflectionTexture_) outputs.reflectionTarget = FRenderOutputView{
        reflectionTexture_, width_, height_, true};
    if (diagnostic) diagnostic->clear();
    return true;
}

void UGL43RayTracingBackend::Shutdown() noexcept
{
    if (contextGeneration_ &&
        contextGeneration_ == ActiveRenderTargetContextGeneration())
        DeleteCurrentResources();
    else
        ForgetCurrentResources();
}

void UGL43RayTracingBackend::DeleteCurrentResources() noexcept
{
    const std::size_t before = (program_ ? 1u : 0u) +
        OwnedOutputTextureCount() + (triangleBuffer_ ? 1u : 0u) +
        (blasNodeBuffer_ ? 1u : 0u) + (blasIndexBuffer_ ? 1u : 0u) +
        (instanceBuffer_ ? 1u : 0u) + (tlasNodeBuffer_ ? 1u : 0u) +
        (tlasIndexBuffer_ ? 1u : 0u) +
        (instanceIdentityBuffer_ ? 1u : 0u) +
        (materialBuffer_ ? 1u : 0u) + (materialAtlasTexture_ ? 1u : 0u);
    if (shadowTexture_) glDeleteTextures(1, &shadowTexture_);
    if (giTexture_) glDeleteTextures(1, &giTexture_);
    if (reflectionTexture_) glDeleteTextures(1, &reflectionTexture_);
    DeleteBuffer(triangleBuffer_); DeleteBuffer(blasNodeBuffer_);
    DeleteBuffer(blasIndexBuffer_); DeleteBuffer(instanceBuffer_);
    DeleteBuffer(tlasNodeBuffer_); DeleteBuffer(tlasIndexBuffer_);
    DeleteBuffer(instanceIdentityBuffer_); DeleteBuffer(materialBuffer_);
    if (materialAtlasTexture_) glDeleteTextures(1, &materialAtlasTexture_);
    if (program_) glDeleteProgram(program_);
    stats_.releasedResources += before;
    ForgetCurrentResources();
}

void UGL43RayTracingBackend::ForgetCurrentResources() noexcept
{
    const std::size_t abandoned = (program_ ? 1u : 0u) +
        OwnedOutputTextureCount() + (triangleBuffer_ ? 1u : 0u) +
        (blasNodeBuffer_ ? 1u : 0u) + (blasIndexBuffer_ ? 1u : 0u) +
        (instanceBuffer_ ? 1u : 0u) + (tlasNodeBuffer_ ? 1u : 0u) +
        (tlasIndexBuffer_ ? 1u : 0u) +
        (instanceIdentityBuffer_ ? 1u : 0u) +
        (materialBuffer_ ? 1u : 0u) + (materialAtlasTexture_ ? 1u : 0u);
    if (contextGeneration_ &&
        contextGeneration_ != ActiveRenderTargetContextGeneration())
        stats_.abandonedResources += abandoned;
    program_ = 0;
    shadowTexture_ = giTexture_ = reflectionTexture_ = 0;
    triangleBuffer_ = blasNodeBuffer_ = blasIndexBuffer_ = 0;
    instanceBuffer_ = tlasNodeBuffer_ = tlasIndexBuffer_ = 0;
    instanceIdentityBuffer_ = materialBuffer_ = materialAtlasTexture_ = 0;
    width_ = height_ = 0;
    outputMask_ = 0;
    maxShaderStorageBlockSize_ = 0;
    maxWorkGroupCountX_ = maxWorkGroupCountY_ = 0;
    maxLights_ = 0;
    contextGeneration_ = 0;
    uploadedBLASRevision_ = uploadedInstanceRevision_ = 0;
    uploadedMaterialRevision_ = 0;
    stats_.ownedOutputTextures = 0;
    api_.Reset();
    sceneCache_.Clear();
}
