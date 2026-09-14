#pragma once

#include "IRayTracingBackend.h"
#include "FRaySceneCache.h"

#include <cstdint>
#include <memory>
#include <string>

class UGL33RayTracingBackend final : public IRayTracingBackend
{
public:
    UGL33RayTracingBackend() = default;
    ~UGL33RayTracingBackend() noexcept override;
    UGL33RayTracingBackend(const UGL33RayTracingBackend&) = delete;
    UGL33RayTracingBackend& operator=(const UGL33RayTracingBackend&) = delete;

    bool Init(std::uint64_t contextGeneration,
              std::string* diagnostic = nullptr) override;
    bool RenderEffects(const FRayEffectInputs& inputs,
                       FRayEffectOutputs& outputs,
                       std::string* diagnostic = nullptr) override;
    void Shutdown() noexcept override;
    const FRayTracingBackendStats& Stats() const override { return stats_; }

    std::uint64_t ContextGeneration() const { return contextGeneration_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    std::size_t OwnedOutputTextureCount() const;
    void InjectNextUploadFailureForTesting()
    { failNextUploadForTesting_ = true; }

private:
    bool ResizeOutputs(int width, int height, unsigned mask,
                       std::uint64_t contextGeneration,
                       std::string* diagnostic);
    bool UploadScene(const FPackedRayScene& packed, std::string* diagnostic);
    void DeleteCurrentResources() noexcept;
    void ForgetCurrentResources() noexcept;

    FRaySceneCache sceneCache_;
    unsigned program_ = 0;
    unsigned fullscreenVAO_ = 0;
    unsigned framebuffer_ = 0;
    unsigned shadowTexture_ = 0;
    unsigned giTexture_ = 0;
    unsigned reflectionTexture_ = 0;
    unsigned triangleBuffer_ = 0, triangleTexture_ = 0;
    unsigned blasNodeBuffer_ = 0, blasNodeTexture_ = 0;
    unsigned blasIndexBuffer_ = 0, blasIndexTexture_ = 0;
    unsigned instanceBuffer_ = 0, instanceTexture_ = 0;
    unsigned instanceIdentityBuffer_ = 0, instanceIdentityTexture_ = 0;
    unsigned tlasNodeBuffer_ = 0, tlasNodeTexture_ = 0;
    unsigned tlasIndexBuffer_ = 0, tlasIndexTexture_ = 0;
    unsigned materialBuffer_ = 0, materialTexture_ = 0;
    unsigned materialAtlasTexture_ = 0;
    int width_ = 0;
    int height_ = 0;
    unsigned outputMask_ = 0;
    int maxTextureBufferTexels_ = 0;
    int maxLights_ = 0;
    std::uint64_t contextGeneration_ = 0;
    std::uint64_t uploadedBLASRevision_ = 0;
    std::uint64_t uploadedInstanceRevision_ = 0;
    std::uint64_t uploadedMaterialRevision_ = 0;
    FRayTracingBackendStats stats_;
    bool failNextUploadForTesting_ = false;
};

class FOpenGLRayTracingBackendFactory final : public IRayTracingBackendFactory
{
public:
    std::unique_ptr<IRayTracingBackend> Create(
        ERayTracingBackend backend, std::string* diagnostic = nullptr) override;
};

class FStderrRayEffectsWarningSink final : public IRayEffectsWarningSink
{
public:
    void Warn(const std::string& warning) override;
};
