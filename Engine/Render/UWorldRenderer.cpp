#include "UWorldRenderer.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "ACamera.h"
#include "FRenderShowFlag.h"
#include "IRayTracingBackend.h"
#include "FTransform.h"
#include "Material.h"
#include "ThreadPool.h"
#include "UGBuffer.h"
#include "UHybridPass.h"
#include "UHardwareGBuffer.h"
#include "UHardwareRasterizer.h"
#include "URasterLightingPass.h"
#include "UGL33RayTracingBackend.h"
#include "UGPUMeshCache.h"
#include "UMesh.h"
#include "URasterizer.h"
#include "URenderer.h"
#include "UScene.h"
#include "USkyHDRI.h"
#include "UWorld.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <utility>
#include <vector>
#include <stdexcept>
#include <string>

namespace
{
ACamera LegacyCameraFrom(const FRenderCamera& source)
{
    ACamera camera;
    camera.eye = source.eye;
    camera.u = source.right;
    camera.v = source.up;
    camera.w = source.backward;
    camera.l = source.left;
    camera.r = source.rightPlane;
    camera.b = source.bottom;
    camera.t = source.top;
    camera.d = source.nearDistance;
    camera.fov = source.fovDegrees;
    return camera;
}

class FLegacyWorldRenderExecutor final : public IWorldRenderExecutor
{
public:
    void Init() override
    {
        hybrid_.Init();
        ready_ = true;
    }

    void Shutdown() noexcept override
    {
        hybrid_.Cleanup();
        sky_.Cleanup();
        ready_ = false;
        hybridUploaded_ = false;
        hybridSignature_ = 0;
    }

    bool RequiresOpenGLTargetBinding() const override { return true; }

    bool Execute(const FWorldRenderRequest& request) override
    {
        // Normal named features have exactly two transitional destinations.
        // Pure GPU RT is deliberately absent from this executor.
        const bool rayEffects = request.features.rayTracing &&
            (request.features.rayTracedShadows || request.features.rayTracedGI ||
             request.features.rayTracedReflections) &&
            request.backendSelection.rayTracingEnabled;
        if (!rayEffects || !ready_) RenderSoftwareRaster(request);
        else                        RenderHybrid(request);
        return true;
    }

private:
    static std::uint64_t GeometrySignature(const FRenderScene& scene)
    {
        std::uint64_t signature = 1469598103934665603ull;
        auto mix = [&](const void* bytes, size_t count)
        {
            const auto* data = static_cast<const unsigned char*>(bytes);
            for (size_t i = 0; i < count; ++i)
            {
                signature ^= data[i];
                signature *= 1099511628211ull;
            }
        };
        for (const FRenderMeshInstance& instance : scene.meshes)
        {
            mix(&instance.mesh, sizeof(instance.mesh));
            const std::uint64_t geometryRevision = instance.mesh
                ? instance.mesh->GeometryRevision() : 0;
            mix(&geometryRevision, sizeof(geometryRevision));
            mix(&instance.modelTransform, sizeof(instance.modelTransform));
            auto mixMaterial = [&](const FResolvedRenderMaterial& material)
            {
                mix(&material.albedo, sizeof(material.albedo));
                mix(&material.mirrorFactor, sizeof(material.mirrorFactor));
                const size_t textureBytes = material.source
                    ? material.source->texData.size() : 0;
                mix(&textureBytes, sizeof(textureBytes));
            };
            if (instance.materialOverride)
                mixMaterial(*instance.materialOverride);
            for (const FResolvedRenderMaterial& slot : instance.materialSlots)
                mixMaterial(slot);
        }
        return signature;
    }

    void RenderSoftwareRaster(const FWorldRenderRequest& request)
    {
        FRenderShowFlag flag;
        flag.shading = static_cast<EShadingModel>(request.scene.shadingModel);
        flag.depthView = request.quality.depthView;
        flag.ambientStrength = request.quality.ambientStrength;
        sky_.GetOrLoad(request.scene.environment.skyPath);
        const std::vector<float>& output = rasterRenderer_.RasterShadedLegacyOutput(
            request.scene, request.target.Width(), request.target.Height(), flag, &sky_);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (!output.empty())
            glDrawPixels(request.target.Width(), request.target.Height(), GL_RGB,
                         GL_FLOAT, output.data());
    }

    void RenderHybrid(const FWorldRenderRequest& request)
    {
        const FRenderScene& scene = request.scene;
        const ACamera camera = LegacyCameraFrom(scene.camera);
        const int width = request.target.Width();
        const int height = request.target.Height();

        std::vector<const UMesh*> meshes;
        std::vector<glm::mat4> models;
        std::vector<glm::vec3> albedos;
        std::vector<const Material*> materials;
        std::vector<glm::vec2> uvTilings;
        meshes.reserve(scene.meshes.size());
        models.reserve(scene.meshes.size());
        albedos.reserve(scene.meshes.size());
        materials.reserve(scene.meshes.size());
        uvTilings.reserve(scene.meshes.size());
        for (const FRenderMeshInstance& instance : scene.meshes)
        {
            meshes.push_back(instance.mesh);
            models.push_back(instance.modelTransform);
            if (instance.materialOverride)
            {
                albedos.push_back(instance.materialOverride->albedo);
                materials.push_back(instance.materialOverride->source);
            }
            else
            {
                albedos.push_back(instance.materialSlots.empty()
                    ? glm::vec3(1.0f) : instance.materialSlots.front().albedo);
                materials.push_back(nullptr);
            }
            uvTilings.push_back(instance.uvTiling);
        }

        std::vector<glm::vec3> lightPositions;
        std::vector<glm::vec3> lightRadiances;
        lightPositions.reserve(scene.pointLights.size());
        lightRadiances.reserve(scene.pointLights.size());
        for (const FRenderPointLight& light : scene.pointLights)
        {
            lightPositions.push_back(light.worldPosition);
            lightRadiances.push_back(light.radiance);
        }

        const FRenderQuality& quality = request.quality;
        const int giSamples = request.features.rayTracedGI ? quality.giSamples : 0;
        hybrid_.SetGI(giSamples,
                      scene.environment.tint * quality.giStrength,
                      scene.environment.horizon,
                      scene.environment.zenith,
                      scene.environment.exponent,
                      quality.giBounces);
        hybrid_.SetQuality(quality.shininess);
        hybrid_.SetShadow(request.features.rayTracedShadows ? quality.shadowSamples : 0,
                          quality.shadowSoftness);
        hybrid_.SetSky(sky_.GetOrLoad(scene.environment.skyPath));

        glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gbuffer_.Init(width, height);
        gbuffer_.Clear();

        std::vector<FTransform> transforms(meshes.size());
        for (size_t i = 0; i < meshes.size(); ++i)
        {
            transforms[i].model = models[i];
            transforms[i].view = FTransform::MakeView(camera);
            transforms[i].proj = FTransform::MakeProjFCG(
                camera.l, camera.r, camera.b, camera.t, -camera.d, -1000.0f);
            transforms[i].viewport = FTransform::MakeViewport(width, height);
        }

        const std::uint64_t signature = GeometrySignature(scene);
        if (!hybridUploaded_ || signature != hybridSignature_)
        {
            std::vector<glm::vec3> triangles;
            for (size_t i = 0; i < meshes.size(); ++i)
            {
                const int triangleCount = meshes[i]->triangleCount();
                for (int triangle = 0; triangle < triangleCount; ++triangle)
                    for (int corner = 0; corner < 3; ++corner)
                    {
                        const uint32_t vertexIndex = meshes[i]->indices[3 * triangle + corner];
                        triangles.push_back(glm::vec3(models[i] * glm::vec4(
                            meshes[i]->vertices[vertexIndex].position, 1.0f)));
                    }
            }
            hybrid_.UploadSceneTriangles(triangles);
            hybridSignature_ = signature;
            hybridUploaded_ = true;
        }

        constexpr int tileSize = 64;
        const int tileColumns = (width + tileSize - 1) / tileSize;
        const int tileRows = (height + tileSize - 1) / tileSize;
        pool_.ParallelForChunks(tileColumns * tileRows, [&](int begin, int end)
        {
            for (int tile = begin; tile < end; ++tile)
            {
                const int tileX = tile % tileColumns;
                const int tileY = tile / tileColumns;
                const int minX = tileX * tileSize;
                const int minY = tileY * tileSize;
                const int maxX = std::min(minX + tileSize - 1, width - 1);
                const int maxY = std::min(minY + tileSize - 1, height - 1);
                for (size_t i = 0; i < meshes.size(); ++i)
                    rasterizer_.DrawMeshGBuffer(*meshes[i], transforms[i], albedos[i],
                        gbuffer_, minX, minY, maxX, maxY, false, materials[i], uvTilings[i]);
            }
        });

        hybrid_.UploadGBuffer(gbuffer_);
        hybrid_.Render(camera, lightPositions, lightRadiances, width, height);
    }

    URenderer rasterRenderer_;
    URasterizer rasterizer_;
    UHybridPass hybrid_;
    UGBuffer gbuffer_;
    ThreadPool pool_;
    USkyHDRI sky_;
    std::uint64_t hybridSignature_ = 0;
    bool hybridUploaded_ = false;
    bool ready_ = false;
};

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
            stats_.rayDispatches = rayEffects_.Stats().backendCalls;
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
