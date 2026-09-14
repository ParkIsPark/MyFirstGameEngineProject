#include "FDeprecatedWorldRenderExecutor.h"

#include <GL/glew.h>

#include "ACamera.h"
#include "FRenderShowFlag.h"
#include "Material.h"
#include "UMeshRayTracer.h"
#include "URenderer.h"
#include "USkyHDRI.h"

#include <utility>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(disable : 4996) // This module intentionally calls deprecated renderers.
#endif

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

class FLegacyPixelDrawState
{
public:
    FLegacyPixelDrawState(int width, int height)
    {
        glGetIntegerv(GL_MATRIX_MODE, &previousMatrixMode_);
        glPushAttrib(GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT |
                     GL_PIXEL_MODE_BIT | GL_TRANSFORM_BIT);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0.0, static_cast<double>(width), 0.0,
                static_cast<double>(height), 1.0, -1.0);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        glDisable(GL_DEPTH_TEST);
        glRasterPos2i(0, 0);
    }

    ~FLegacyPixelDrawState()
    {
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glPopAttrib();
        glMatrixMode(previousMatrixMode_);
    }

private:
    GLint previousMatrixMode_ = GL_MODELVIEW;
};

class FDeprecatedSoftwareRasterExecutor final
    : public IDeprecatedWorldRenderExecutor
{
public:
    void Init() override
    {
        if (initialized_) return;
        ++stats_.initializationAttempts;
        initialized_ = true;
        ++stats_.initializations;
    }

    void Shutdown() noexcept override
    {
        if (!initialized_) return;
        sky_.Cleanup();
        initialized_ = false;
        ++stats_.shutdowns;
    }

    bool RequiresOpenGLTargetBinding() const override { return true; }
    bool Ready() const noexcept override { return initialized_; }
    ELegacyRendererOverride OverrideKind() const noexcept override
    {
        return ELegacyRendererOverride::SoftwareRasterizer;
    }
    const FDeprecatedWorldRenderExecutorStats& LifecycleStats()
        const noexcept override
    {
        return stats_;
    }

    bool Execute(const FWorldRenderRequest& request) override
    {
        if (!initialized_) return false;
        ++stats_.executions;
        FRenderShowFlag flags;
        flags.shading = static_cast<EShadingModel>(request.scene.shadingModel);
        flags.depthView = request.quality.depthView;
        flags.ambientStrength = request.quality.ambientStrength;
        sky_.GetOrLoad(request.scene.environment.skyPath);
        const std::vector<float>& output = renderer_.RasterShadedLegacyOutput(
            request.scene, request.target.Width(), request.target.Height(),
            flags, &sky_);

        FLegacyPixelDrawState fixedFunctionState(
            request.target.Width(), request.target.Height());
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (!output.empty())
            glDrawPixels(request.target.Width(), request.target.Height(), GL_RGB,
                         GL_FLOAT, output.data());
        return true;
    }

private:
    URenderer renderer_;
    USkyHDRI sky_;
    FDeprecatedWorldRenderExecutorStats stats_;
    bool initialized_ = false;
};

class FDeprecatedPureGPURayTracerExecutor final
    : public IDeprecatedWorldRenderExecutor
{
public:
    void Init() override
    {
        if (!ready_)
        {
            initializationStarted_ = true;
            ++stats_.initializationAttempts;
            rayTracer_.Init();
            ready_ = rayTracer_.ready();
            if (ready_) ++stats_.initializations;
            else Shutdown();
        }
    }

    void Shutdown() noexcept override
    {
        if (!initializationStarted_) return;
        rayTracer_.Cleanup();
        sky_.Cleanup();
        ready_ = false;
        initializationStarted_ = false;
        ++stats_.shutdowns;
    }

    bool RequiresOpenGLTargetBinding() const override { return true; }
    bool Ready() const noexcept override { return ready_ && rayTracer_.ready(); }
    ELegacyRendererOverride OverrideKind() const noexcept override
    {
        return ELegacyRendererOverride::PureGPURayTracer;
    }
    const FDeprecatedWorldRenderExecutorStats& LifecycleStats()
        const noexcept override
    {
        return stats_;
    }

    bool Execute(const FWorldRenderRequest& request) override
    {
        if (!ready_) return false;
        ++stats_.executions;

        std::vector<const UMesh*> meshes;
        std::vector<glm::mat4> models;
        std::vector<glm::vec3> albedos;
        std::vector<float> mirrors;
        std::vector<const Material*> materials;
        std::vector<glm::vec2> uvTilings;
        meshes.reserve(request.scene.meshes.size());
        models.reserve(request.scene.meshes.size());
        albedos.reserve(request.scene.meshes.size());
        mirrors.reserve(request.scene.meshes.size());
        materials.reserve(request.scene.meshes.size());
        uvTilings.reserve(request.scene.meshes.size());
        for (const FRenderMeshInstance& instance : request.scene.meshes)
        {
            if (!instance.mesh) continue;
            const FResolvedRenderMaterial* material = instance.materialOverride
                ? &*instance.materialOverride
                : (instance.materialSlots.empty() ? nullptr
                                                  : &instance.materialSlots.front());
            meshes.push_back(instance.mesh);
            models.push_back(instance.modelTransform);
            albedos.push_back(material ? material->albedo : glm::vec3(1.0f));
            mirrors.push_back(material ? material->mirrorFactor : 0.0f);
            materials.push_back(material ? material->source : nullptr);
            uvTilings.push_back(instance.uvTiling);
        }

        std::vector<glm::vec3> lightPositions;
        std::vector<glm::vec3> lightRadiances;
        lightPositions.reserve(request.scene.pointLights.size());
        lightRadiances.reserve(request.scene.pointLights.size());
        for (const FRenderPointLight& light : request.scene.pointLights)
        {
            lightPositions.push_back(light.worldPosition);
            lightRadiances.push_back(light.sourceIntensity);
        }
        if (lightPositions.empty())
        {
            lightPositions.push_back(glm::vec3(6.0f, 8.0f, 2.0f));
            lightRadiances.push_back(glm::vec3(1.0f));
        }

        const FRenderQuality& quality = request.quality;
        rayTracer_.SetGI(quality.giSamples,
                         request.scene.environment.tint * quality.giStrength,
                         request.scene.environment.horizon,
                         request.scene.environment.zenith,
                         request.scene.environment.exponent,
                         quality.giBounces);
        rayTracer_.SetQuality(quality.reflStrength, quality.shininess);
        rayTracer_.SetShadow(quality.shadowSamples, quality.shadowSoftness);
        rayTracer_.SetSky(sky_.GetOrLoad(request.scene.environment.skyPath));
        rayTracer_.UploadWorld(meshes, models, albedos,
                               lightPositions.front(), lightRadiances.front(),
                               mirrors, materials, uvTilings);
        rayTracer_.SetLights(lightPositions, lightRadiances);

        glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        rayTracer_.RenderFrame(LegacyCameraFrom(request.scene.camera),
                               request.target.Width(), request.target.Height());
        return true;
    }

private:
    UMeshRayTracer rayTracer_;
    USkyHDRI sky_;
    FDeprecatedWorldRenderExecutorStats stats_;
    bool ready_ = false;
    bool initializationStarted_ = false;
};
} // namespace

std::unique_ptr<IDeprecatedWorldRenderExecutor> CreateDeprecatedWorldRenderExecutor(
    ELegacyRendererOverride overrideKind)
{
    switch (overrideKind)
    {
        case ELegacyRendererOverride::SoftwareRasterizer:
            return std::make_unique<FDeprecatedSoftwareRasterExecutor>();
        case ELegacyRendererOverride::PureGPURayTracer:
            return std::make_unique<FDeprecatedPureGPURayTracerExecutor>();
        case ELegacyRendererOverride::None:
            return nullptr;
    }
    return nullptr;
}
