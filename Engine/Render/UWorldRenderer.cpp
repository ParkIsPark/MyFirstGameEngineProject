#include "UWorldRenderer.h"

#include "ACamera.h"
#include "IRayTracingBackend.h"
#include "FRenderHistory.h"
#include "FRenderMath.h"
#include "UHardwareGBuffer.h"
#include "UHardwareRasterizer.h"
#include "UHybridPresentationPass.h"
#include "URasterLightingPass.h"
#include "URayEffectsReconstruction.h"
#include "UGL33RayTracingBackend.h"
#include "UGPUMeshCache.h"
#include "UWorld.h"

#include <algorithm>
#include <iostream>
#include <limits>
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
                                 UHybridPresentationPass& presentation,
                                 FRayEffectsScheduler& rayEffects,
                                 URayEffectsReconstruction& reconstruction,
                                 FTemporalSequence& temporalSequence,
                                 FWorldRendererStats& stats)
        : meshCache_(meshCache), gbuffer_(gbuffer), rasterizer_(rasterizer),
          lighting_(lighting), presentation_(presentation), rayEffects_(rayEffects),
          reconstruction_(reconstruction), temporalSequence_(temporalSequence),
          stats_(stats)
    {
    }

    void Init() override
    {
        std::string diagnostic;
        const std::uint64_t generation = ActiveRenderTargetContextGeneration();
        if (!rasterizer_.Init(generation, &diagnostic) ||
            !lighting_.Init(generation, &diagnostic) ||
            !presentation_.Init(generation, &diagnostic))
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
            !gbuffer_.Resize(request.internalWidth, request.internalHeight, generation) ||
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
        if (!request.quality.depthView &&
            std::find(request.passPlan.begin(), request.passPlan.end(),
                      ERenderPass::RayTracedEffects) != request.passPlan.end())
        {
            const ERayTracingBackend signatureBackend =
                rayEffects_.ActiveKind() == ERayTracingBackend::Auto
                    ? request.backendSelection.selected : rayEffects_.ActiveKind();
            const std::uint64_t signature = BuildRenderHistorySignature(
                request.scene, request.features, request.quality,
                request.internalWidth, request.internalHeight, signatureBackend,
                generation, lighting_.EnvironmentRevision());
            const FTemporalFrame temporal = temporalSequence_.Begin(signature,
                static_cast<std::uint32_t>(request.quality.temporalFrames));
            stats_.temporalFrameIndex = temporal.frameIndex;
            FRayEffectInputs rayInputs;
            rayInputs.gbuffer = &gbuffer_;
            rayInputs.scene = &request.scene;
            rayInputs.rasterLighting = &rasterOutput;
            rayInputs.features = request.features;
            rayInputs.quality = request.quality;
            rayInputs.environmentTexture = lighting_.EnvironmentTexture();
            rayInputs.width = request.internalWidth;
            rayInputs.height = request.internalHeight;
            rayInputs.historySignature = signature;
            rayInputs.frameIndex = temporal.frameIndex;
            rayInputs.contextGeneration = generation;
            rayEffects_.Execute(rayInputs, request.backendSelection, rayOutputs);
            stats_.activeRayBackend = rayEffects_.ActiveKind();
            stats_.backendReason = rayEffects_.BackendReason();
            if (rayEffects_.ActiveKind() == ERayTracingBackend::Auto ||
                (!rayOutputs.shadowedDirectTarget && !rayOutputs.globalIlluminationTarget &&
                 !rayOutputs.opticalContributionTarget))
            {
                temporalSequence_.Reset();
                reconstruction_.Reset();
                stats_.temporalFrameIndex = 0;
            }
            else
            {
                FRayEffectOutputs reconstructed;
                if (reconstruction_.Reconstruct(gbuffer_, rayOutputs, temporal,
                    request.quality, generation, reconstructed, &diagnostic))
                    rayOutputs = reconstructed;
                else
                {
                    reconstruction_.Reset();
                    temporalSequence_.Reset();
                    stats_.temporalFrameIndex = 0;
                    if (!diagnostic.empty())
                        std::cerr << "[Renderer] " << diagnostic << '\n';
                }
            }
        }
        else
        {
            temporalSequence_.Reset();
            reconstruction_.Reset();
            stats_.temporalFrameIndex = 0;
        }
        if (!presentation_.Resize(request.internalWidth, request.internalHeight,
                                  generation, &diagnostic))
        {
            if (!diagnostic.empty())
                std::cerr << "[Renderer] " << diagnostic << '\n';
            return false;
        }
        FRenderOutputView presentationInput;
        if (request.quality.depthView)
            presentationInput = rasterOutput.environmentAmbientTarget;
        else if (!presentation_.CompositeHDR(gbuffer_, rasterOutput, rayOutputs,
                                              request.quality, presentationInput,
                                              &diagnostic))
        {
            if (!diagnostic.empty())
                std::cerr << "[Renderer] " << diagnostic << '\n';
            return false;
        }
        if (!presentation_.Present(presentationInput, request.target,
                                   request.quality, &diagnostic))
        {
            if (!diagnostic.empty())
                std::cerr << "[Renderer] " << diagnostic << '\n';
            return false;
        }
        ++stats_.compositePasses;
        return true;
    }

private:
    UGPUMeshCache& meshCache_;
    UHardwareGBuffer& gbuffer_;
    UHardwareRasterizer& rasterizer_;
    URasterLightingPass& lighting_;
    UHybridPresentationPass& presentation_;
    FRayEffectsScheduler& rayEffects_;
    URayEffectsReconstruction& reconstruction_;
    FTemporalSequence& temporalSequence_;
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
      hybridPresentationPass_(std::make_unique<UHybridPresentationPass>()),
      rayBackendFactory_(std::make_unique<FOpenGLRayTracingBackendFactory>()),
      rayWarningSink_(std::make_unique<FStderrRayEffectsWarningSink>()),
      rayEffectsScheduler_(std::make_unique<FRayEffectsScheduler>(
          *rayBackendFactory_, *rayWarningSink_)),
      rayEffectsReconstruction_(std::make_unique<URayEffectsReconstruction>()),
      temporalSequence_(std::make_unique<FTemporalSequence>())
{
    executor_ = std::make_unique<FHardwareWorldRenderExecutor>(
        *hardwareMeshCache_, *hardwareGBuffer_, *hardwareRasterizer_,
        *rasterLightingPass_, *hybridPresentationPass_, *rayEffectsScheduler_,
        *rayEffectsReconstruction_, *temporalSequence_, stats_);
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
    if (rayEffectsReconstruction_) rayEffectsReconstruction_->Shutdown();
    if (temporalSequence_) temporalSequence_->Reset();
    if (hardwareMeshCache_) hardwareMeshCache_->Clear();
    if (hardwareGBuffer_) hardwareGBuffer_->Release();
    if (rasterLightingPass_) rasterLightingPass_->Shutdown();
    if (hybridPresentationPass_) hybridPresentationPass_->Shutdown();
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
        stats_.liveHybridHDRTextures = hybridPresentationPass_->HDRTexture() ? 1u : 0u;
        stats_.liveHybridFramebuffers = hybridPresentationPass_->Framebuffer() ? 1u : 0u;
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
        const auto& reconstruction = rayEffectsReconstruction_->Stats();
        stats_.reconstructionResourceAllocations = reconstruction.resourceAllocations;
        stats_.reconstructionReleasedResources = reconstruction.releasedResources;
        stats_.reconstructionPasses = reconstruction.reconstructionPasses;
        stats_.liveReconstructionTextures = reconstruction.ownedTextures;
        stats_.liveReconstructionFramebuffers = reconstruction.ownedFramebuffers;
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
    const FRenderQuality sanitizedQuality = SanitizeRenderQuality(quality);
    const FInternalRenderSize internalSize = ResolveInternalRenderSize(
        target.Width(), target.Height(), sanitizedQuality.ssaa,
        std::numeric_limits<int>::max());
    if (internalSize.width == 0 || internalSize.height == 0) return false;
    stats_.outputWidth = target.Width();
    stats_.outputHeight = target.Height();
    stats_.internalWidth = internalSize.width;
    stats_.internalHeight = internalSize.height;
    FWorldRenderRequest request{
        scene, target, normalized, sanitizedQuality,
        internalSize.width, internalSize.height, backendSelection, passPlan,
    };

    if (!executor_->RequiresOpenGLTargetBinding()) return executor_->Execute(request);
    if (!target.Begin()) return false;
    FTargetRestoreGuard restore(target);
    return executor_->Execute(request);
}
