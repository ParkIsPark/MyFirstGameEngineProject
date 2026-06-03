#include "URenderer.h"
#include "FTransform.h"
#include "UMesh.h"
#include "UMeshComponent.h"
#include "AActor.h"
#include "ACamera.h"
#include "UScene.h"
#include "UWorld.h"
#include "ALight.h"
#include "PointLight.h"

#include <vector>
#include <algorithm>

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
    // Primary visibility into the G-buffer (world pos / normal / albedo / depth).
    GBufferWorld(scene, cam, nx, ny);

    // Gather the scene's point lights (from the light actors).
    struct PL { glm::vec3 pos; glm::vec3 col; };
    std::vector<PL> lights;
    for (AActor* a : scene.Actors)
        if (ALight* L = dynamic_cast<ALight*>(a))
            if (PointLight* pl = dynamic_cast<PointLight*>(L->lightComp))
                lights.push_back({ pl->LightPos, pl->LightColor * pl->LightIntensity });

    // Deferred Lambert diffuse + ambient (Blinn-Phong specular + shadows are the
    // GPU / Hybrid path). Editing a light or material in the editor shows live.
    scene.outputImage.resize(static_cast<size_t>(nx) * ny * 3);
    const glm::vec3 ambient(0.16f);
    const glm::vec3 bg(0.10f, 0.11f, 0.13f);
    const int n = nx * ny;

    // Per-pixel shading is embarrassingly parallel (each pixel independent and
    // writes a disjoint slot) -> split across the worker pool. Bit-equal to the
    // serial path, which `multithread = false` selects for the equivalence test.
    auto shadeRange = [&](int begin, int end)
    {
        for (int i = begin; i < end; ++i)
        {
            glm::vec3 col;
            if (gbuffer_.depth[i] >= 1.0f)
            {
                col = bg;
            }
            else
            {
                const glm::vec3 P   = gbuffer_.worldPos[i];
                const glm::vec3 N   = glm::normalize(gbuffer_.normal[i]);
                const glm::vec3 alb = gbuffer_.albedo[i];
                col = alb * ambient;
                for (const PL& L : lights)
                {
                    glm::vec3 l = L.pos - P;
                    const float d = glm::length(l);
                    if (d < 1e-5f) continue;
                    l /= d;
                    col += alb * L.col * glm::max(glm::dot(N, l), 0.0f);
                }
                col = glm::min(col, glm::vec3(1.0f));
            }
            scene.outputImage[3 * i + 0] = col.r;
            scene.outputImage[3 * i + 1] = col.g;
            scene.outputImage[3 * i + 2] = col.b;
        }
    };

    if (multithread) pool_.ParallelForChunks(n, shadeRange);
    else             shadeRange(0, n);
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

        const FTransform xf = ActorTransform(comp->GetWorldMatrix(), cam, nx, ny);
        raster_.DrawMeshGBuffer(*comp->mesh, xf, albedo, gbuffer_);
    }
}
