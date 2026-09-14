#pragma once

#include "FGL43ComputeApi.h"
#include "IRayTracingBackend.h"
#include "FRaySceneCache.h"

#include <cstdint>
#include <memory>
#include <string>

class UGL43RayTracingBackend final : public IRayTracingBackend
{
public:
    explicit UGL43RayTracingBackend(
        std::shared_ptr<const IGL43ProcAddressSource> procSource = {});
    ~UGL43RayTracingBackend() noexcept override;
    UGL43RayTracingBackend(const UGL43RayTracingBackend&) = delete;
    UGL43RayTracingBackend& operator=(const UGL43RayTracingBackend&) = delete;

    bool Init(std::uint64_t contextGeneration,
              std::string* diagnostic = nullptr) override;
    bool RenderEffects(const FRayEffectInputs& inputs,
                       FRayEffectOutputs& outputs,
                       std::string* diagnostic = nullptr) override;
    void Shutdown() noexcept override;
    const FRayTracingBackendStats& Stats() const override { return stats_; }
    const std::vector<std::string>& SceneWarnings() const override { return sceneWarnings_; }

    std::uint64_t ContextGeneration() const { return contextGeneration_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    std::size_t OwnedOutputTextureCount() const;
    unsigned MaterialAtlasTextureForTesting() const
    { return materialAtlasTexture_; }
    void InjectNextUploadFailureForTesting() { failNextUploadForTesting_ = true; }
    void InjectNextInitializationFailureForTesting()
    { failNextInitializationForTesting_ = true; }

private:
    bool ResizeOutputs(int width, int height, unsigned mask,
                       std::uint64_t contextGeneration,
                       std::string* diagnostic);
    bool UploadScene(const FPackedRayScene& packed, float requestedAnisotropy,
                     std::string* diagnostic);
    void DeleteCurrentResources() noexcept;
    void ForgetCurrentResources() noexcept;

    std::shared_ptr<const IGL43ProcAddressSource> procSource_;
    FGL43ComputeApi api_;
    FRaySceneCache sceneCache_;
    unsigned program_ = 0;
    unsigned shadowedDirectTexture_ = 0;
    unsigned giTexture_ = 0;
    unsigned reflectionTexture_ = 0;
    unsigned triangleBuffer_ = 0;
    unsigned blasNodeBuffer_ = 0;
    unsigned blasIndexBuffer_ = 0;
    unsigned instanceBuffer_ = 0;
    unsigned tlasNodeBuffer_ = 0;
    unsigned tlasIndexBuffer_ = 0;
    unsigned instanceIdentityBuffer_ = 0;
    unsigned materialBuffer_ = 0;
    unsigned materialAtlasTexture_ = 0;
    int width_ = 0;
    int height_ = 0;
    unsigned outputMask_ = 0;
    std::int64_t maxShaderStorageBlockSize_ = 0;
    int maxWorkGroupCountX_ = 0;
    int maxWorkGroupCountY_ = 0;
    int maxLights_ = 0;
    std::uint64_t contextGeneration_ = 0;
    std::uint64_t uploadedBLASRevision_ = 0;
    std::uint64_t uploadedInstanceRevision_ = 0;
    std::uint64_t uploadedMaterialRevision_ = 0;
    FRayTracingBackendStats stats_;
    std::vector<std::string> sceneWarnings_;
    bool failNextUploadForTesting_ = false;
    bool failNextInitializationForTesting_ = false;
};
