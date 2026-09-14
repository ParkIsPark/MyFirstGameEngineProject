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
            stats_.rayResourceAllocations = rayEffects_.Stats().resourceAllocations;
            stats_.rayBackendCalls = rayEffects_.Stats().backendCalls;
            stats_.activeRayBackend = rayEffects_.ActiveKind();
            const IRayTracingBackend* activeBackend =
                rayEffects_.ActiveBackend();
            stats_.rayDispatches = activeBackend
                ? activeBackend->Stats().rayDispatches : 0u;
            stats_.rayMemoryBarriers = activeBackend
                ? activeBackend->Stats().memoryBarriers : 0u;
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
    const FRenderScene scene = ExtractRenderScene(world, camera);
    FWorldRenderRequest request{
        scene, target, normalized, quality, backendSelection, passPlan,
    };

    if (!executor_->RequiresOpenGLTargetBinding()) return executor_->Execute(request);
    if (!target.Begin()) return false;
    FTargetRestoreGuard restore(target);
    return executor_->Execute(request);
}
