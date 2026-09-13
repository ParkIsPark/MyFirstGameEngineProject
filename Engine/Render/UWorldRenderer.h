#pragma once

#include "FGraphicsCapabilities.h"
#include "FRenderFeatures.h"
#include "FRenderPipelinePlan.h"
#include "FRenderQuality.h"
#include "FRenderScene.h"
#include "FRenderTarget.h"

#include <cstdint>
#include <memory>
#include <vector>

class ACamera;
class FOpenGLMeshUploadAdapter;
class UGPUMeshCache;
class UHardwareGBuffer;
class UHardwareRasterizer;
class URasterLightingPass;
class UWorld;

struct FWorldRenderRequest
{
    const FRenderScene& scene;
    FRenderTarget& target;
    FRenderFeatures features;
    FRenderQuality quality;
    FBackendSelection backendSelection;
    std::vector<ERenderPass> passPlan;
};

struct FWorldRendererStats
{
    std::uint64_t hardwareGBufferPasses = 0;
    std::uint64_t rasterLightingPasses = 0;
    std::uint64_t compositePasses = 0;
    std::uint64_t rayResourceAllocations = 0;
    std::uint64_t rayDispatches = 0;
    std::uint64_t cpuFramebufferGenerations = 0;
    std::uint64_t cpuReadbacks = 0;
    std::uint64_t cpuFramebufferUploads = 0;
};

// Narrow dispatch seam: tests observe one immutable scene extraction and one
// execution request without OpenGL. Production construction selects the
// hardware G-buffer, raster-lighting, and composite executor.
class IWorldRenderExecutor
{
public:
    virtual ~IWorldRenderExecutor() = default;
    virtual void Init() {}
    virtual void Shutdown() noexcept {}
    virtual bool RequiresOpenGLTargetBinding() const { return false; }
    virtual bool Execute(const FWorldRenderRequest& request) = 0;
};

class UWorldRenderer
{
public:
    UWorldRenderer();
    explicit UWorldRenderer(std::unique_ptr<IWorldRenderExecutor> executor);
    ~UWorldRenderer();
    UWorldRenderer(const UWorldRenderer&) = delete;
    UWorldRenderer& operator=(const UWorldRenderer&) = delete;

    void Init();
    void Shutdown() noexcept;

    bool Render(UWorld& world,
                const ACamera& camera,
                FRenderTarget& target,
                const FRenderFeatures& features,
                const FRenderQuality& quality,
                const FBackendSelection& backendSelection,
                std::uint64_t expectedContextGeneration = 0);
    const FWorldRendererStats& Stats() const { return stats_; }

private:
    std::unique_ptr<IWorldRenderExecutor> executor_;
    // Shared hardware path resources. The injected-executor constructor leaves
    // these empty so GL-free routing tests retain their narrow seam.
    std::unique_ptr<FOpenGLMeshUploadAdapter> hardwareUploadAdapter_;
    std::unique_ptr<UGPUMeshCache> hardwareMeshCache_;
    std::unique_ptr<UHardwareGBuffer> hardwareGBuffer_;
    std::unique_ptr<UHardwareRasterizer> hardwareRasterizer_;
    std::unique_ptr<URasterLightingPass> rasterLightingPass_;
    bool initialized_ = false;
    FWorldRendererStats stats_;
};
