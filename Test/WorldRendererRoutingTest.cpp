// Current complete recipe: powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1 -Name WorldRendererRoutingTest
// Prerequisite: Engine.sln Debug|Win32. Older direct compile examples follow.
// Build Engine.sln Debug|Win32 first, then from the repository root:
// $includes = (Get-ChildItem Engine -Directory -Recurse).FullName | ForEach-Object { '/I"' + $_ + '"' }
// $cmd = 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 >nul && cl /nologo /std:c++17 /EHsc /MDd /DWIN32 /D_DEBUG /Iinclude ' + ($includes -join ' ') + ' Test\WorldRendererRoutingTest.cpp /Fe:WorldRendererRoutingTest.exe bin\Engine.lib /link /LIBPATH:lib glew32.lib freeglut.lib glfw3dll.lib opengl32.lib glu32.lib assimp-vc143-mt.lib'; & cmd.exe /d /c $cmd
// $env:Path = "$PWD\bin;$env:Path"; .\WorldRendererRoutingTest.exe
#include "FRenderOutputs.h"
#include "FRenderScene.h"
#include "FRenderTarget.h"
#include "FDeprecatedWorldRenderExecutor.h"
#include "FTransform.h"
#include "UGBuffer.h"
#include "URasterizer.h"
#include "UWorldRenderer.h"

#include "AActor.h"
#include "ACamera.h"
#include "ALight.h"
#include "EnvironmentLightComponent.h"
#include "PointLightComponent.h"
#include "UMesh.h"
#include "UMeshComponent.h"
#include "UWorld.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
template <typename T, typename = void>
struct THasWorldMember : std::false_type {};

template <typename T>
struct THasWorldMember<T, std::void_t<decltype(std::declval<T>().world)>>
    : std::true_type {};

template <typename T, typename = void>
struct THasCameraMember : std::false_type {};

template <typename T>
struct THasCameraMember<T, std::void_t<decltype(std::declval<T>().camera)>>
    : std::true_type {};

static_assert(!THasWorldMember<FWorldRenderRequest>::value,
              "common render requests must not expose the live world");
static_assert(!THasCameraMember<FWorldRenderRequest>::value,
              "common render requests must not expose the live camera");

bool Near(float lhs, float rhs)
{
    return std::fabs(lhs - rhs) < 0.0001f;
}

void Require(bool condition, const std::string& message)
{
    if (condition) return;
    std::cerr << "WorldRendererRoutingTest failure: " << message << '\n';
    std::exit(1);
}

std::string ReadSource(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input && "routing topology source is missing");
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

std::size_t CountOccurrences(const std::string& text, const char* needle)
{
    std::size_t count = 0;
    for (std::size_t offset = 0;
         (offset = text.find(needle, offset)) != std::string::npos;
         offset += std::char_traits<char>::length(needle))
        ++count;
    return count;
}

void CheckNormalRouteSourceIsolation()
{
    const std::string editor = ReadSource("Engine/Editor/EditorEngine.cpp");
    const std::string game = ReadSource("Engine/Framework/GameEngine.cpp");
    const std::string renderer = ReadSource("Engine/Render/UWorldRenderer.cpp");
    const std::string engine = ReadSource("Engine/Framework/Engine.cpp");
    const std::string deprecatedHeader =
        ReadSource("Engine/Render/FDeprecatedWorldRenderExecutor.h");
    const std::string deprecatedSource =
        ReadSource("Engine/Render/FDeprecatedWorldRenderExecutor.cpp");
    const std::string softwareHeader = ReadSource("Engine/Render/URenderer.h");
    const std::string pureGPUHeader =
        ReadSource("Engine/RayTracing/UMeshRayTracer.h");

    assert(CountOccurrences(editor, "worldRenderer_.Render(") == 1);
    assert(CountOccurrences(game, "worldRenderer_.Render(") == 1);

    const char* duplicateRouteSymbols[] = {
        "RenderWorldGPU", "renderMode_", "vpTex_", "glDrawPixels",
        "RasterShadedLegacyOutput", "UHybridPass", "FLegacyWorldRenderExecutor",
        "CPU framebuffer", "ReadPixels", "glReadPixels", "glTexImage",
        "glTexSubImage",
    };
    for (const char* symbol : duplicateRouteSymbols)
    {
        Require(editor.find(symbol) == std::string::npos,
                std::string("Editor normal route contains ") + symbol);
        Require(game.find(symbol) == std::string::npos,
                std::string("Game normal route contains ") + symbol);
        Require(renderer.find(symbol) == std::string::npos,
                std::string("UWorldRenderer normal route contains ") + symbol);
    }

    const char* legacyDependencies[] = {
        "ThreadPool.h", "UGBuffer.h", "URasterizer.h", "URenderer.h",
        "USkyHDRI.h",
    };
    for (const char* dependency : legacyDependencies)
        assert(renderer.find(dependency) == std::string::npos);

    assert(engine.find("glDrawPixels") == std::string::npos);
    assert(engine.find("glMatrixMode") == std::string::npos);
    assert(engine.find("glOrtho") == std::string::npos);

    assert(deprecatedHeader.find("CreateDeprecatedWorldRenderExecutor") !=
           std::string::npos);
    assert(deprecatedSource.find("FDeprecatedSoftwareRasterExecutor") !=
           std::string::npos);
    assert(deprecatedSource.find("URenderer") != std::string::npos);
    assert(deprecatedSource.find("FDeprecatedPureGPURayTracerExecutor") !=
           std::string::npos);
    assert(deprecatedSource.find("UMeshRayTracer") != std::string::npos);
    assert(softwareHeader.find("ENGINE_DEPRECATED") != std::string::npos);
    assert(softwareHeader.find("hardware raster") != std::string::npos);
    assert(pureGPUHeader.find("ENGINE_DEPRECATED") != std::string::npos);
    assert(pureGPUHeader.find("hardware raster plus ray-traced effects") !=
           std::string::npos);
}

void CheckDeprecatedOverrideFactoryIsExplicitAndLazy()
{
    std::unique_ptr<IDeprecatedWorldRenderExecutor> none =
        CreateDeprecatedWorldRenderExecutor(ELegacyRendererOverride::None);
    assert(!none);

    std::unique_ptr<IDeprecatedWorldRenderExecutor> software =
        CreateDeprecatedWorldRenderExecutor(
            ELegacyRendererOverride::SoftwareRasterizer);
    std::unique_ptr<IDeprecatedWorldRenderExecutor> pureGPU =
        CreateDeprecatedWorldRenderExecutor(
            ELegacyRendererOverride::PureGPURayTracer);
    assert(software);
    assert(pureGPU);
    assert(software.get() != pureGPU.get());
    Require(software->OverrideKind() ==
                ELegacyRendererOverride::SoftwareRasterizer,
            "SoftwareRasterizer factory branch returned the wrong implementation");
    Require(pureGPU->OverrideKind() ==
                ELegacyRendererOverride::PureGPURayTracer,
            "PureGPURayTracer factory branch returned the wrong implementation");
    assert(software->RequiresOpenGLTargetBinding());
    assert(pureGPU->RequiresOpenGLTargetBinding());
    assert(software->LifecycleStats().initializations == 0);
    assert(software->LifecycleStats().executions == 0);
    assert(software->LifecycleStats().shutdowns == 0);
    assert(pureGPU->LifecycleStats().initializations == 0);
    assert(pureGPU->LifecycleStats().executions == 0);
    assert(pureGPU->LifecycleStats().shutdowns == 0);
}

class FSpyWorldRenderExecutor final : public IWorldRenderExecutor
{
public:
    bool Execute(const FWorldRenderRequest& request) override
    {
        ++calls;
        scene = request.scene;
        target = &request.target;
        features = request.features;
        quality = request.quality;
        backend = request.backendSelection;
        plan = request.passPlan;
        return true;
    }

    int calls = 0;
    FRenderScene scene;
    const FRenderTarget* target = nullptr;
    FRenderFeatures features;
    FRenderQuality quality;
    FBackendSelection backend;
    std::vector<ERenderPass> plan;
};

void CheckSceneExtractionAndRouting()
{
    UWorld world;

    auto* first = new AActor();
    first->name = "First";
    first->SetActorLocation({2.0f, 3.0f, 4.0f});
    auto* meshComponent = new UMeshComponent();
    meshComponent->mesh = UMesh::GenerateCube({1.0f, 1.0f, 1.0f});
    meshComponent->hasMaterialOverride = true;
    meshComponent->materialOverride.kd = {0.2f, 0.4f, 0.6f};
    meshComponent->materialOverride.ks = {0.1f, 0.3f, 0.5f};
    meshComponent->materialOverride.shininess = 23.0f;
    meshComponent->materialOverride.km = {0.25f, 0.5f, 0.75f};
    meshComponent->uvTiling = {3.0f, 5.0f};
    first->SetMesh(meshComponent);
    world.Spawn(first);

    world.GetScene().Actors.push_back(nullptr);
    auto* empty = new AActor();
    empty->name = "No mesh";
    world.Spawn(empty);

    auto* pointActor = new ALight();
    pointActor->name = "Point";
    pointActor->SetActorLocation({7.0f, 8.0f, 9.0f});
    pointActor->SetLightComponent(new PointLightComponent({0.5f, 0.25f, 0.75f}, glm::vec3(4.0f)));
    world.Spawn(pointActor);

    auto* environmentActor = new ALight();
    environmentActor->name = "Environment";
    auto* environment = new EnvironmentLightComponent({0.8f, 0.7f, 0.6f}, glm::vec3(2.0f));
    environment->skyTexPath.clear();
    environment->horizonColor = {0.1f, 0.2f, 0.3f};
    environment->zenithColor = {0.6f, 0.7f, 0.8f};
    environment->skyExp = 1.75f;
    environmentActor->SetLightComponent(environment);
    world.Spawn(environmentActor);
    auto* skyActor = new ALight();
    skyActor->name = "Sky override";
    auto* skyEnvironment = new EnvironmentLightComponent({0.1f, 0.1f, 0.1f}, glm::vec3(1.0f));
    skyEnvironment->skyTexPath = "Content/Sky/Actor.hdr";
    skyEnvironment->horizonColor = {0.9f, 0.9f, 0.9f};
    skyActor->SetLightComponent(skyEnvironment);
    world.Spawn(skyActor);
    world.GetScene().skyHDRI = "Content/Sky/Scene.hdr";
    world.GetScene().shadingModel = 1;

    ACamera& camera = world.GetCamera();
    camera.eye = {11.0f, 12.0f, 13.0f};
    camera.SetOrientation(22.0f, -13.0f);
    camera.SetFOV(67.0f, 2.0f);

    auto spy = std::make_unique<FSpyWorldRenderExecutor>();
    FSpyWorldRenderExecutor* observed = spy.get();
    UWorldRenderer renderer(std::move(spy));

    FRenderTarget target = FRenderTarget::DefaultFramebuffer(800, 400, 9);
    FRenderFeatures features;
    features.hardwareRaster = false; // normal rendering must normalize this on.
    features.rayTracing = true;
    features.rayTracedShadows = false;
    features.rayTracedGI = true;
    features.rayTracedReflections = false;
    features.rayTracingBackend = ERayTracingBackend::CompatibleGL33;
    FRenderQuality quality;
    quality.ssaa = 2;
    quality.ambientStrength = 0.35f;
    quality.giSamples = 17;
    FBackendSelection backend;
    backend.requested = ERayTracingBackend::CompatibleGL33;
    backend.selected = ERayTracingBackend::CompatibleGL33;
    backend.available = true;
    backend.rayTracingEnabled = true;

    assert(renderer.Render(world, camera, target, features, quality, backend));
    assert(observed->calls == 1);
    assert(observed->target == &target);
    assert(observed->features.hardwareRaster);
    assert(observed->features.rayTracing);
    assert(!observed->features.rayTracedShadows);
    assert(observed->features.rayTracedGI);
    assert(!observed->features.rayTracedReflections);
    assert(observed->quality.ssaa == 2 && Near(observed->quality.ambientStrength, 0.35f));
    assert(observed->quality.giSamples == 17);
    assert(observed->backend.available && observed->backend.rayTracingEnabled);
    assert(observed->plan == std::vector<ERenderPass>({
        ERenderPass::HardwareGBuffer,
        ERenderPass::RasterLighting,
        ERenderPass::RayTracedEffects,
        ERenderPass::Composite,
    }));

    assert(Near(observed->scene.camera.eye.x, 11.0f));
    assert(Near(observed->scene.camera.fovDegrees, 67.0f));
    assert(observed->scene.meshes.size() == 1);
    const FRenderMeshInstance& instance = observed->scene.meshes[0];
    assert(instance.mesh == meshComponent->mesh);
    assert(instance.objectIdentity == 1);
    assert(instance.materialOverrideIdentity == 1);
    assert(instance.shadingModel == ERenderShadingModel::Gouraud);
    assert(Near(instance.modelTransform[3].x, 2.0f));
    assert(instance.materialOverride.has_value());
    assert(instance.materialOverride->source == &meshComponent->materialOverride);
    assert(instance.materialOverride->albedo == glm::vec3(0.2f, 0.4f, 0.6f));
    assert(instance.materialOverride->specularColor == glm::vec3(0.1f, 0.3f, 0.5f));
    assert(Near(instance.materialOverride->shininess, 23.0f));
    assert(Near(instance.materialOverride->mirrorFactor, 0.75f));
    assert(instance.materialSlots.size() == 1);
    assert(instance.materialSlots[0].source == &meshComponent->mesh->material);
    assert(instance.materialSlotIdentities.size() == 1);
    assert(instance.uvTiling == glm::vec2(3.0f, 5.0f));
    assert(observed->scene.pointLights.size() == 1);
    assert(!observed->scene.usesDefaultPointLight);
    assert(observed->scene.pointLights[0].worldPosition == glm::vec3(7.0f, 8.0f, 9.0f));
    assert(observed->scene.pointLights[0].sourceIntensity == glm::vec3(2.0f, 1.0f, 3.0f));
    assert(observed->scene.environment.skyPath == "Content/Sky/Actor.hdr");
    assert(observed->scene.environment.tint == glm::vec3(1.6f, 1.4f, 1.2f));
    assert(Near(observed->scene.environment.exponent, 1.75f));

    delete meshComponent->mesh;
    meshComponent->mesh = nullptr;
}

void CheckDefaultLightAndTargetValidation()
{
    UWorld world;
    auto spy = std::make_unique<FSpyWorldRenderExecutor>();
    FSpyWorldRenderExecutor* observed = spy.get();
    UWorldRenderer renderer(std::move(spy));
    FRenderFeatures features;
    features.rayTracing = true;
    features.rayTracedShadows = false;
    features.rayTracedGI = false;
    features.rayTracedReflections = false;
    features.rayTracedTranslucency = false;
    FRenderQuality quality;
    FBackendSelection backend;
    backend.available = true;
    backend.rayTracingEnabled = true;

    FRenderTarget target = FRenderTarget::DefaultFramebuffer(320, 200, 7);
    assert(target.IsValidForContext(7));
    assert(target.Resize(640, 360, 7));
    assert(target.Width() == 640 && target.Height() == 360);
    assert(target.Identity() == 0);
    assert(target.Kind() == ERenderTargetKind::DefaultFramebuffer);
    assert(!target.Resize(0, 360, 7));
    assert(target.Width() == 640 && target.Height() == 360);

    assert(renderer.Render(world, world.GetCamera(), target, features, quality, backend));
    assert(observed->calls == 1);
    assert(observed->scene.pointLights.size() == 1);
    assert(!observed->features.rayTracedTranslucency);
    assert(observed->scene.usesDefaultPointLight);
    assert(observed->scene.pointLights[0].worldPosition == glm::vec3(6.0f, 8.0f, 2.0f));
    assert(observed->plan == std::vector<ERenderPass>({
        ERenderPass::HardwareGBuffer,
        ERenderPass::RasterLighting,
        ERenderPass::Composite,
    }));

    FRenderTarget wrongGeneration = FRenderTarget::DefaultFramebuffer(10, 10, 8);
    assert(!renderer.Render(world, world.GetCamera(), wrongGeneration, features, quality, backend, 7));
    assert(observed->calls == 1);
    FRenderTarget invalid = FRenderTarget::DefaultFramebuffer(-1, 10, 7);
    assert(!renderer.Render(world, world.GetCamera(), invalid, features, quality, backend));
    assert(observed->calls == 1);
}

void CheckNeutralRayOutputs()
{
    const FRayEffectOutputs outputs;
    assert(!outputs.shadows.has_value());
    assert(!outputs.globalIllumination.has_value());
    assert(!outputs.reflections.has_value());
    assert(outputs.ShadowVisibilityOrNeutral() == 1.0f);
    assert(outputs.GlobalIlluminationOrNeutral() == glm::vec3(0.0f));
    assert(outputs.ReflectionOrNeutral() == glm::vec3(0.0f));

    FLogicalGBufferSample sample;
    sample.valid = true;
    sample.coverage = 1.0f;
    sample.depth = 0.5f;
    sample.objectIdentity = 4;
    sample.materialIdentity = 2;
    assert(sample.valid && sample.coverage == 1.0f && sample.depth == 0.5f);
}

void CheckRuntimeBackendChangesUseCurrentWorldRequest()
{
    UWorld world;
    auto spy = std::make_unique<FSpyWorldRenderExecutor>();
    FSpyWorldRenderExecutor* observed = spy.get();
    UWorldRenderer renderer(std::move(spy));
    FRenderTarget target = FRenderTarget::DefaultFramebuffer(64, 64, 3);
    FRenderFeatures features;
    features.rayTracing = true;
    features.rayTracingBackend = ERayTracingBackend::ComputeGL43;
    FRenderQuality quality;

    FBackendSelection unavailableCompute;
    unavailableCompute.requested = ERayTracingBackend::ComputeGL43;
    unavailableCompute.selected = ERayTracingBackend::ComputeGL43;
    unavailableCompute.available = false;
    unavailableCompute.rayTracingEnabled = false;
    assert(renderer.Render(world, world.GetCamera(), target, features, quality,
                           unavailableCompute));
    assert(observed->calls == 1);
    assert(observed->backend.requested == ERayTracingBackend::ComputeGL43);
    assert(observed->features.rayTracingBackend == ERayTracingBackend::ComputeGL43);
    assert(observed->features.rayTracing); // authored request is preserved; plan is session-effective
    assert(observed->plan.size() == 3);

    features.rayTracingBackend = ERayTracingBackend::CompatibleGL33;
    FBackendSelection compatible;
    compatible.requested = ERayTracingBackend::CompatibleGL33;
    compatible.selected = ERayTracingBackend::CompatibleGL33;
    compatible.available = true;
    compatible.rayTracingEnabled = true;
    assert(renderer.Render(world, world.GetCamera(), target, features, quality,
                           compatible));
    assert(observed->calls == 2);
    assert(observed->backend.requested == ERayTracingBackend::CompatibleGL33);
    assert(observed->features.rayTracing);
    assert(observed->plan.size() == 4);
    assert(observed->plan[2] == ERenderPass::RayTracedEffects);
}

UMesh* MakeTwoMaterialMesh()
{
    auto* mesh = new UMesh();
    mesh->vertices = {
        {{-0.9f, -0.8f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{-0.1f, -0.8f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{-0.5f,  0.8f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}},
        {{ 0.1f, -0.8f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{ 0.9f, -0.8f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.8f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}},
    };
    mesh->indices = {0, 1, 2, 3, 4, 5};
    Material red;
    red.kd = {1.0f, 0.0f, 0.0f};
    red.texData = {255, 0, 0};
    red.texWidth = 1;
    red.texHeight = 1;
    red.texChannels = 3;
    Material green;
    green.kd = {0.0f, 1.0f, 0.0f};
    mesh->materials = {red, green};
    mesh->triMaterial = {0, 1};
    mesh->FinalizeGeometry();
    return mesh;
}

void CheckMultiMaterialSlotsAndOverridePrecedence()
{
    UWorld world;
    auto* actor = new AActor();
    auto* component = new UMeshComponent();
    component->mesh = MakeTwoMaterialMesh();
    actor->SetMesh(component);
    world.Spawn(actor);

    auto spy = std::make_unique<FSpyWorldRenderExecutor>();
    FSpyWorldRenderExecutor* observed = spy.get();
    UWorldRenderer renderer(std::move(spy));
    FRenderTarget target = FRenderTarget::DefaultFramebuffer(64, 64, 5);
    FRenderFeatures features;
    FRenderQuality quality;
    FBackendSelection backend;
    assert(renderer.Render(world, world.GetCamera(), target, features, quality, backend));

    assert(observed->scene.meshes.size() == 1);
    const FRenderMeshInstance& slots = observed->scene.meshes.front();
    assert(!slots.materialOverride.has_value());
    assert(slots.materialOverrideKind == ERenderMaterialOverrideKind::None);
    assert(slots.materialOverrideIdentity == 0);
    assert(slots.materialSlots.size() == 2);
    assert(slots.materialSlotIdentities.size() == 2);
    assert(slots.materialSlotIdentities[0] != slots.materialSlotIdentities[1]);
    assert(slots.triangleMaterialSlots == &component->mesh->triMaterial);
    assert(slots.triangleMaterialSlotCount == 2);
    assert(slots.materialSlots[0].source == &component->mesh->materials[0]);
    assert(slots.materialSlots[1].source == &component->mesh->materials[1]);
    const unsigned char* originalTextureBytes =
        component->mesh->materials[0].texData.data();
    assert(slots.materialSlots[0].source->texData.data() == originalTextureBytes);
    assert(slots.materialSlots[0].source->texData.size() == 3);

    FTransform transform;
    transform.viewport = FTransform::MakeViewport(64, 64);
    UGBuffer gbuffer;
    gbuffer.Init(64, 64);
    gbuffer.Clear();
    URasterizer rasterizer;
    rasterizer.nearClip = false; // identity projection in this isolated raster test
    rasterizer.DrawMeshGBuffer(*component->mesh, transform, glm::vec3(0.25f),
                               gbuffer, 0, 0, 63, 63, true, nullptr);
    bool foundRed = false;
    bool foundGreen = false;
    for (std::size_t i = 0; i < gbuffer.depth.size(); ++i)
    {
        if (gbuffer.depth[i] >= 1.0f) continue;
        foundRed = foundRed || gbuffer.albedo[i] == glm::vec3(1.0f, 0.0f, 0.0f);
        foundGreen = foundGreen || gbuffer.albedo[i] == glm::vec3(0.0f, 1.0f, 0.0f);
    }
    assert(foundRed && foundGreen);

    component->hasMaterialOverride = true;
    component->materialOverride.kd = {0.0f, 0.0f, 1.0f};
    assert(renderer.Render(world, world.GetCamera(), target, features, quality, backend));
    const FRenderMeshInstance& overridden = observed->scene.meshes.front();
    assert(overridden.materialOverride.has_value());
    assert(overridden.materialOverrideKind ==
           ERenderMaterialOverrideKind::ComponentOverride);
    assert(overridden.materialOverride->source == &component->materialOverride);
    assert(overridden.materialOverride->albedo == glm::vec3(0.0f, 0.0f, 1.0f));
    assert(overridden.materialSlots.size() == 2);

    gbuffer.Clear();
    rasterizer.DrawMeshGBuffer(*component->mesh, transform,
                               overridden.materialOverride->albedo,
                               gbuffer, 0, 0, 63, 63, true,
                               overridden.materialOverride->source);
    bool foundCovered = false;
    for (std::size_t i = 0; i < gbuffer.depth.size(); ++i)
    {
        if (gbuffer.depth[i] >= 1.0f) continue;
        foundCovered = true;
        assert(gbuffer.albedo[i] == glm::vec3(0.0f, 0.0f, 1.0f));
    }
    assert(foundCovered);

    Material shared;
    shared.kd = {0.75f, 0.25f, 0.5f};
    component->sharedMaterial = &shared;
    assert(renderer.Render(world, world.GetCamera(), target, features, quality, backend));
    const FRenderMeshInstance& sharedOverride = observed->scene.meshes.front();
    assert(sharedOverride.materialOverrideKind ==
           ERenderMaterialOverrideKind::SharedMaterial);
    assert(sharedOverride.materialOverride->source == &shared);
    assert(sharedOverride.materialOverride->albedo == shared.kd);

    delete component->mesh;
    component->mesh = nullptr;
}

class FFakeRenderTargetAdapter final : public IRenderTargetGLAdapter
{
public:
    std::uint64_t activeGeneration = 1;
    unsigned nextIdentity = 10;
    int allocations = 0;
    int deletions = 0;
    int captures = 0;
    int binds = 0;
    int restores = 0;
    FRenderTargetBindingState restoredState;

    std::uint64_t ActiveContextGeneration() const noexcept override
    {
        return activeGeneration;
    }

    bool AllocateTextureViewport(int, int, FRenderTargetAttachments& out) override
    {
        ++allocations;
        out.framebuffer = nextIdentity++;
        out.colorTexture = nextIdentity++;
        out.depthAttachment = nextIdentity++;
        return true;
    }

    void DeleteTextureViewport(const FRenderTargetAttachments&) noexcept override
    {
        ++deletions;
    }

    FRenderTargetBindingState CaptureBindingState() override
    {
        ++captures;
        FRenderTargetBindingState state;
        state.framebuffer = 77;
        state.viewport[0] = 1;
        state.viewport[1] = 2;
        state.viewport[2] = 3;
        state.viewport[3] = 4;
        return state;
    }

    void BindTarget(unsigned, int, int) override { ++binds; }

    void RestoreBindingState(const FRenderTargetBindingState& state) noexcept override
    {
        ++restores;
        restoredState = state;
    }
};

void CheckRenderTargetGenerationAwareLifetime()
{
    FFakeRenderTargetAdapter adapter;
    {
        FRenderTarget target = FRenderTarget::TextureViewport(32, 16, 1, adapter);
        assert(target.IsValid());
        target.Release();
        assert(adapter.deletions == 1);
    }

    adapter.deletions = 0;
    {
        FRenderTarget stale = FRenderTarget::TextureViewport(32, 16, 1, adapter);
        adapter.activeGeneration = 2;
        stale.Release();
        assert(adapter.deletions == 0);
    }

    adapter.activeGeneration = 3;
    adapter.deletions = 0;
    {
        FRenderTarget staleDestructor =
            FRenderTarget::TextureViewport(32, 16, 3, adapter);
        adapter.activeGeneration = 4;
    }
    assert(adapter.deletions == 0);

    adapter.activeGeneration = 5;
    adapter.deletions = 0;
    {
        FRenderTarget resized = FRenderTarget::TextureViewport(32, 16, 5, adapter);
        adapter.activeGeneration = 6;
        assert(resized.Resize(64, 32, 6));
        assert(adapter.deletions == 0);
        resized.Release();
        assert(adapter.deletions == 1);
    }

    adapter.activeGeneration = 7;
    adapter.restores = 0;
    {
        FRenderTarget source = FRenderTarget::TextureViewport(32, 16, 7, adapter);
        assert(source.Begin());
        FRenderTarget moved(std::move(source));
        moved.End();
        assert(adapter.restores == 1);
        assert(adapter.restoredState.framebuffer == 77);
        assert(adapter.restoredState.viewport[0] == 1);
        assert(adapter.restoredState.viewport[3] == 4);
    }
}
} // namespace

int main()
{
    CheckNormalRouteSourceIsolation();
    CheckDeprecatedOverrideFactoryIsExplicitAndLazy();
    CheckSceneExtractionAndRouting();
    CheckDefaultLightAndTargetValidation();
    CheckNeutralRayOutputs();
    CheckRuntimeBackendChangesUseCurrentWorldRequest();
    CheckMultiMaterialSlotsAndOverridePrecedence();
    CheckRenderTargetGenerationAwareLifetime();
    std::cout << "WorldRendererRoutingTest passed\n";
    return 0;
}
