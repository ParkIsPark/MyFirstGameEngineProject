#include "FRenderHistory.h"

#include "FRenderQuality.h"
#include "FRenderScene.h"
#include "UMesh.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

namespace
{
class FExactHasher
{
public:
    template <typename T> void Scalar(const T& value)
    {
        const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
        for (std::size_t i = 0; i < sizeof(T); ++i)
        {
            hash_ ^= bytes[i];
            hash_ *= 1099511628211ull;
        }
    }
    void Float(float value)
    {
        std::uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value), "float must be binary32");
        std::memcpy(&bits, &value, sizeof(bits));
        Scalar(bits);
    }
    void Vector(const glm::vec2& value) { Float(value.x); Float(value.y); }
    void Vector(const glm::vec3& value)
    { Float(value.x); Float(value.y); Float(value.z); }
    void Matrix(const glm::mat3& value)
    { for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r) Float(value[c][r]); }
    void Matrix(const glm::mat4& value)
    { for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) Float(value[c][r]); }
    void String(const std::string& value)
    {
        Scalar(value.size());
        for (unsigned char byte : value) Scalar(byte);
    }
    std::uint64_t Value() const { return hash_; }
private:
    std::uint64_t hash_ = 14695981039346656037ull;
};

std::uint64_t FileStamp(const std::string& path)
{
    if (path.empty()) return 0;
    std::error_code error;
    const auto stamp = std::filesystem::last_write_time(path, error);
    return error ? 0 : static_cast<std::uint64_t>(stamp.time_since_epoch().count());
}

void HashMaterial(FExactHasher& hash, const FResolvedRenderMaterial& material)
{
    hash.Vector(material.ambient); hash.Vector(material.albedo);
    hash.Vector(material.specularColor); hash.Vector(material.emissive);
    hash.Float(material.shininess); hash.Float(material.mirrorFactor);
    hash.Scalar(material.blendMode); hash.Float(material.opacity);
    hash.Float(material.refraction); hash.Vector(material.transmittanceColor);
    hash.Float(material.transmittanceDistance);
    hash.Scalar(material.castRayTracedShadows);
    hash.Scalar(material.runtimeRevision);
    hash.String(material.diffuseTexturePath);
    hash.Scalar(FileStamp(material.diffuseTexturePath));
}
}

std::uint64_t BuildRenderHistorySignature(
    const FRenderScene& scene, const FRenderFeatures& features,
    const FRenderQuality& quality, int internalWidth, int internalHeight,
    ERayTracingBackend backend, std::uint64_t contextGeneration,
    std::uint64_t environmentRevision)
{
    FExactHasher hash;
    hash.Scalar(internalWidth); hash.Scalar(internalHeight);
    hash.Scalar(backend); hash.Scalar(contextGeneration);
    hash.Scalar(environmentRevision);
    hash.Vector(scene.camera.eye); hash.Vector(scene.camera.right);
    hash.Vector(scene.camera.up); hash.Vector(scene.camera.backward);
    hash.Float(scene.camera.left); hash.Float(scene.camera.rightPlane);
    hash.Float(scene.camera.bottom); hash.Float(scene.camera.top);
    hash.Float(scene.camera.nearDistance); hash.Float(scene.camera.fovDegrees);
    hash.Scalar(scene.shadingModel);
    hash.Scalar(scene.meshes.size());
    for (const auto& instance : scene.meshes)
    {
        const std::uint64_t asset = instance.mesh ? instance.mesh->AssetId() : 0;
        const std::uint64_t revision = instance.mesh ? instance.mesh->GeometryRevision() : 0;
        hash.Scalar(asset); hash.Scalar(revision);
        hash.Matrix(instance.modelTransform); hash.Matrix(instance.normalTransform);
        hash.Vector(instance.uvTiling); hash.Scalar(instance.shadingModel);
        hash.Scalar(instance.objectIdentity); hash.Scalar(instance.materialOverrideKind);
        hash.Scalar(instance.materialOverrideIdentity);
        hash.Scalar(instance.materialOverride.has_value());
        if (instance.materialOverride) HashMaterial(hash, *instance.materialOverride);
        hash.Scalar(instance.materialSlots.size());
        for (const auto& material : instance.materialSlots) HashMaterial(hash, material);
        hash.Scalar(instance.materialSlotIdentities.size());
        for (auto identity : instance.materialSlotIdentities) hash.Scalar(identity);
        hash.Scalar(instance.triangleMaterialSlotCount);
    }
    hash.Scalar(scene.materialsByIdentity.size());
    for (const auto& material : scene.materialsByIdentity) HashMaterial(hash, material);
    hash.Scalar(scene.pointLights.size());
    for (const auto& light : scene.pointLights)
    { hash.Vector(light.worldPosition); hash.Vector(light.sourceIntensity); }
    hash.Scalar(scene.usesDefaultPointLight);
    hash.String(scene.environment.skyPath); hash.Scalar(FileStamp(scene.environment.skyPath));
    hash.Vector(scene.environment.tint); hash.Vector(scene.environment.horizon);
    hash.Vector(scene.environment.zenith); hash.Float(scene.environment.exponent);
    hash.Scalar(features.hardwareRaster); hash.Scalar(features.rayTracing);
    hash.Scalar(features.rayTracedShadows); hash.Scalar(features.rayTracedGI);
    hash.Scalar(features.rayTracedReflections); hash.Scalar(features.rayTracedTranslucency);
    hash.Scalar(features.rayTracingBackend);
    hash.Scalar(quality.ssaa); hash.Float(quality.ambientStrength);
    hash.Scalar(quality.giSamples); hash.Scalar(quality.giBounces);
    hash.Float(quality.giStrength); hash.Float(quality.reflStrength);
    hash.Float(quality.shininess); hash.Scalar(quality.shadowSamples);
    hash.Float(quality.shadowSoftness); hash.Float(quality.exposureEV);
    hash.Scalar(quality.temporalFrames); hash.Float(quality.anisotropy);
    hash.Scalar(quality.depthView);
    return hash.Value();
}

FTemporalFrame FTemporalSequence::Begin(std::uint64_t signature,
                                        std::uint32_t frameCap)
{
    frameCap = std::max<std::uint32_t>(1, frameCap);
    const bool reset = !valid_ || signature_ != signature || frameCap_ != frameCap;
    if (reset)
    {
        signature_ = signature;
        frameCap_ = frameCap;
        frameIndex_ = 0;
        accumulatedFrames_ = 1;
        valid_ = true;
    }
    else
    {
        const FTemporalFrame advanced = AdvanceTemporalFrame(
            {signature_, frameIndex_, false, accumulatedFrames_}, frameCap_);
        frameIndex_ = advanced.frameIndex;
        accumulatedFrames_ = advanced.accumulatedFrames;
    }
    return {signature_, frameIndex_, reset, accumulatedFrames_};
}

FTemporalFrame AdvanceTemporalFrame(const FTemporalFrame& current,
                                    std::uint32_t frameCap) noexcept
{
    frameCap = std::max<std::uint32_t>(1, frameCap);
    FTemporalFrame next = current;
    next.frameIndex = current.frameIndex + std::uint32_t{1};
    const std::uint32_t accumulated =
        std::max<std::uint32_t>(1, current.accumulatedFrames);
    next.accumulatedFrames = accumulated < frameCap
        ? accumulated + std::uint32_t{1} : frameCap;
    next.reset = false;
    return next;
}

unsigned TemporalHistorySlot(const FTemporalFrame& frame) noexcept
{
    return frame.frameIndex & 1u;
}

int TemporalShaderFrameIndex(const FTemporalFrame& frame) noexcept
{
    constexpr std::uint32_t ShaderIndexMask = 0x7fffffffu;
    return static_cast<int>(frame.frameIndex & ShaderIndexMask);
}

float TemporalCurrentWeight(const FTemporalFrame& frame,
                            int temporalFrames) noexcept
{
    const std::uint32_t cap = temporalFrames > 0
        ? static_cast<std::uint32_t>(temporalFrames) : 1u;
    const std::uint32_t accumulated =
        std::max<std::uint32_t>(1, frame.accumulatedFrames);
    const std::uint32_t sampleCount = std::min(accumulated, cap);
    return 1.0f / static_cast<float>(sampleCount);
}

void FTemporalSequence::Reset() noexcept
{
    signature_ = 0;
    frameIndex_ = 0;
    accumulatedFrames_ = 1;
    frameCap_ = 0;
    valid_ = false;
}
