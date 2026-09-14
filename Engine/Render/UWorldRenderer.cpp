#include "UWorldRenderer.h"

#include "ACamera.h"
#include "IRayTracingBackend.h"
#include "UHardwareGBuffer.h"
#include "UHardwareRasterizer.h"
#include "URasterLightingPass.h"
#include "UGL33RayTracingBackend.h"
#include "UGPUMeshCache.h"
#include "UWorld.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
class FHardwareWorldRenderExecutor final : public IWorldRenderExecutor
{
public:
    FHardwareWorldRenderExecutor(UGPUMeshCache& meshCache,
                                 UHardwareGBuffer& gbuffer,
                                 UHardwareRasterizer& rasterizer,
                                 URasterLightingPass& lighting,
                                 FRayEffectsScheduler& rayEffects,
                                 FWorldRendererStats& stats)
        : meshCache_(meshCache), gbuffer_(gbuffer), rasterizer_(rasterizer),
          lighting_(lighting), rayEffects_(rayEffects), stats_(stats)
    {
    }

    void Init() override
    {
        std::string diagnostic;
        const std::uint64_t generation = ActiveRenderTargetContextGeneration();
        if (!rasterizer_.Init(generation, &diagnostic) ||
            !lighting_.Init(generation, &diagnostic))
            throw std::runtime_error(diagnostic.empty()
                ? "Hardware renderer initialization failed" : diagnostic);
    }

    bool RequiresOpenGLTargetBinding() const override { return true; }

    bool Execute(const FWorldRenderRequest& request) override
    {
        const std::uint64_t generation = request.target.ContextGeneration();
        std::string diagnostic;
        meshCache_.BeginFrame();
        if (!lighting_.PrepareEnvironment(request.scene, generation, &diagnostic) ||
            !gbuffer_.Resize(request.target.Width(), request.target.Height(), generation) ||
            !rasterizer_.RenderGeometry(request.scene, request.quality, meshCache_,
                                        gbuffer_, lighting_.EnvironmentTexture(),
                                        generation, &diagnostic))
        {
            meshCache_.ReleaseUnused();
            if (!diagnostic.empty())
                std::cerr << "[Renderer] " << diagnostic << '\n';
            return false;
        }
        meshCache_.ReleaseUnused();
        ++stats_.hardwareGBufferPasses;

        FRasterLightingOutput rasterOutput;
        if (!lighting_.Render(request.scene, request.quality, gbuffer_, generation,
                              rasterOutput, &diagnostic))
        {
            if (!diagnostic.empty())
                std::cerr << "[Renderer] " << diagnostic << '\n';
            return false;
        }
        ++stats_.rasterLightingPasses;

        FRayEffectOutputs rayOutputs;
        if (std::find(request.passPlan.begin(), request.passPlan.end(),
                      ERenderPass::RayTracedEffects) != request.passPlan.end())
        {
            FRayEffectInputs rayInputs;
            rayInputs.gbuffer = &gbuffer_;
            rayInputs.scene = &request.scene;
            rayInputs.rasterLighting = &rasterOutput;
            rayInputs.features = request.features;
            rayInputs.quality = request.quality;
            rayInputs.environmentTexture = lighting_.EnvironmentTexture();
            rayInputs.width = request.target.Width();
            rayInputs.height = request.target.Height();
            rayInputs.contextGeneration = generation;
            rayEffects_.Execute(rayInputs, request.backendSelection, rayOutputs);
            stats_.activeRayBackend = rayEffects_.ActiveKind();
            stats_.backendReason = rayEffects_.BackendReason();
        }
        FCompositeOutput composite;
        if (!lighting_.Composite(request.target, rasterOutput, rayOutputs,
                                 generation, composite, &diagnostic))
        {
            if (!diagnostic.empty())
                std::cerr << "[Renderer] " << diagnostic << '\n';
            return false;
        }
        ++stats_.compositePasses;
        return composite.valid;
    }

private:
    UGPUMeshCache& meshCache_;
    UHardwareGBuffer& gbuffer_;
    UHardwareRasterizer& rasterizer_;
    URasterLightingPass& lighting_;
    FRayEffectsScheduler& rayEffects_;
    FWorldRendererStats& stats_;
};

class FTargetRestoreGuard
{
public:
    explicit FTargetRestoreGuard(FRenderTarget& target) : target_(target) {}
    ~FTargetRestoreGuard() { target_.End(); }
private:
    FRenderTarget& target_;
};
} // namespace

UWorldRenderer::UWorldRenderer()
    : hardwareUploadAdapter_(std::make_unique<FOpenGLMeshUploadAdapter>()),
      hardwareMeshCache_(std::make_unique<UGPUMeshCache>(*hardwareUploadAdapter_)),
      hardwareGBuffer_(std::make_unique<UHardwareGBuffer>()),
      hardwareRasterizer_(std::make_unique<UHardwareRasterizer>()),
      rasterLightingPass_(std::make_unique<URasterLightingPass>()),
      rayBackendFactory_(std::make_unique<FOpenGLRayTracingBackendFactory>()),
      rayWarningSink_(std::make_unique<FStderrRayEffectsWarningSink>()),
      rayEffectsScheduler_(std::make_unique<FRayEffectsScheduler>(
          *rayBackendFactory_, *rayWarningSink_))
{
    executor_ = std::make_unique<FHardwareWorldRenderExecutor>(
        *hardwareMeshCache_, *hardwareGBuffer_, *hardwareRasterizer_,
        *rasterLightingPass_, *rayEffectsScheduler_, stats_);
}

UWorldRenderer::UWorldRenderer(std::unique_ptr<IWorldRenderExecutor> executor)
    : executor_(std::move(executor))
{
}

UWorldRenderer::~UWorldRenderer()
{
    Shutdown();
}

void UWorldRenderer::Init()
{
    if (initialized_ || !executor_) return;
    executor_->Init();
    initialized_ = true;
}

void UWorldRenderer::Shutdown() noexcept
{
    if (!executor_) return;
    if (rayEffectsScheduler_) rayEffectsScheduler_->Shutdown();
    if (hardwareMeshCache_) hardwareMeshCache_->Clear();
    if (hardwareGBuffer_) hardwareGBuffer_->Release();
    if (rasterLightingPass_) rasterLightingPass_->Shutdown();
    if (hardwareRasterizer_) hardwareRasterizer_->Shutdown();
    executor_->Shutdown();
    initialized_ = false;
    stats_.activeRayBackend = ERayTracingBackend::Auto;
    stats_.backendReason.clear();
}

const FWorldRendererStats& UWorldRenderer::Stats() const
{
    if (hardwareMeshCache_)
    {
        stats_.geometryUploads = hardwareMeshCache_->Stats().uploads;
        stats_.geometryReuploads = hardwareMeshCache_->Stats().reuploads;
        stats_.residentGeometryResources = hardwareMeshCache_->Stats().residentResources;
        stats_.hardwareDrawCalls = hardwareRasterizer_->IndexedDrawCalls();
        stats_.liveMaterialTextures = hardwareRasterizer_->OwnedMaterialTextureCount();
        stats_.liveGBufferTextures = hardwareGBuffer_->OwnedTextureCount();
        stats_.liveGBufferFramebuffers = hardwareGBuffer_->Framebuffer() ? 1u : 0u;
        stats_.liveEnvironmentTextures = rasterLightingPass_->EnvironmentTexture() ? 1u : 0u;
        stats_.liveRasterOutputTextures = rasterLightingPass_->OwnedTextureCount();
        stats_.liveRasterFramebuffers = rasterLightingPass_->Framebuffer() ? 1u : 0u;
        const auto& ray = rayEffectsScheduler_->Stats();
        stats_.rayResourceAllocations = ray.resourceAllocations;
        stats_.rayReleasedResources = ray.releasedResources;
        stats_.raySceneUploadAttempts = ray.sceneUploadAttempts;
        stats_.rayBLASUploadAttempts = ray.blasUploadAttempts;
        stats_.rayInstanceUploadAttempts = ray.instanceUploadAttempts;
        stats_.rayMaterialUploadAttempts = ray.materialUploadAttempts;
        stats_.rayOutputAllocationAttempts = ray.outputAllocationAttempts;
        stats_.rayBufferUploadCalls = ray.bufferUploadCalls;
        stats_.rayTextureUploadCalls = ray.textureUploadCalls;
        stats_.rayFactoryCalls = ray.factoryCalls;
        stats_.rayBackendInitializations = ray.backendInitializations;
        stats_.rayBackendCalls = ray.backendCalls;
        stats_.rayDraws = ray.rayDraws;
        stats_.rayDispatches = ray.rayDispatches;
        stats_.rayMemoryBarriers = ray.memoryBarriers;
        stats_.raySceneUploads = ray.sceneUploads;
        stats_.rayBLASUploads = ray.blasUploads;
        stats_.rayInstanceUploads = ray.instanceUploads;
        stats_.rayMaterialUploads = ray.materialUploads;
        stats_.rayOutputAllocations = ray.outputAllocations;
        const auto* backend = rayEffectsScheduler_->ActiveBackend();
        stats_.liveRayOutputTextures = backend ? backend->Stats().ownedOutputTextures : 0u;
        stats_.liveRayBLAS = backend ? backend->Stats().residentBLAS : 0u;
    }
    return stats_;
}

bool UWorldRenderer::Render(UWorld& world,
                            const ACamera& camera,
                            FRenderTarget& target,
                            const FRenderFeatures& features,
                            const FRenderQuality& quality,
                            const FBackendSelection& backendSelection,
                            std::uint64_t expectedContextGeneration)
{
    const std::uint64_t generation = expectedContextGeneration == 0
        ? target.ContextGeneration() : expectedContextGeneration;
    if (!executor_ || !target.IsValidForContext(generation)) return false;

    FRenderFeatures normalized = features;
    normalized.hardwareRaster = true;
    FRenderFeatures effective = normalized;
    if (!backendSelection.rayTracingEnabled) effective.rayTracing = false;
    const std::vector<ERenderPass> passPlan = BuildRenderPipelinePlan(effective);
    stats_.activeRayBackend = ERayTracingBackend::Auto;
    stats_.backendReason = !features.rayTracing ? "Ray tracing master is off" :
        (!backendSelection.fallbackReason.empty() ? backendSelection.fallbackReason :
         "No secondary ray effects requested");
    const FRenderScene scene = ExtractRenderScene(world, camera);
    FWorldRenderRequest request{
        scene, target, normalized, quality, backendSelection, passPlan,
    };

    if (!executor_->RequiresOpenGLTargetBinding()) return executor_->Execute(request);
    if (!target.Begin()) return false;
    FTargetRestoreGuard restore(target);
    return executor_->Execute(request);
}
