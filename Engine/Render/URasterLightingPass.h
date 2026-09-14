#pragma once

#include "FRenderOutputs.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>

struct FRenderQuality;
struct FRenderScene;
class FRenderTarget;
class UHardwareGBuffer;

struct FRasterLightingPassStats
{
    std::uint64_t outputAllocations = 0;
    std::uint64_t lightingPasses = 0;
    std::uint64_t environmentTextureUploads = 0;
    std::uint64_t environmentTextureUploadFailures = 0;
    std::size_t pointLightLimit = 0;
    std::size_t uploadedPointLights = 0;
    std::size_t truncatedPointLights = 0;
    std::uint64_t overflowWarnings = 0;
};

class URasterLightingPass
{
public:
    URasterLightingPass() = default;
    ~URasterLightingPass() noexcept;
    URasterLightingPass(const URasterLightingPass&) = delete;
    URasterLightingPass& operator=(const URasterLightingPass&) = delete;

    bool Init(std::uint64_t contextGeneration, std::string* diagnostic = nullptr);
    bool Resize(int width, int height, std::uint64_t contextGeneration,
                std::string* diagnostic = nullptr);
    bool PrepareEnvironment(const FRenderScene& scene,
                            std::uint64_t contextGeneration,
                            std::string* diagnostic = nullptr);
    bool Render(const FRenderScene& scene, const FRenderQuality& quality,
                const UHardwareGBuffer& gbuffer,
                std::uint64_t contextGeneration,
                FRasterLightingOutput& output,
                std::string* diagnostic = nullptr);
    void Shutdown() noexcept;

    bool IsReady() const { return lightingProgram_ != 0; }
    unsigned EnvironmentAmbientTexture() const { return environmentAmbientTexture_; }
    unsigned UnshadowedDirectTexture() const { return unshadowedDirectTexture_; }
    unsigned Framebuffer() const { return framebuffer_; }
    unsigned EnvironmentTexture() const { return environmentTexture_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    std::uint64_t ContextGeneration() const { return contextGeneration_; }
    std::uint64_t ResourceRevision() const { return resourceRevision_; }
    std::size_t OwnedTextureCount() const {
        return (environmentAmbientTexture_ ? 1u : 0u) +
               (unshadowedDirectTexture_ ? 1u : 0u);
    }
    const FRasterLightingPassStats& Stats() const { return stats_; }
    void InjectNextEnvironmentTextureUploadFailureForTesting() {
        failNextEnvironmentTextureUploadForTesting_ = true;
    }

private:
    bool LoadEnvironmentTexture(const std::string& path,
                                std::uint64_t contextGeneration,
                                std::string* diagnostic);
    void DeleteCurrentResources() noexcept;
    void ForgetCurrentResources() noexcept;

    unsigned lightingProgram_ = 0;
    unsigned fullscreenVAO_ = 0;
    unsigned framebuffer_ = 0;
    unsigned environmentAmbientTexture_ = 0;
    unsigned unshadowedDirectTexture_ = 0;
    unsigned environmentTexture_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::uint64_t contextGeneration_ = 0;
    std::uint64_t resourceRevision_ = 0;
    std::string environmentPath_;
    std::uint64_t environmentStamp_ = 0;
    int pointLightLimit_ = 0;
    FRasterLightingPassStats stats_;
    std::unordered_set<std::uint64_t> overflowWarnings_;
    bool failNextEnvironmentTextureUploadForTesting_ = false;
};
