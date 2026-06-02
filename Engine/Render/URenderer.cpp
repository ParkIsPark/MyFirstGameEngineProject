#include "URenderer.h"
#include "FTransform.h"
#include "UMesh.h"
#include "UMeshComponent.h"
#include "AActor.h"
#include "ACamera.h"
#include "UScene.h"
#include "UWorld.h"

namespace
{
    // Build the model->view->proj->viewport stack for one actor's mesh.
    // FCG projection: near = -cam.d (signed negative), far = -1000 (Q1 convention).
    FTransform ActorTransform(const glm::mat4& model, const ACamera& cam, int nx, int ny)
    {
        FTransform xf;
        xf.model    = model;
        xf.view     = FTransform::MakeView(cam);
        xf.proj     = FTransform::MakeProjFCG(cam.l, cam.r, cam.b, cam.t, -cam.d, -1000.0f);
        xf.viewport = FTransform::MakeViewport(nx, ny);
        return xf;
    }
}

std::vector<ERenderStage> URenderer::Plan(ERenderMode mode)
{
    switch (mode)
    {
        case ERenderMode::RasterOnly:
            return { ERenderStage::Raster };
        case ERenderMode::GPURayTrace:
            return { ERenderStage::GPURayTrace };
        case ERenderMode::Hybrid:
            return { ERenderStage::GBuffer, ERenderStage::Upload, ERenderStage::GPUShadow };
    }
    return { ERenderStage::Raster };
}

void URenderer::Render(UWorld& world, ERenderMode mode)
{
    UScene&        scene = world.GetScene();
    const ACamera& cam   = world.GetCamera();
    const int nx = scene.width, ny = scene.height;

    lastPlan_ = Plan(mode);
    for (ERenderStage stage : lastPlan_)
    {
        switch (stage)
        {
            case ERenderStage::Raster:
                RasterWorld(scene, cam, nx, ny);
                break;
            case ERenderStage::GBuffer:
                GBufferWorld(scene, cam, nx, ny);
                break;
            case ERenderStage::Upload:
                // TODO(page4 increment 2): upload gbuffer_ to GPU textures/TBO.
                break;
            case ERenderStage::GPUShadow:
                // TODO(page4 increment 2): fragment-shader shadow/reflection pass
                // reading the G-buffer + scene triangles (#version 330, no compute).
                break;
            case ERenderStage::GPURayTrace:
                // TODO(page4 increment 2): drive UMeshRayTracer over the world.
                break;
        }
    }
}

void URenderer::RasterWorld(UScene& scene, const ACamera& cam, int nx, int ny)
{
    fb_.Init(nx, ny);
    fb_.Clear(glm::vec3(0.0f));

    for (AActor* actor : scene.Actors)
    {
        UMeshComponent* comp = actor ? actor->mesh : nullptr;
        if (!comp || !comp->mesh) continue;

        const glm::vec3 albedo = comp->hasMaterialOverride
            ? comp->materialOverride.kd : comp->mesh->material.kd;

        const FTransform xf = ActorTransform(comp->GetWorldMatrix(*actor), cam, nx, ny);
        raster_.DrawMesh(*comp->mesh, xf, albedo, fb_);
    }

    fb_.ToOutputImage(scene.outputImage);
}

void URenderer::GBufferWorld(UScene& scene, const ACamera& cam, int nx, int ny)
{
    gbuffer_.Init(nx, ny);
    gbuffer_.Clear();

    for (AActor* actor : scene.Actors)
    {
        UMeshComponent* comp = actor ? actor->mesh : nullptr;
        if (!comp || !comp->mesh) continue;

        const glm::vec3 albedo = comp->hasMaterialOverride
            ? comp->materialOverride.kd : comp->mesh->material.kd;

        const FTransform xf = ActorTransform(comp->GetWorldMatrix(*actor), cam, nx, ny);
        raster_.DrawMeshGBuffer(*comp->mesh, xf, albedo, gbuffer_);
    }
}
