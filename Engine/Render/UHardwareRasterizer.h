#pragma once

#include "FGPUMeshResource.h"

#include <cstdint>
#include <string>
#include <unordered_map>

struct FRenderScene;
struct FRenderQuality;
struct Material;
class UGPUMeshCache;
class UHardwareGBuffer;

class FOpenGLMeshUploadAdapter final : public IMeshGPUUploadAdapter
{
public:
    std::uint64_t ActiveContextGeneration() const noexcept override;
    FGPUMeshResource CreateAndUpload(const FMeshGPUUploadView& upload,
                                     std::uint64_t contextGeneration) override;
    bool Reupload(FGPUMeshResource& resource,
                  const FMeshGPUUploadView& upload,
                  std::string& diagnostic) noexcept override;
    void Destroy(FGPUMeshResource& resource) noexcept override;
    void Abandon(FGPUMeshResource& resource) noexcept override;
};
class UHardwareRasterizer
{
public:
    UHardwareRasterizer() = default;
    ~UHardwareRasterizer() noexcept;
    UHardwareRasterizer(const UHardwareRasterizer&) = delete;
    UHardwareRasterizer& operator=(const UHardwareRasterizer&) = delete;

    bool Init(std::uint64_t contextGeneration,
              std::string* diagnostic = nullptr);
    void Shutdown() noexcept;
    bool RenderGeometry(const FRenderScene& scene,
                        UGPUMeshCache& meshCache,
                        UHardwareGBuffer& gbuffer,
                        std::uint64_t contextGeneration,
                        std::string* diagnostic = nullptr);
    bool RenderGeometry(const FRenderScene& scene,
                        const FRenderQuality& quality,
                        UGPUMeshCache& meshCache,
                        UHardwareGBuffer& gbuffer,
                        std::uint64_t contextGeneration,
                        std::string* diagnostic = nullptr);
    bool RenderGeometry(const FRenderScene& scene,
                        const FRenderQuality& quality,
                        UGPUMeshCache& meshCache,
                        UHardwareGBuffer& gbuffer,
                        unsigned environmentTexture,
                        std::uint64_t contextGeneration,
                        std::string* diagnostic = nullptr);

    bool IsReady() const { return program_ != 0; }
    unsigned Program() const { return program_; }
    std::uint64_t ContextGeneration() const { return contextGeneration_; }
    std::uint64_t MaterialTextureUploads() const { return materialTextureUploads_; }
    std::uint64_t MaterialTextureUploadFailures() const { return materialTextureUploadFailures_; }
    std::uint64_t MaterialTextureHashComputations() const { return materialTextureHashComputations_; }
    std::uint64_t IndexedDrawCalls() const { return indexedDrawCalls_; }
    std::size_t OwnedMaterialTextureCount() const { return materialTextures_.size(); }
    unsigned MaterialTextureForTesting(const Material* material) const
    {
        const auto found = materialTextures_.find(material);
        return found == materialTextures_.end() ? 0u : found->second.texture;
    }
    void InjectNextMaterialTextureUploadFailureForTesting() {
        failNextMaterialTextureUploadForTesting_ = true;
    }

private:
    bool ResolveMaterialTexture(const Material* material,
                                std::uint64_t contextGeneration,
                                float requestedAnisotropy,
                                unsigned& texture,
                                std::string& diagnostic);
    void ReleaseUnusedMaterialTextures() noexcept;
    void ClearMaterialTextures() noexcept;
    unsigned program_ = 0;
    std::uint64_t contextGeneration_ = 0;
    struct FMaterialTextureResource
    {
        unsigned texture = 0;
        std::uint64_t signature = 0;
        std::uint64_t lastUsedFrame = 0;
    };
    std::unordered_map<const Material*, FMaterialTextureResource> materialTextures_;
    std::unordered_map<const Material*, std::uint64_t> materialSignaturesThisFrame_;
    std::uint64_t materialTextureFrame_ = 0;
    std::uint64_t materialTextureUploads_ = 0;
    std::uint64_t indexedDrawCalls_ = 0;
    std::uint64_t materialTextureUploadFailures_ = 0;
    std::uint64_t materialTextureHashComputations_ = 0;
    bool failNextMaterialTextureUploadForTesting_ = false;
    int pointLightLimit_ = 0;
};
