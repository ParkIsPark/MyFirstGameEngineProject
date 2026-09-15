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
#include "Shaders/SharedLightingShaderSource.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

static void Require(bool value, const char* message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

static std::string ReadSource(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    Require(bool(input), path);
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

static std::size_t CountText(const std::string& text, const std::string& needle)
{
    std::size_t count = 0;
    for (std::size_t offset = 0;
         (offset = text.find(needle, offset)) != std::string::npos;
         offset += needle.size())
        ++count;
    return count;
}

static std::string FunctionBody(const std::string& source, const char* marker)
{
    const std::size_t signature = source.find(marker);
    Require(signature != std::string::npos, marker);
    const std::size_t open = source.find('{', signature);
    Require(open != std::string::npos, marker);
    int depth = 0;
    for (std::size_t offset = open; offset < source.size(); ++offset)
    {
        if (source[offset] == '{') ++depth;
        else if (source[offset] == '}' && --depth == 0)
            return source.substr(open, offset - open + 1u);
    }
    Require(false, marker);
    return {};
}

static void CheckFinalRendererSourceInvariants()
{
    const std::string shared = ReadSource(
        "Engine/Render/Shaders/SharedLightingShaderSource.h");
    const std::string hardware = ReadSource(
        "Engine/Render/Shaders/HardwareRasterShaders.h");
    const std::string raster = ReadSource(
        "Engine/Render/Shaders/RasterLightingShaders.h");
    const std::string fragment = ReadSource(
        "Engine/Render/Shaders/RayEffectsFragmentShaders.h");
    const std::string compute = ReadSource(
        "Engine/Render/Shaders/RayEffectsComputeShaders.h");
    const std::string evaluator = SharedLightingShaderSource::PointLightFunctions();
    const std::string generatedHardware =
        SharedLightingShaderSource::BuildHardwareGeometryShader();
    const std::string generatedRaster =
        SharedLightingShaderSource::BuildRasterLightingFragmentShader();
    const std::string generatedFragment =
        SharedLightingShaderSource::BuildRayEffectsFragmentShader();
    const std::string generatedCompute =
        SharedLightingShaderSource::BuildRayEffectsComputeShader();
    Require(generatedHardware == std::string(HardwareRasterShaders::GeometryBeforePointLight) +
            evaluator + HardwareRasterShaders::GeometryAfterPointLight &&
        generatedRaster == std::string(RasterLightingShaders::LightingBeforePointLight) +
            evaluator + RasterLightingShaders::LightingAfterPointLight &&
        generatedFragment == std::string(RayEffectsFragmentShaders::EffectsBeforePointLight) +
            evaluator + RayEffectsFragmentShaders::EffectsAfterPointLight &&
        generatedCompute == std::string(RayEffectsComputeShaders::EffectsBeforePointLight) +
            evaluator + RayEffectsComputeShaders::EffectsAfterPointLight &&
        CountText(generatedHardware, "void evaluatePointLight(") == 1 &&
        CountText(generatedRaster, "void evaluatePointLight(") == 1 &&
        CountText(generatedFragment, "void evaluatePointLight(") == 1 &&
        CountText(generatedCompute, "void evaluatePointLight(") == 1 &&
        CountText(generatedHardware, "lightSource / distanceSquared") == 1 &&
        CountText(generatedRaster, "lightSource / distanceSquared") == 1 &&
        CountText(generatedFragment, "lightSource / distanceSquared") == 1 &&
        CountText(generatedCompute, "lightSource / distanceSquared") == 1 &&
        CountText(shared, "void evaluatePointLight(") == 1 &&
        CountText(shared, "PointLightFunctions()") == 5 &&
        hardware.find("void evaluatePointLight(") == std::string::npos &&
        raster.find("void evaluatePointLight(") == std::string::npos &&
        fragment.find("void evaluatePointLight(") == std::string::npos &&
        compute.find("void evaluatePointLight(") == std::string::npos,
        "all raster/ray shaders consume one shared point-light implementation");

    const std::string presentation = ReadSource(
        "Engine/Render/Shaders/HybridPresentationShaders.h");
    Require(CountText(presentation, "vec3 linearToSRGB(") == 1 &&
        CountText(presentation, "linearToSRGB(mapped)") == 1 &&
        hardware.find("linearToSRGB") == std::string::npos &&
        raster.find("linearToSRGB") == std::string::npos &&
        fragment.find("linearToSRGB") == std::string::npos &&
        compute.find("linearToSRGB") == std::string::npos,
        "presentation owns the single linear-to-sRGB conversion");

    const std::string rasterizerCpp = ReadSource("Engine/Render/UHardwareRasterizer.cpp");
    const std::string lightingCpp = ReadSource("Engine/Render/URasterLightingPass.cpp");
    const std::string presentationCpp = ReadSource("Engine/Render/UHybridPresentationPass.cpp");
    const std::string gl33Cpp = ReadSource("Engine/Render/UGL33RayTracingBackend.cpp");
    const std::string gl43Cpp = ReadSource("Engine/Render/UGL43RayTracingBackend.cpp");
    const std::string reconstructionCpp = ReadSource("Engine/Render/URayEffectsReconstruction.cpp");
    const std::string renderSources = rasterizerCpp + lightingCpp + presentationCpp +
        gl33Cpp + gl43Cpp + reconstructionCpp;
    const std::string rasterDraw = FunctionBody(rasterizerCpp,
        "bool UHardwareRasterizer::RenderGeometry(const FRenderScene& scene,\n                                         const FRenderQuality& quality");
    const std::string lightingDraw = FunctionBody(lightingCpp,
        "void ConfigureFullscreenState()");
    const std::string presentationDraw = FunctionBody(presentationCpp,
        "void ConfigureFullscreenState()");
    const std::string gl33Draw = FunctionBody(gl33Cpp,
        "bool UGL33RayTracingBackend::RenderEffects(");
    const std::string reconstructionDraw = FunctionBody(reconstructionCpp,
        "bool URayEffectsReconstruction::Reconstruct(");
    const std::string rasterRestore = FunctionBody(rasterizerCpp, "void RestoreGeometryState(");
    const std::string lightingRestore = FunctionBody(lightingCpp, "void RestoreState(");
    const std::string presentationRestore = FunctionBody(presentationCpp, "void RestoreState(");
    const std::string gl33Restore = FunctionBody(gl33Cpp, "void RestoreState(");
    const std::string reconstructionRestore = FunctionBody(reconstructionCpp, "~FState()");
    const auto operationalDisablesSRGB = [](const std::string& function) {
        return CountText(function, "glDisable(GL_FRAMEBUFFER_SRGB)") == 1 &&
            function.find("glEnable(GL_FRAMEBUFFER_SRGB)") == std::string::npos;
    };
    const auto restoresSRGBBothWays = [](const std::string& function) {
        return CountText(function, "glEnable(GL_FRAMEBUFFER_SRGB)") == 1 &&
            CountText(function, "glDisable(GL_FRAMEBUFFER_SRGB)") == 1;
    };
    Require(operationalDisablesSRGB(rasterDraw) &&
        operationalDisablesSRGB(lightingDraw) &&
        operationalDisablesSRGB(presentationDraw) &&
        operationalDisablesSRGB(gl33Draw) &&
        operationalDisablesSRGB(reconstructionDraw) &&
        restoresSRGBBothWays(rasterRestore) &&
        restoresSRGBBothWays(lightingRestore) &&
        restoresSRGBBothWays(presentationRestore) &&
        restoresSRGBBothWays(gl33Restore) &&
        restoresSRGBBothWays(reconstructionRestore) &&
        gl43Cpp.find("glEnable(GL_FRAMEBUFFER_SRGB)") == std::string::npos &&
        gl43Cpp.find("glDisable(GL_FRAMEBUFFER_SRGB)") == std::string::npos &&
        renderSources.find("glMatrixMode") == std::string::npos &&
        renderSources.find("glOrtho") == std::string::npos &&
        renderSources.find("glBegin(") == std::string::npos &&
        renderSources.find("glDrawPixels") == std::string::npos &&
        renderSources.find("glEnable(GL_LIGHTING)") == std::string::npos,
        "each draw function disables framebuffer sRGB independently of restoration and no fixed-function path returns");

    const std::string hardwareInit = FunctionBody(rasterizerCpp,
        "bool UHardwareRasterizer::Init(");
    const std::string lightingInit = FunctionBody(lightingCpp,
        "bool URasterLightingPass::Init(");
    const std::string gl33Program = FunctionBody(gl33Cpp, "bool BuildProgram(");
    const std::string gl43Program = FunctionBody(gl43Cpp, "bool CompileComputeProgram(");
    Require(CountText(hardwareInit,
                "SharedLightingShaderSource::BuildHardwareGeometryShader()") == 1 &&
        CountText(lightingInit,
                "SharedLightingShaderSource::BuildRasterLightingFragmentShader()") == 1 &&
        CountText(gl33Program,
                "SharedLightingShaderSource::BuildRayEffectsFragmentShader()") == 1 &&
        CountText(gl43Program,
                "SharedLightingShaderSource::BuildRayEffectsComputeShader()") == 1 &&
        CountText(rasterizerCpp, "BuildHardwareGeometryShader()") == 1 &&
        CountText(lightingCpp, "BuildRasterLightingFragmentShader()") == 1 &&
        CountText(gl33Cpp, "BuildRayEffectsFragmentShader()") == 1 &&
        CountText(gl43Cpp, "BuildRayEffectsComputeShader()") == 1,
        "each production shader initialization calls its one intended shared-lighting builder");

    const std::string outputs = ReadSource("Engine/Render/FRenderOutputs.h");
    const std::string hybrid = ReadSource(
        "Engine/Render/Shaders/HybridPresentationShaders.h");
    Require(outputs.find("shadowVisibilityTarget") == std::string::npos &&
        hybrid.find("shadowVisibilityTarget") == std::string::npos &&
        hybrid.find("raster * visibility") == std::string::npos,
        "shadows cannot regress to a whole-raster visibility multiply");

    const std::string geometry = ReadSource("Engine/Render/UHardwareRasterizer.cpp");
    Require(geometry.find("uEnvironmentTint") == std::string::npos &&
        geometry.find("uSkyHorizon") == std::string::npos &&
        geometry.find("uSkyZenith") == std::string::npos &&
        geometry.find("uSkyExponent") == std::string::npos &&
        geometry.find("uAmbientStrength") == std::string::npos &&
        geometry.find("uHasEnvironmentTexture") == std::string::npos &&
        geometry.find("uEnvironmentTexture") == std::string::npos,
        "geometry pass does not upload obsolete environment/ambient uniforms");

    const std::string rayCache = ReadSource("Engine/RayTracing/FRaySceneCache.cpp");
    Require(rasterizerCpp.find("material.texData.data()") == std::string::npos &&
        rayCache.find("HashBytes(hash, source.texData.data()") == std::string::npos,
        "steady material signatures never scan texture payload bytes");
    Require(CountText(rasterizerCpp, "ResampleRGBA8ToLayer(") == 1 &&
        CountText(rayCache, "ResampleRGBA8ToLayer(") == 1,
        "raster uploads and ray layers share one 1/2/3/4-channel canonicalizer");
    Require(hardware.find("texture(uDiffuseTexture") ==
            hardware.find("inline constexpr const char* Fragment") +
                hardware.substr(hardware.find("inline constexpr const char* Fragment"))
                    .find("texture(uDiffuseTexture") &&
        hardware.substr(0, hardware.find("inline constexpr const char* Fragment"))
            .find("texture(uDiffuseTexture") == std::string::npos,
        "mip-filtered albedo is sampled only in the fragment stage");
    Require(CountText(fragment, "for(int internalBounce=0;internalBounce<4;") == 1 &&
        CountText(compute, "for(int internalBounce=0;internalBounce<4;") == 1 &&
        CountText(fragment, "reflect(insideDirection,exitNormal)") == 1 &&
        CountText(compute, "reflect(insideDirection,exitNormal)") == 1,
        "exit-boundary TIR continues inside the closed material with a finite budget");

    const std::string gl33 = ReadSource("Engine/Render/UGL33RayTracingBackend.cpp");
    const std::string gl33Header = ReadSource("Engine/Render/UGL33RayTracingBackend.h");
    Require(gl33.find("instanceIdentityTexels") == std::string::npos &&
        gl33.find("instanceIdentityBuffer_") == std::string::npos &&
        gl33.find("instanceIdentityTexture_") == std::string::npos &&
        gl33Header.find("instanceIdentityBuffer_") == std::string::npos &&
        gl33Header.find("instanceIdentityTexture_") == std::string::npos,
        "GL3.3 identity uses packed instance texel 3 without a redundant typed TBO");
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
    CheckFinalRendererSourceInvariants();
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
