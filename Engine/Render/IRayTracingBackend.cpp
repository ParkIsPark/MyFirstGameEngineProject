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
    const std::string token = "\n" + key + "\n";
    if (warningKeys_.find(token) != std::string::npos) return;
    warningKeys_ += token;
    warnings_.Warn(message);
    ++stats_.warnings;
}

void FRayEffectsScheduler::ResetForContext(
    std::uint64_t contextGeneration) noexcept
{
    if (backend_) backend_->Shutdown();
    backend_.reset();
    activeKind_ = ERayTracingBackend::Auto;
    contextGeneration_ = contextGeneration;
    failureKey_.clear();
    warningKeys_.clear();
    automaticComputeFallback_ = false;
}

bool FRayEffectsScheduler::EnsureBackend(ERayTracingBackend kind,
                                         std::uint64_t contextGeneration,
                                         std::string& diagnostic)
{
    if (backend_ && activeKind_ == kind) return true;
    if (backend_) backend_->Shutdown();
    backend_.reset();
    activeKind_ = kind;
    ++stats_.factoryCalls;
    backend_ = factory_.Create(kind, &diagnostic);
    if (!backend_) return false;
    ++stats_.backendInitializations;
    if (!backend_->Init(contextGeneration, &diagnostic))
    {
        backend_->Shutdown();
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
    if (!HasRequestedEffects(inputs.features)) return true;
    if (inputs.contextGeneration != contextGeneration_)
        ResetForContext(inputs.contextGeneration);

    const std::string requestKey = std::to_string(inputs.contextGeneration) + ":" +
        BackendName(selection.requested) + ":" + BackendName(selection.selected) + ":" +
        (selection.available ? "available" : "unavailable") + ":" +
        (selection.rayTracingEnabled ? "enabled" : "disabled");
    if (failureKey_ == requestKey) return true;

    if (!selection.rayTracingEnabled || !selection.available)
    {
        failureKey_ = requestKey;
        WarnOnce(requestKey + ":unavailable",
            selection.fallbackReason.empty()
                ? std::string("Ray effects disabled because requested backend is unavailable")
                : selection.fallbackReason);
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
            diagnostic.clear();
            selected = ERayTracingBackend::CompatibleGL33;
            if (!EnsureBackend(selected, inputs.contextGeneration, diagnostic))
            {
                failureKey_ = requestKey;
                WarnOnce(requestKey + ":fallback-failed",
                    "Compatible ray-effects fallback failed: " + diagnostic);
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
            return true;
        }
    }

    ++stats_.backendCalls;
    if (!backend_->RenderEffects(inputs, outputs, &diagnostic))
    {
        outputs = {};
        // Per-frame uploads and draws are transactional. Their failures are
        // recoverable, so keep the initialized backend and retry next frame.
        WarnOnce(requestKey + ":render-failed:" + diagnostic,
            std::string("Ray-effects backend ") + BackendName(selected) +
            " failed; retaining raster output: " + diagnostic);
        return true;
    }
    const FRayTracingBackendStats& backendStats = backend_->Stats();
    stats_.resourceAllocations = backendStats.resourceAllocations;
    stats_.sceneUploads = backendStats.sceneUploads;
    return true;
}

void FRayEffectsScheduler::Shutdown() noexcept
{
    if (backend_) backend_->Shutdown();
    backend_.reset();
    activeKind_ = ERayTracingBackend::Auto;
    contextGeneration_ = 0;
    failureKey_.clear();
    warningKeys_.clear();
    automaticComputeFallback_ = false;
}
