// Current complete recipe: powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1 -Name RayEffectsSchedulingTest
// Prerequisite: Engine.sln Debug|Win32. Older direct compile examples follow.
// & 'C:\msys64\ucrt64\bin\g++.exe' -std=c++17 -Wall -Wextra -pedantic -I./include -I./Engine/Render -I./Engine/Core Test/RayEffectsSchedulingTest.cpp Engine/Render/IRayTracingBackend.cpp -o RayEffectsSchedulingTest.exe; if ($LASTEXITCODE -eq 0) { .\RayEffectsSchedulingTest.exe }
#include "IRayTracingBackend.h"
#include "UWorldRenderer.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
class FFakeBackend final : public IRayTracingBackend
{
public:
    bool initResult = true;
    bool renderResult = true;
    int* renderFailuresRemaining = nullptr;
    int* initCalls = nullptr;
    int* renderCalls = nullptr;
    FRenderFeatures* observed = nullptr;

    bool Init(std::uint64_t, std::string* diagnostic) override
    {
        ++*initCalls;
        if (!initResult && diagnostic) *diagnostic = "injected initialization failure";
        return initResult;
    }
    bool RenderEffects(const FRayEffectInputs& inputs, FRayEffectOutputs& outputs,
                       std::string* diagnostic) override
    {
        ++*renderCalls;
        *observed = inputs.features;
        if ((renderFailuresRemaining && *renderFailuresRemaining > 0) || !renderResult)
        {
            if (renderFailuresRemaining && *renderFailuresRemaining > 0)
                --*renderFailuresRemaining;
            if (diagnostic) *diagnostic = "injected render failure";
            return false;
        }
        if (inputs.features.rayTracedShadows) outputs.shadows = 0.5f;
        if (inputs.features.rayTracedGI) outputs.globalIllumination = glm::vec3(0.25f);
        if (inputs.features.rayTracedReflections) outputs.reflections = glm::vec3(0.75f);
        return true;
    }
    void Shutdown() noexcept override {}
    const FRayTracingBackendStats& Stats() const override { return stats; }

private:
    FRayTracingBackendStats stats;
};

class FFakeFactory final : public IRayTracingBackendFactory
{
public:
    int calls = 0;
    int compatibleCreates = 0;
    int computeCreates = 0;
    int initCalls = 0;
    int renderCalls = 0;
    bool computeAvailable = false;
    bool initResult = true;
    bool renderResult = true;
    int renderFailuresRemaining = 0;
    FRenderFeatures observed;

    std::unique_ptr<IRayTracingBackend> Create(ERayTracingBackend backend,
                                               std::string* diagnostic) override
    {
        ++calls;
        if (backend == ERayTracingBackend::ComputeGL43)
        {
            ++computeCreates;
            if (!computeAvailable)
            {
                if (diagnostic) *diagnostic = "Compute backend is not implemented";
                return {};
            }
        }
        else ++compatibleCreates;
        auto result = std::make_unique<FFakeBackend>();
        result->initResult = initResult;
        result->renderResult = renderResult;
        result->renderFailuresRemaining = &renderFailuresRemaining;
        result->initCalls = &initCalls;
        result->renderCalls = &renderCalls;
        result->observed = &observed;
        return result;
    }
};

class FWarnings final : public IRayEffectsWarningSink
{
public:
    void Warn(const std::string& warning) override { messages.push_back(warning); }
    std::vector<std::string> messages;
};

FBackendSelection Selection(ERayTracingBackend requested,
                            ERayTracingBackend selected,
                            bool enabled = true)
{
    FBackendSelection result;
    result.requested = requested;
    result.selected = selected;
    result.available = enabled;
    result.rayTracingEnabled = enabled;
    return result;
}

FRayEffectInputs Inputs(const FRenderFeatures& features)
{
    FRayEffectInputs result;
    result.features = features;
    result.contextGeneration = 7;
    result.width = 64;
    result.height = 64;
    return result;
}

void CheckStatus(const FRayEffectsScheduler& scheduler, const FRenderFeatures& features,
                 const char* effective, const char* reason)
{
    FWorldRendererStats stats;
    stats.activeRayBackend = scheduler.ActiveKind();
    stats.backendReason = scheduler.BackendReason();
    assert(DescribeRayTracingStatus(features, stats).find(effective) != std::string::npos);
    assert(stats.backendReason.find(reason) != std::string::npos);
}

void CheckNoWorkCombinations()
{
    FFakeFactory factory;
    FWarnings warnings;
    FRayEffectsScheduler scheduler(factory, warnings);
    FRenderFeatures features;
    FRayEffectOutputs outputs;

    features.rayTracing = false;
    features.rayTracedShadows = features.rayTracedGI =
        features.rayTracedReflections = true;
    assert(scheduler.Execute(Inputs(features),
        Selection(ERayTracingBackend::CompatibleGL33,
                  ERayTracingBackend::CompatibleGL33), outputs));

    features.rayTracing = true;
    features.rayTracedShadows = features.rayTracedGI =
        features.rayTracedReflections = false;
    assert(scheduler.Execute(Inputs(features),
        Selection(ERayTracingBackend::CompatibleGL33,
                  ERayTracingBackend::CompatibleGL33), outputs));

    assert(factory.calls == 0);
    assert(factory.initCalls == 0);
    assert(factory.renderCalls == 0);
    assert(scheduler.Stats().resourceAllocations == 0);
    assert(scheduler.Stats().sceneUploads == 0);
    assert(!outputs.shadowVisibilityTarget);
    assert(!outputs.globalIlluminationTarget);
    assert(!outputs.reflectionTarget);
}

void CheckExactChildMasksUseOneCall()
{
    const bool masks[][3] = {
        {true, false, false}, {false, true, false}, {false, false, true},
        {true, true, true},
    };
    for (const auto& mask : masks)
    {
        FFakeFactory factory;
        FWarnings warnings;
        FRayEffectsScheduler scheduler(factory, warnings);
        FRenderFeatures features;
        features.rayTracing = true;
        features.rayTracedShadows = mask[0];
        features.rayTracedGI = mask[1];
        features.rayTracedReflections = mask[2];
        FRayEffectOutputs outputs;
        assert(scheduler.Execute(Inputs(features),
            Selection(ERayTracingBackend::CompatibleGL33,
                      ERayTracingBackend::CompatibleGL33), outputs));
        assert(factory.calls == 1);
        assert(factory.initCalls == 1);
        assert(factory.renderCalls == 1);
        assert(factory.observed.rayTracedShadows == mask[0]);
        assert(factory.observed.rayTracedGI == mask[1]);
        assert(factory.observed.rayTracedReflections == mask[2]);
        assert(outputs.shadows.has_value() == mask[0]);
        assert(outputs.globalIllumination.has_value() == mask[1]);
        assert(outputs.reflections.has_value() == mask[2]);
    }
}

void CheckAutoFallsBackOnceAndForcedComputeDoesNot()
{
    FRenderFeatures features;
    features.rayTracing = true;
    features.rayTracedShadows = true;
    features.rayTracedGI = features.rayTracedReflections = false;

    {
        FFakeFactory factory;
        FWarnings warnings;
        FRayEffectsScheduler scheduler(factory, warnings);
        FRayEffectOutputs outputs;
        const FBackendSelection automatic = Selection(
            ERayTracingBackend::Auto, ERayTracingBackend::ComputeGL43);
        assert(scheduler.Execute(Inputs(features), automatic, outputs));
        assert(factory.computeCreates == 1);
        assert(factory.compatibleCreates == 1);
        assert(factory.renderCalls == 1);
        assert(warnings.messages.size() == 1);
        CheckStatus(scheduler, features, "On (Compatible)", "falling back");
        assert(scheduler.Execute(Inputs(features), automatic, outputs));
        assert(factory.computeCreates == 1);
        assert(factory.compatibleCreates == 1);
        assert(factory.renderCalls == 2);
        assert(warnings.messages.size() == 1);
        CheckStatus(scheduler, features, "On (Compatible)", "falling back");
    }

    {
        FFakeFactory factory;
        FWarnings warnings;
        FRayEffectsScheduler scheduler(factory, warnings);
        FRayEffectOutputs outputs;
        features.rayTracingBackend = ERayTracingBackend::ComputeGL43;
        const FBackendSelection forced = Selection(
            ERayTracingBackend::ComputeGL43, ERayTracingBackend::ComputeGL43);
        assert(scheduler.Execute(Inputs(features), forced, outputs));
        assert(factory.computeCreates == 1);
        assert(factory.compatibleCreates == 0);
        assert(factory.renderCalls == 0);
        assert(warnings.messages.size() == 1);
        assert(scheduler.Execute(Inputs(features), forced, outputs));
        assert(factory.computeCreates == 1);
        assert(warnings.messages.size() == 1);
        CheckStatus(scheduler, features, "Disabled", "failed to initialize");
    }
}

void CheckPermanentInitFailureIsNeutralWarnedAndLatched()
{
    FFakeFactory factory;
    factory.initResult = false;
    FWarnings warnings;
    FRayEffectsScheduler scheduler(factory, warnings);
    FRenderFeatures features;
    features.rayTracing = true;
    features.rayTracedShadows = true;
    features.rayTracedGI = features.rayTracedReflections = false;
    FRayEffectOutputs outputs;
    const FBackendSelection selection = Selection(
        ERayTracingBackend::CompatibleGL33, ERayTracingBackend::CompatibleGL33);

    assert(scheduler.Execute(Inputs(features), selection, outputs));
    assert(!outputs.shadowVisibilityTarget && !outputs.shadows);
    assert(factory.initCalls == 1 && factory.renderCalls == 0);
    assert(warnings.messages.size() == 1);
    CheckStatus(scheduler, features, "Disabled", "injected initialization failure");
    assert(scheduler.Execute(Inputs(features), selection, outputs));
    assert(factory.initCalls == 1 && factory.renderCalls == 0);
    assert(warnings.messages.size() == 1);
    CheckStatus(scheduler, features, "Disabled", "injected initialization failure");

    FRayEffectInputs nextContext = Inputs(features);
    nextContext.contextGeneration = 8;
    factory.initResult = true;
    assert(scheduler.Execute(nextContext, selection, outputs));
    assert(factory.initCalls == 2 && factory.renderCalls == 1);
}

void CheckAvailabilityChangeInvalidatesPermanentFailureLatch()
{
    FFakeFactory factory;
    FWarnings warnings;
    FRayEffectsScheduler scheduler(factory, warnings);
    FRenderFeatures features;
    features.rayTracing = true;
    features.rayTracedShadows = true;
    FRayEffectOutputs outputs;

    FBackendSelection unavailable = Selection(
        ERayTracingBackend::CompatibleGL33, ERayTracingBackend::CompatibleGL33,
        false);
    unavailable.fallbackReason = "injected capability unavailable";
    assert(scheduler.Execute(Inputs(features), unavailable, outputs));
    assert(factory.calls == 0 && factory.renderCalls == 0);
    CheckStatus(scheduler, features, "Disabled", "injected capability unavailable");
    assert(scheduler.Execute(Inputs(features), unavailable, outputs));
    CheckStatus(scheduler, features, "Disabled", "injected capability unavailable");

    const FBackendSelection available = Selection(
        ERayTracingBackend::CompatibleGL33, ERayTracingBackend::CompatibleGL33);
    assert(scheduler.Execute(Inputs(features), available, outputs));
    assert(factory.calls == 1 && factory.initCalls == 1 && factory.renderCalls == 1);
    assert(outputs.shadowVisibilityTarget || outputs.shadows);
}

void CheckTransientRenderFailureRetriesNextFrame()
{
    FFakeFactory factory;
    factory.renderFailuresRemaining = 2;
    FWarnings warnings;
    FRayEffectsScheduler scheduler(factory, warnings);
    FRenderFeatures features;
    features.rayTracing = true;
    features.rayTracedShadows = true;
    FRayEffectOutputs outputs;
    const FBackendSelection selection = Selection(
        ERayTracingBackend::CompatibleGL33, ERayTracingBackend::CompatibleGL33);

    assert(scheduler.Execute(Inputs(features), selection, outputs));
    assert(!outputs.shadowVisibilityTarget && !outputs.shadows);
    assert(factory.renderCalls == 1 && warnings.messages.size() == 1);
    assert(scheduler.ActiveKind() == ERayTracingBackend::Auto);
    const auto failureReason = scheduler.BackendReason();
    assert(failureReason.find("retaining raster") != std::string::npos);
    FWorldRendererStats display;
    display.activeRayBackend = scheduler.ActiveKind(); display.backendReason = failureReason;
    assert(DescribeRayTracingStatus(features, display).find("Disabled") != std::string::npos);
    assert(scheduler.Execute(Inputs(features), selection, outputs));
    assert(factory.renderCalls == 2);
    assert(scheduler.BackendReason() == failureReason && warnings.messages.size() == 1);
    CheckStatus(scheduler, features, "Disabled", "retaining raster");
    assert(scheduler.Execute(Inputs(features), selection, outputs));
    assert(factory.renderCalls == 3);
    display.activeRayBackend = scheduler.ActiveKind();
    assert(DescribeRayTracingStatus(features, display).find("On (Compatible)") != std::string::npos);
    assert(outputs.shadowVisibilityTarget || outputs.shadows);
    assert(warnings.messages.size() == 1);
}
} // namespace

int main()
{
    CheckNoWorkCombinations();
    CheckExactChildMasksUseOneCall();
    CheckAutoFallsBackOnceAndForcedComputeDoesNot();
    CheckPermanentInitFailureIsNeutralWarnedAndLatched();
    CheckAvailabilityChangeInvalidatesPermanentFailureLatch();
    CheckTransientRenderFailureRetriesNextFrame();
    std::cout << "RayEffectsSchedulingTest passed\n";
    return 0;
}
