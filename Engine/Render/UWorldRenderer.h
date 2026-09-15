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
class FOpenGLRayTracingBackendFactory;
class FRayEffectsScheduler;
class FStderrRayEffectsWarningSink;
class FOpenGLMeshUploadAdapter;
class UGPUMeshCache;
class UHardwareGBuffer;
class UHardwareRasterizer;
class URasterLightingPass;
class UHybridPresentationPass;
class URayEffectsReconstruction;
class FTemporalSequence;
class UWorld;

struct FWorldRenderRequest
{
    const FRenderScene& scene;
    FRenderTarget& target;
    FRenderFeatures features;
    FRenderQuality quality;
    int internalWidth;
    int internalHeight;
    FBackendSelection backendSelection;
    std::vector<ERenderPass> passPlan;
};

struct FWorldRendererStats
{
    // Cumulative for this renderer's lifetime, including Shutdown/Init cycles.
    std::uint64_t hardwareDrawCalls = 0;
    std::uint64_t geometryUploads = 0;
    std::uint64_t geometryReuploads = 0;
    std::uint64_t primaryResourceAllocations = 0;
    std::uint64_t primaryReleasedResources = 0;
    std::uint64_t primaryResourceIdentity = 0;
    std::uint64_t materialTextureUploads = 0;
    std::uint64_t gbufferResourceRevision = 0;
    std::uint64_t rasterOutputAllocations = 0;
    std::uint64_t environmentTextureUploads = 0;
    std::uint64_t rasterResourceRevision = 0;
    std::uint64_t rasterResourceIdentity = 0;
    std::uint64_t presentationResourceAllocations = 0;
    std::uint64_t presentationReleasedResources = 0;
    std::uint64_t presentationResourceRevision = 0;
    std::uint64_t presentationResourceIdentity = 0;
    std::uint64_t hardwareGBufferPasses = 0;
    std::uint64_t rasterLightingPasses = 0;
    std::uint64_t compositePasses = 0;
    int outputWidth = 0;
    int outputHeight = 0;
    int internalWidth = 0;
    int internalHeight = 0;
    // Actual GL work, including rollback; ray*Uploads below are committed transactions.
    std::uint64_t rayResourceAllocations = 0;
    std::uint64_t rayReleasedResources = 0;
    std::uint64_t raySceneUploadAttempts = 0;
    std::uint64_t rayBLASUploadAttempts = 0;
    std::uint64_t rayInstanceUploadAttempts = 0;
    std::uint64_t rayMaterialUploadAttempts = 0;
    std::uint64_t rayOutputAllocationAttempts = 0;
    std::uint64_t rayBufferUploadCalls = 0;
    std::uint64_t rayTextureUploadCalls = 0;
    std::uint64_t rayBackendCalls = 0;
    std::uint64_t rayFactoryCalls = 0;
    std::uint64_t rayBackendInitializations = 0;
    std::uint64_t rayDraws = 0;
    std::uint64_t raySceneUploads = 0;
    std::uint64_t rayBLASUploads = 0;
    std::uint64_t rayInstanceUploads = 0;
    std::uint64_t rayMaterialUploads = 0;
    std::uint64_t rayOutputAllocations = 0;
    std::uint64_t rayDispatches = 0;
    std::uint64_t rayMemoryBarriers = 0;
    ERayTracingBackend activeRayBackend = ERayTracingBackend::Auto;
    std::string backendReason;
    // Current live ownership, refreshed by Stats(); target is independently
    // owned by the caller and exposes FRenderTarget::OwnedAttachmentCount().
    std::size_t residentGeometryResources = 0;
    std::size_t liveGBufferTextures = 0;
    std::size_t liveGBufferFramebuffers = 0;
    std::size_t liveMaterialTextures = 0;
    std::size_t liveEnvironmentTextures = 0;
    std::size_t liveRasterOutputTextures = 0;
    std::size_t liveRasterFramebuffers = 0;
    std::size_t liveHybridHDRTextures = 0;
    std::size_t liveHybridFramebuffers = 0;
    std::size_t liveRayOutputTextures = 0;
    std::size_t liveRayBLAS = 0;
    std::uint64_t reconstructionResourceAllocations = 0;
    std::uint64_t reconstructionReleasedResources = 0;
    std::uint64_t reconstructionPasses = 0;
    std::size_t liveReconstructionTextures = 0;
    std::size_t liveReconstructionFramebuffers = 0;
    std::uint32_t temporalFrameIndex = 0;
    std::uint64_t cpuFramebufferGenerations = 0;
    std::uint64_t cpuReadbacks = 0;
    std::uint64_t cpuFramebufferUploads = 0;
};

// GL-free view-model used by both Editor surfaces. Auto in activeRayBackend
// means no effective effects; requested intent must not advertise success.
inline std::string DescribeRayTracingStatus(const FRenderFeatures& requested,
                                           const FWorldRendererStats& stats)
{
    const auto name = [](ERayTracingBackend backend) {
        return backend == ERayTracingBackend::ComputeGL43 ? "Compute" :
            backend == ERayTracingBackend::CompatibleGL33 ? "Compatible" : "Auto";
    };
    const bool enabled = requested.rayTracing &&
        stats.activeRayBackend != ERayTracingBackend::Auto;
    return std::string("Hardware Raster | RT ") + (enabled ? "On (" : "Disabled (") +
        (enabled ? name(stats.activeRayBackend) : "raster only") + ") | requested " +
        (requested.rayTracing ? name(requested.rayTracingBackend) : "Off");
}

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
    const FWorldRendererStats& Stats() const;

private:
    std::unique_ptr<IWorldRenderExecutor> executor_;
    // Shared hardware path resources. The injected-executor constructor leaves
    // these empty so GL-free routing tests retain their narrow seam.
    std::unique_ptr<FOpenGLMeshUploadAdapter> hardwareUploadAdapter_;
    std::unique_ptr<UGPUMeshCache> hardwareMeshCache_;
    std::unique_ptr<UHardwareGBuffer> hardwareGBuffer_;
    std::unique_ptr<UHardwareRasterizer> hardwareRasterizer_;
    std::unique_ptr<URasterLightingPass> rasterLightingPass_;
    std::unique_ptr<UHybridPresentationPass> hybridPresentationPass_;
    std::unique_ptr<FOpenGLRayTracingBackendFactory> rayBackendFactory_;
    std::unique_ptr<FStderrRayEffectsWarningSink> rayWarningSink_;
    std::unique_ptr<FRayEffectsScheduler> rayEffectsScheduler_;
    std::unique_ptr<URayEffectsReconstruction> rayEffectsReconstruction_;
    std::unique_ptr<FTemporalSequence> temporalSequence_;
    bool initialized_ = false;
    mutable FWorldRendererStats stats_;
};
