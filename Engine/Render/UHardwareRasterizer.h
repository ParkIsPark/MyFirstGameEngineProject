#pragma once

#include "FGPUMeshResource.h"

#include <cstdint>
#include <string>

struct FRenderScene;
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

    bool IsReady() const { return program_ != 0; }
    unsigned Program() const { return program_; }
    std::uint64_t ContextGeneration() const { return contextGeneration_; }

private:
    unsigned program_ = 0;
    std::uint64_t contextGeneration_ = 0;
};
