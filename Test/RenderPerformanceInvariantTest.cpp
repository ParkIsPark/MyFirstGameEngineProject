// Current complete recipe: powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1 -Name RenderPerformanceInvariantTest
// Prerequisite: Engine.sln Debug|Win32. Older direct compile examples follow.
// Build Engine.sln Debug|Win32 first, then from the repository root in PowerShell:
// $includes = (Get-ChildItem Engine -Directory -Recurse).FullName | ForEach-Object { '/I"' + $_ + '"' }
// $cmd = 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 >nul && cl /nologo /std:c++17 /EHsc /MDd /DWIN32 /D_DEBUG /Iinclude ' + ($includes -join ' ') + ' Test\RenderPerformanceInvariantTest.cpp /Fe:RenderPerformanceInvariantTest.exe bin\Engine.lib /link /LIBPATH:lib glew32.lib freeglut.lib glfw3dll.lib opengl32.lib glu32.lib assimp-vc143-mt.lib'; & cmd.exe /d /c $cmd
// $env:Path = "$PWD\bin;$env:Path"; .\RenderPerformanceInvariantTest.exe
// Real GL companion (bounded): .\bin\Test.exe --render-performance-selftest
// Breaks caught: per-instance geometry uploads, work with RT master off, and
// loss of cumulative ray allocation/upload totals when switching backends.
#include "AActor.h"
#include "UMeshComponent.h"
#include "UWorld.h"
#include "UGPUMeshCache.h"
#include "FRenderScene.h"
#include "FRenderPipelinePlan.h"
#include "IRayTracingBackend.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>

static void Require(bool value, const char* message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

class UploadAdapter final : public IMeshGPUUploadAdapter
{
public:
    unsigned uploads = 0, reuploads = 0, live = 0;
    std::uint64_t ActiveContextGeneration() const noexcept override { return 1; }
    FGPUMeshResource CreateAndUpload(const FMeshGPUUploadView& view, std::uint64_t generation) override
    {
        Require(view.vertexCount == 24 && view.indexCount == 36, "cube geometry upload is complete");
        ++uploads; ++live;
        FGPUMeshResource result; result.vao = uploads; result.contextGeneration = generation; return result;
    }
    bool Reupload(FGPUMeshResource&, const FMeshGPUUploadView&, std::string&) noexcept override { ++reuploads; return true; }
    void Destroy(FGPUMeshResource& resource) noexcept override { --live; resource = {}; }
    void Abandon(FGPUMeshResource& resource) noexcept override { Destroy(resource); }
};

class AllocationBackend final : public IRayTracingBackend
{
public:
    explicit AllocationBackend(bool succeeds = true) : succeeds(succeeds) {}
    bool succeeds;
    FRayTracingBackendStats stats;
    bool Init(std::uint64_t, std::string* diagnostic) override
    { ++stats.resourceAllocations; if (!succeeds && diagnostic) *diagnostic = "deliberate initialization failure"; return succeeds; }
    bool RenderEffects(const FRayEffectInputs&, FRayEffectOutputs&, std::string*) override
    { ++stats.sceneUploads; return true; }
    void Shutdown() noexcept override {}
    const FRayTracingBackendStats& Stats() const override { return stats; }
};
class Factory final : public IRayTracingBackendFactory
{
public:
    bool failCompute = false;
    std::unique_ptr<IRayTracingBackend> Create(ERayTracingBackend kind, std::string*) override
    { return std::make_unique<AllocationBackend>(!failCompute || kind != ERayTracingBackend::ComputeGL43); }
};
class Warnings final : public IRayEffectsWarningSink
{
public:
    void Warn(const std::string&) override {}
};

int main()
{
    for (int count : {1, 100, 1000})
    {
        std::unique_ptr<UMesh> cube(UMesh::GenerateCube(glm::vec3(1.0f)));
        UWorld world;
        for (int i = 0; i < count; ++i)
        {
            auto* actor = new AActor(); auto* component = new UMeshComponent();
            component->mesh = cube.get(); actor->SetMesh(component);
            actor->SetActorLocation(glm::vec3(float(i % 32) * 0.05f, float(i / 32) * 0.05f, -5.0f));
            world.Spawn(actor);
        }
        UploadAdapter adapter;
        UGPUMeshCache cache(adapter);
        for (int frame = 0; frame < 4; ++frame)
        {
            if (frame == 2) world.GetScene().Actors.front()->SetActorLocation(glm::vec3(0, 0, -6));
            if (frame == 3) cube->MarkGeometryDirty();
            const auto scene = ExtractRenderScene(world, world.GetCamera());
            Require(scene.meshes.size() == std::size_t(count), "one extracted instance per Actor");
            cache.BeginFrame();
            for (const auto& instance : scene.meshes) cache.Acquire(*instance.mesh, 1);
            cache.ReleaseUnused();
            Require(cache.Stats().uploads == 1 && adapter.uploads == 1, "one geometry upload across all shared instances");
            Require(cache.Stats().reuploads == (frame == 3 ? 1u : 0u) && adapter.reuploads == (frame == 3 ? 1u : 0u), "unchanged/transform/revision upload deltas");
            Require(cache.Stats().residentResources == 1 && adapter.live == 1, "one resident geometry resource");
        }
        cache.Clear(); Require(adapter.live == 0 && cache.Stats().residentResources == 0, "geometry shutdown releases ownership");
        FRenderFeatures off; off.rayTracing = false;
        const auto plan = BuildRenderPipelinePlan(off);
        Require(std::find(plan.begin(), plan.end(), ERenderPass::RayTracedEffects) == plan.end(), "RT-off plan has no ray pass");
    }
    Factory factory; Warnings warnings; FRayEffectsScheduler scheduler(factory, warnings);
    FRayEffectInputs inputs; inputs.contextGeneration = 1;
    FBackendSelection selection; selection.available = true; selection.rayTracingEnabled = true;
    selection.requested = selection.selected = ERayTracingBackend::CompatibleGL33;
    FRayEffectOutputs outputs;
    scheduler.Execute(inputs, selection, outputs);
    Require(scheduler.Stats().factoryCalls == 0 && scheduler.Stats().resourceAllocations == 0 && scheduler.Stats().backendCalls == 0, "RT master off performs no backend work");
    inputs.features.rayTracing = true;
    scheduler.Execute(inputs, selection, outputs);
    Require(scheduler.Stats().resourceAllocations == 1, "first backend allocation is observable");
    selection.requested = selection.selected = ERayTracingBackend::ComputeGL43;
    scheduler.Execute(inputs, selection, outputs);
    Require(scheduler.Stats().resourceAllocations == 2 && scheduler.Stats().sceneUploads == 2, "ray allocation/upload totals remain cumulative across backend replacement");
    scheduler.Shutdown();
    factory.failCompute = true;
    scheduler.Execute(inputs, selection, outputs);
    Require(scheduler.Stats().resourceAllocations == 3 && scheduler.ActiveKind() == ERayTracingBackend::Auto &&
        scheduler.BackendReason().find("deliberate initialization failure") != std::string::npos,
        "failed initialization retains allocation work and meaningful reason");
    inputs.features.rayTracing = false; scheduler.Execute(inputs, selection, outputs);
    inputs.features.rayTracing = true; scheduler.Execute(inputs, selection, outputs);
    Require(scheduler.BackendReason().find("deliberate initialization failure") != std::string::npos,
        "latched failure reason survives RT off/on");
    selection.requested = ERayTracingBackend::Auto;
    scheduler.Execute(inputs, selection, outputs);
    Require(scheduler.ActiveKind() == ERayTracingBackend::CompatibleGL33 &&
        scheduler.BackendReason().find("falling back") != std::string::npos, "automatic initialization fallback is observable");
    inputs.features.rayTracing = false; scheduler.Execute(inputs, selection, outputs);
    inputs.features.rayTracing = true; scheduler.Execute(inputs, selection, outputs);
    Require(scheduler.BackendReason().find("falling back") != std::string::npos,
        "successful latched fallback reason survives RT off/on");
    std::cout << "RenderPerformanceInvariantTest passed (1/100/1000 shared Actors and scheduler lifetime totals)\n";
}
