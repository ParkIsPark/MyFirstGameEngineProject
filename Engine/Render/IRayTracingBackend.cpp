#include "IRayTracingBackend.h"

#include <utility>

namespace
{
bool HasRequestedEffects(const FRenderFeatures& features)
{
    return features.rayTracing && (features.rayTracedShadows ||
        features.rayTracedGI || features.rayTracedReflections);
}

const char* BackendName(ERayTracingBackend backend)
{
    switch (backend)
    {
        case ERayTracingBackend::CompatibleGL33: return "CompatibleGL33";
        case ERayTracingBackend::ComputeGL43: return "ComputeGL43";
        default: return "Auto";
    }
}
} // namespace

FRayEffectsScheduler::FRayEffectsScheduler(IRayTracingBackendFactory& factory,
                                           IRayEffectsWarningSink& warnings)
    : factory_(factory), warnings_(warnings)
{
}

FRayEffectsScheduler::~FRayEffectsScheduler()
{
    Shutdown();
}

void FRayEffectsScheduler::WarnOnce(const std::string& key,
                                    const std::string& message)
{
    backendReason_ = message;
    const std::string token = "\n" + key + "\n";
    if (warningKeys_.find(token) != std::string::npos) return;
    warningKeys_ += token;
    warnings_.Warn(message);
    ++stats_.warnings;
}

void FRayEffectsScheduler::ResetForContext(
    std::uint64_t contextGeneration) noexcept
{
    if (backend_) { backend_->Shutdown(); CollectBackendStats(); }
    backend_.reset();
    activeKind_ = ERayTracingBackend::Auto;
    contextGeneration_ = contextGeneration;
    failureKey_.clear();
    warningKeys_.clear();
    automaticComputeFallback_ = false;
    observedBackendStats_ = {};
    backendReason_.clear();
    failureReason_.clear();
    automaticFallbackReason_.clear();
}

void FRayEffectsScheduler::CollectBackendStats() noexcept
{
    if (!backend_) return;
    const auto& current = backend_->Stats();
    // Backends keep lifetime totals while alive; snapshot deltas before any
    // replacement so totals cannot decrease when a new backend starts at zero.
#define ACCUMULATE(field) stats_.field += current.field - observedBackendStats_.field
    ACCUMULATE(resourceAllocations);
    ACCUMULATE(releasedResources);
    ACCUMULATE(sceneUploadAttempts);
    ACCUMULATE(blasUploadAttempts);
    ACCUMULATE(instanceUploadAttempts);
    ACCUMULATE(materialUploadAttempts);
    ACCUMULATE(outputAllocationAttempts);
    ACCUMULATE(bufferUploadCalls);
    ACCUMULATE(textureUploadCalls);
    ACCUMULATE(sceneUploads);
    ACCUMULATE(rayDraws);
    ACCUMULATE(rayDispatches);
    ACCUMULATE(memoryBarriers);
    ACCUMULATE(blasUploads);
    ACCUMULATE(instanceUploads);
    ACCUMULATE(materialUploads);
    ACCUMULATE(outputAllocations);
#undef ACCUMULATE
    observedBackendStats_ = current;
}

bool FRayEffectsScheduler::EnsureBackend(ERayTracingBackend kind,
                                         std::uint64_t contextGeneration,
                                         std::string& diagnostic)
{
    if (backend_ && activeKind_ == kind) return true;
    if (backend_) { backend_->Shutdown(); CollectBackendStats(); }
    backend_.reset();
    activeKind_ = kind;
    observedBackendStats_ = {};
    ++stats_.factoryCalls;
    backend_ = factory_.Create(kind, &diagnostic);
    if (!backend_) return false;
    ++stats_.backendInitializations;
    const bool initialized = backend_->Init(contextGeneration, &diagnostic);
    CollectBackendStats();
    if (!initialized)
    {
        backend_->Shutdown();
        CollectBackendStats();
        backend_.reset();
        return false;
    }
    return true;
}

bool FRayEffectsScheduler::Execute(const FRayEffectInputs& inputs,
                                   const FBackendSelection& selection,
                                   FRayEffectOutputs& outputs)
{
    outputs = {};
    if (!HasRequestedEffects(inputs.features))
    {
        backendReason_ = inputs.features.rayTracing
            ? "No secondary ray effects requested" : "Ray tracing master is off";
        return true;
    }
    if (inputs.contextGeneration != contextGeneration_)
        ResetForContext(inputs.contextGeneration);

    const std::string requestKey = std::to_string(inputs.contextGeneration) + ":" +
        BackendName(selection.requested) + ":" + BackendName(selection.selected) + ":" +
        (selection.available ? "available" : "unavailable") + ":" +
        (selection.rayTracingEnabled ? "enabled" : "disabled");
    if (failureKey_ == requestKey)
    {
        backendReason_ = failureReason_;
        return true;
    }

    if (!selection.rayTracingEnabled || !selection.available)
    {
        failureKey_ = requestKey;
        WarnOnce(requestKey + ":unavailable",
            selection.fallbackReason.empty()
                ? std::string("Ray effects disabled because requested backend is unavailable")
                : selection.fallbackReason);
        failureReason_ = backendReason_;
        return true;
    }

    const bool automaticComputeRequest =
        selection.requested == ERayTracingBackend::Auto &&
        selection.selected == ERayTracingBackend::ComputeGL43;
    if (!automaticComputeRequest) automaticComputeFallback_ = false;
    ERayTracingBackend selected = selection.selected;
    if (automaticComputeRequest && automaticComputeFallback_ && backend_ &&
        activeKind_ == ERayTracingBackend::CompatibleGL33)
        selected = ERayTracingBackend::CompatibleGL33;
    std::string diagnostic;
    if (!EnsureBackend(selected, inputs.contextGeneration, diagnostic))
    {
        const bool automaticCompute = selection.requested == ERayTracingBackend::Auto &&
            selected == ERayTracingBackend::ComputeGL43;
        if (automaticCompute)
        {
            WarnOnce(requestKey + ":auto-fallback",
                "OpenGL 4.3 Compute ray effects are unavailable (" + diagnostic +
                "); falling back to the OpenGL 3.3 compatible backend");
            automaticFallbackReason_ = backendReason_;
            diagnostic.clear();
            selected = ERayTracingBackend::CompatibleGL33;
            if (!EnsureBackend(selected, inputs.contextGeneration, diagnostic))
            {
                failureKey_ = requestKey;
                WarnOnce(requestKey + ":fallback-failed",
                    "Compatible ray-effects fallback failed: " + diagnostic);
                failureReason_ = backendReason_;
                return true;
            }
            automaticComputeFallback_ = true;
        }
        else
        {
            failureKey_ = requestKey;
            WarnOnce(requestKey + ":init-failed",
                std::string("Ray-effects backend ") + BackendName(selected) +
                " failed to initialize: " + diagnostic);
            failureReason_ = backendReason_;
            return true;
        }
    }

    ++stats_.backendCalls;
    const bool rendered = backend_->RenderEffects(inputs, outputs, &diagnostic);
    CollectBackendStats();
    if (!rendered)
    {
        outputs = {};
        // Per-frame uploads and draws are transactional. Their failures are
        // recoverable, so keep the initialized backend and retry next frame.
        WarnOnce(requestKey + ":render-failed:" + diagnostic,
            std::string("Ray-effects backend ") + BackendName(selected) +
            " failed; retaining raster output: " + diagnostic);
        return true;
    }
    if (automaticComputeFallback_)
        backendReason_ = automaticFallbackReason_;
    else
        backendReason_ = selection.fallbackReason.empty()
            ? std::string("Selected ") + BackendName(selected) : selection.fallbackReason;
    return true;
}

void FRayEffectsScheduler::Shutdown() noexcept
{
    if (backend_) { backend_->Shutdown(); CollectBackendStats(); }
    backend_.reset();
    activeKind_ = ERayTracingBackend::Auto;
    contextGeneration_ = 0;
    failureKey_.clear();
    warningKeys_.clear();
    automaticComputeFallback_ = false;
    observedBackendStats_ = {};
    backendReason_.clear();
    failureReason_.clear();
    automaticFallbackReason_.clear();
}
