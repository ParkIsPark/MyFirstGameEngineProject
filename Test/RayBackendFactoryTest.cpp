// & 'C:\msys64\ucrt64\bin\g++.exe' -std=c++17 -Wall -Wextra -pedantic -I./include -I./Engine/Render -I./Engine/RayTracing Test/RayBackendFactoryTest.cpp Engine/Render/FGraphicsCapabilities.cpp Engine/Render/IRayTracingBackend.cpp Engine/Render/FGL43ComputeApi.cpp -o RayBackendFactoryTest.exe; if ($LASTEXITCODE -eq 0) { .\RayBackendFactoryTest.exe }
#include "FGL43ComputeApi.h"
#include "FGraphicsCapabilities.h"
#include "IRayTracingBackend.h"
#include "UGL33RayTracingBackend.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
FGraphicsCapabilities Capabilities(int major, int minor, bool compute)
{
    FGraphicsCapabilities value;
    value.major = major;
    value.minor = minor;
    value.computeShaders = compute;
    value.shaderStorageBuffers = compute;
    value.requiredComputeEntryPoints = compute;
    return value;
}

void CheckSyntheticSelectionMatrix()
{
    const FGraphicsCapabilities gl33 = Capabilities(3, 3, false);
    const FGraphicsCapabilities gl43 = Capabilities(4, 3, true);

    FBackendSelection selected = SelectRayTracingBackend(
        ERayTracingBackend::Auto, gl33);
    assert(selected.available && selected.rayTracingEnabled);
    assert(selected.selected == ERayTracingBackend::CompatibleGL33);

    selected = SelectRayTracingBackend(ERayTracingBackend::Auto, gl43);
    assert(selected.available && selected.rayTracingEnabled);
    assert(selected.selected == ERayTracingBackend::ComputeGL43);

    selected = SelectRayTracingBackend(
        ERayTracingBackend::CompatibleGL33, gl43);
    assert(selected.available && selected.rayTracingEnabled);
    assert(selected.selected == ERayTracingBackend::CompatibleGL33);

    selected = SelectRayTracingBackend(
        ERayTracingBackend::ComputeGL43, gl33);
    assert(!selected.available && !selected.rayTracingEnabled);
    assert(selected.selected == ERayTracingBackend::ComputeGL43);
}

class FCompatibleBackend final : public IRayTracingBackend
{
public:
    bool Init(std::uint64_t, std::string*) override { return true; }
    bool RenderEffects(const FRayEffectInputs&, FRayEffectOutputs&,
                       std::string*) override { return true; }
    void Shutdown() noexcept override {}
    const FRayTracingBackendStats& Stats() const override { return stats_; }
private:
    FRayTracingBackendStats stats_;
};

class FComputeBackend final : public IRayTracingBackend
{
public:
    bool Init(std::uint64_t, std::string*) override { return true; }
    bool RenderEffects(const FRayEffectInputs&, FRayEffectOutputs&,
                       std::string*) override { return true; }
    void Shutdown() noexcept override {}
    const FRayTracingBackendStats& Stats() const override { return stats_; }
private:
    FRayTracingBackendStats stats_;
};

void CheckProductionFactoryRoutesDistinctRegisteredBackends()
{
    FOpenGLRayTracingBackendFactory factory(
        [] { return std::make_unique<FCompatibleBackend>(); },
        [] { return std::make_unique<FComputeBackend>(); });
    std::string diagnostic;
    std::unique_ptr<IRayTracingBackend> compatible = factory.Create(
        ERayTracingBackend::CompatibleGL33, &diagnostic);
    assert(compatible);
    assert(dynamic_cast<FCompatibleBackend*>(compatible.get()));

    std::unique_ptr<IRayTracingBackend> compute = factory.Create(
        ERayTracingBackend::ComputeGL43, &diagnostic);
    assert(compute);
    assert(dynamic_cast<FComputeBackend*>(compute.get()));
    assert(typeid(*compatible) != typeid(*compute));
}

class FFakeProcSource final : public IGL43ProcAddressSource
{
public:
    FGL43GenericProc Resolve(const char* name) const override
    {
        const auto found = entries.find(name);
        return found == entries.end() ? nullptr : found->second;
    }
    std::unordered_map<std::string, FGL43GenericProc> entries;
};

void StubProc() {}

void CheckLoaderNamesMissingEntryPoint()
{
    FFakeProcSource source;
    for (const char* name : FGL43ComputeApi::RequiredEntryPointNames())
        source.entries.emplace(name, &StubProc);
    source.entries.erase("glDispatchCompute");

    FGL43ComputeApi api;
    std::string diagnostic;
    assert(!api.Load(source, &diagnostic));
    assert(diagnostic.find("glDispatchCompute") != std::string::npos);
    assert(!api.IsLoaded());

    source.entries.clear();
    for (const char* name : FGL43ComputeApi::RequiredEntryPointNames())
        source.entries.emplace(name, &StubProc);
    source.entries.erase("glGetInteger64v");
    diagnostic.clear();
    assert(!api.Load(source, &diagnostic));
    assert(diagnostic.find("glGetInteger64v") != std::string::npos);
    assert(!api.IsLoaded());
}

class FFakeBackend final : public IRayTracingBackend
{
public:
    FFakeBackend(bool init, int& initCalls, int& renderCalls)
        : init_(init), initCalls_(initCalls), renderCalls_(renderCalls) {}
    bool Init(std::uint64_t, std::string* diagnostic) override
    {
        ++initCalls_;
        if (!init_ && diagnostic) *diagnostic = "injected compute init failure";
        return init_;
    }
    bool RenderEffects(const FRayEffectInputs& inputs, FRayEffectOutputs& outputs,
                       std::string*) override
    {
        ++renderCalls_;
        if (inputs.features.rayTracedShadows) outputs.shadows = 0.5f;
        return true;
    }
    void Shutdown() noexcept override {}
    const FRayTracingBackendStats& Stats() const override { return stats_; }
private:
    bool init_;
    int& initCalls_;
    int& renderCalls_;
    FRayTracingBackendStats stats_;
};

class FFakeFactory final : public IRayTracingBackendFactory
{
public:
    explicit FFakeFactory(bool computeInit = false) : computeInit_(computeInit) {}

    std::unique_ptr<IRayTracingBackend> Create(
        ERayTracingBackend backend, std::string*) override
    {
        if (backend == ERayTracingBackend::ComputeGL43)
        {
            ++computeCreates;
            return std::make_unique<FFakeBackend>(computeInit_, computeInits,
                                                  computeRenders);
        }
        ++compatibleCreates;
        return std::make_unique<FFakeBackend>(true, compatibleInits,
                                              compatibleRenders);
    }
    int computeCreates = 0, compatibleCreates = 0;
    int computeInits = 0, compatibleInits = 0;
    int computeRenders = 0, compatibleRenders = 0;
private:
    bool computeInit_ = false;
};

class FWarnings final : public IRayEffectsWarningSink
{
public:
    void Warn(const std::string& value) override { messages.push_back(value); }
    std::vector<std::string> messages;
};

FRayEffectInputs Inputs(bool master, bool shadows)
{
    FRayEffectInputs inputs;
    inputs.contextGeneration = 41;
    inputs.width = inputs.height = 8;
    inputs.features.rayTracing = master;
    inputs.features.rayTracedShadows = shadows;
    inputs.features.rayTracedGI = false;
    inputs.features.rayTracedReflections = false;
    return inputs;
}

FBackendSelection Selection(ERayTracingBackend requested)
{
    FBackendSelection value;
    value.requested = requested;
    value.selected = ERayTracingBackend::ComputeGL43;
    value.available = true;
    value.rayTracingEnabled = true;
    return value;
}

void CheckSchedulerFallbackAndLazyWork()
{
    {
        FFakeFactory factory;
        FWarnings warnings;
        FRayEffectsScheduler scheduler(factory, warnings);
        FRayEffectOutputs outputs;
        assert(scheduler.Execute(Inputs(false, true),
                                 Selection(ERayTracingBackend::Auto), outputs));
        assert(scheduler.Execute(Inputs(true, false),
                                 Selection(ERayTracingBackend::Auto), outputs));
        assert(factory.computeCreates == 0 && factory.compatibleCreates == 0);
    }

    {
        FFakeFactory factory(true);
        FWarnings warnings;
        FRayEffectsScheduler scheduler(factory, warnings);
        FRayEffectOutputs outputs;
        FBackendSelection compatible = Selection(
            ERayTracingBackend::CompatibleGL33);
        compatible.selected = ERayTracingBackend::CompatibleGL33;
        assert(scheduler.Execute(Inputs(true, true), compatible, outputs));
        assert(factory.compatibleCreates == 1 && factory.compatibleRenders == 1);
        assert(scheduler.ActiveKind() == ERayTracingBackend::CompatibleGL33);

        const FBackendSelection automatic = Selection(
            ERayTracingBackend::Auto);
        assert(scheduler.Execute(Inputs(true, true), automatic, outputs));
        assert(factory.computeCreates == 1 && factory.computeInits == 1);
        assert(factory.computeRenders == 1);
        assert(factory.compatibleRenders == 1);
        assert(scheduler.ActiveKind() == ERayTracingBackend::ComputeGL43);
        assert(warnings.messages.empty());
    }

    {
        FFakeFactory factory;
        FWarnings warnings;
        FRayEffectsScheduler scheduler(factory, warnings);
        FRayEffectOutputs outputs;
        const FBackendSelection automatic = Selection(ERayTracingBackend::Auto);
        assert(scheduler.Execute(Inputs(true, true), automatic, outputs));
        assert(scheduler.Execute(Inputs(true, true), automatic, outputs));
        assert(factory.computeCreates == 1 && factory.computeInits == 1);
        assert(factory.compatibleCreates == 1 && factory.compatibleInits == 1);
        assert(factory.compatibleRenders == 2);
        assert(scheduler.ActiveKind() == ERayTracingBackend::CompatibleGL33);
        assert(warnings.messages.size() == 1);
    }

    {
        FFakeFactory factory;
        FWarnings warnings;
        FRayEffectsScheduler scheduler(factory, warnings);
        FRayEffectOutputs outputs;
        const FBackendSelection forced = Selection(
            ERayTracingBackend::ComputeGL43);
        assert(scheduler.Execute(Inputs(true, true), forced, outputs));
        assert(scheduler.Execute(Inputs(true, true), forced, outputs));
        assert(factory.computeCreates == 1 && factory.computeInits == 1);
        assert(factory.compatibleCreates == 0 && factory.compatibleRenders == 0);
        assert(!outputs.shadowVisibilityTarget && !outputs.shadows);
        assert(scheduler.ActiveKind() == ERayTracingBackend::Auto);
        assert(warnings.messages.size() == 1);
    }
}
} // namespace

int main()
{
    CheckSyntheticSelectionMatrix();
    CheckProductionFactoryRoutesDistinctRegisteredBackends();
    CheckSchedulerFallbackAndLazyWork();
    CheckLoaderNamesMissingEntryPoint();
    std::cout << "RayBackendFactoryTest passed\n";
    return 0;
}
