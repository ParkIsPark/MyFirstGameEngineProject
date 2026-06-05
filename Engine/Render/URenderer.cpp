#include "URenderer.h"
#include "FTransform.h"
#include "UMesh.h"
#include "UMeshComponent.h"
#include "AActor.h"
#include "ACamera.h"
#include "UScene.h"
#include "UWorld.h"
#include "ALight.h"
#include "PointLightComponent.h"
#include "EnvironmentLightComponent.h"
#include "USkyHDRI.h"
#include "URay.h"

#include <vector>
#include <algorithm>
#include <cmath>

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
            if (PointLightComponent* pl = dynamic_cast<PointLightComponent*>(L->lightComp))
                lights.push_back({ pl->GetWorldLocation(), pl->LightColor * pl->LightIntensity });

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

// ---------------------------------------------------------------------------
// CPU shaded raster (HW6 Q1-Q3) — Flat / Gouraud / Phong + Blinn-Phong + gamma.
// ---------------------------------------------------------------------------
void URenderer::RasterShaded(UWorld& world, const FRenderShowFlag& flag, const USkyHDRI* sky)
{
    UScene&        scene = world.GetScene();
    const ACamera& cam   = world.GetCamera();
    const int nx = scene.width, ny = scene.height;

    // Gather every point light in the scene. HW6 uses one; the editor may place
    // several -- all contribute (extras beyond the first go to extraLight*).
    FShadeParams sp;
    sp.ambient = glm::vec3(0.2f);
    sp.eye     = cam.eye;
    sp.envAmbient = true;                                  // sky-gradient ambient (GPU-consistent)
    sp.ambientMul = flag.ambientStrength;
    std::vector<std::pair<glm::vec3, glm::vec3>> lights;   // (pos, color*intensity)
    for (AActor* a : scene.Actors)
    {
        if (ALight* L = dynamic_cast<ALight*>(a))
        {
            if (PointLightComponent* pl = dynamic_cast<PointLightComponent*>(L->lightComp))
                lights.emplace_back(pl->GetWorldLocation(), pl->LightColor * pl->LightIntensity);
            else if (auto* el = dynamic_cast<EnvironmentLightComponent*>(L->lightComp))
            { sp.skyHorizon = el->horizonColor; sp.skyZenith = el->zenithColor; sp.skyExp = el->skyExp; }
        }
    }

    if (lights.empty())                                   // fallback: assignment light
    {
        sp.lightPos = glm::vec3(-4.0f, 4.0f, -3.0f);
        sp.lightColor = glm::vec3(1.0f);
    }
    else
    {
        sp.lightPos = lights[0].first; sp.lightColor = lights[0].second;
        for (size_t i = 1; i < lights.size(); ++i)
        { sp.extraLightPos.push_back(lights[i].first); sp.extraLightColor.push_back(lights[i].second); }
    }

    fb_.Init(nx, ny);
    fb_.Clear(glm::vec3(0.0f));

    // Gather drawable meshes once + a running triangle offset so threads can split
    // the scene's triangles evenly (one big mesh must not land on a single thread).
    struct Draw { const UMesh* mesh; FTransform xf; const Material* ov; int triBase; };
    std::vector<Draw> draws;
    int totalTris = 0;
    for (AActor* actor : scene.Actors)
    {
        UMeshComponent* comp = actor ? actor->mesh : nullptr;
        if (!comp || !comp->mesh) continue;
        Draw d;
        d.mesh    = comp->mesh;
        d.ov      = comp->EffectiveOverride();   // shared material asset > override > mesh slots
        d.xf      = ActorTransform(comp->GetWorldMatrix(), cam, nx, ny);
        d.triBase = totalTris;
        draws.push_back(d);
        totalTris += comp->mesh->triangleCount();
    }

    // Thread count: bounded by the pool, a small cap, and a memory budget (each
    // worker needs its own full framebuffer). Small scenes stay single-threaded.
    const size_t fbBytes = (size_t)nx * ny * (sizeof(glm::vec3) + sizeof(float));
    const int byMem   = fbBytes ? (int)((256ull << 20) / fbBytes) : 1;
    const int nThreads = std::max(1, std::min({ multithread ? pool_.size() : 1, 8, byMem }));

    if (nThreads > 1 && totalTris > 20000)
    {
        // Each worker rasterizes a contiguous slice of the scene's triangles into
        // its OWN framebuffer (triangle setup happens exactly once -- no per-tile
        // redundancy), then we merge by nearest depth. Buffers persist across frames.
        if ((int)rasterParts_.size() != nThreads) rasterParts_.assign(nThreads, UFrameBuffer());
        for (UFrameBuffer& f : rasterParts_)
        { if (f.nx != nx || f.ny != ny) f.Init(nx, ny); else f.Clear(glm::vec3(0.0f)); }

        const int per = (totalTris + nThreads - 1) / nThreads;
        for (int t = 0; t < nThreads; ++t)
        {
            const int gBegin = t * per, gEnd = std::min(gBegin + per, totalTris);
            if (gBegin >= gEnd) continue;
            pool_.Submit([&, t, gBegin, gEnd]
            {
                UFrameBuffer& tf = rasterParts_[t];
                for (const Draw& d : draws)
                {
                    const int dEnd = d.triBase + d.mesh->triangleCount();
                    if (dEnd <= gBegin || d.triBase >= gEnd) continue;     // slice misses this mesh
                    raster_.DrawMeshShaded(*d.mesh, d.xf, d.ov, sp, flag.shading, tf,
                                           std::max(gBegin, d.triBase) - d.triBase,
                                           std::min(gEnd, dEnd)        - d.triBase);
                }
            });
        }
        pool_.WaitAll();

        // Merge the per-thread buffers into fb_ (nearest depth wins). Row-parallel.
        pool_.ParallelForChunks(ny, [&](int y0, int y1)
        {
            for (int y = y0; y < y1; ++y)
            for (int x = 0; x < nx; ++x)
            {
                const int idx = y * nx + x;
                float best = fb_.depth[idx]; glm::vec3 col = fb_.color[idx];
                for (int t = 0; t < nThreads; ++t)
                {
                    const UFrameBuffer& tf = rasterParts_[t];
                    if (tf.depth[idx] < best) { best = tf.depth[idx]; col = tf.color[idx]; }
                }
                fb_.depth[idx] = best; fb_.color[idx] = col;
            }
        });
    }
    else
    {
        for (const Draw& d : draws)
            raster_.DrawMeshShaded(*d.mesh, d.xf, d.ov, sp, flag.shading, fb_);
    }

    if (flag.depthView) { fb_.ToDepthImage(scene.outputImage); return; }

    // Sky background (matches GPU modes): fill un-covered pixels with the HDRI or
    // the env-light gradient (gamma-corrected to match the lit pixels).
    const bool hasHDRI = sky && sky->ready();
    const glm::vec3 invG(1.0f / 2.2f);
    for (int y = 0; y < ny; ++y)
        for (int x = 0; x < nx; ++x)
        {
            const int idx = y * nx + x;
            if (fb_.depth[idx] < 1.0f) continue;          // covered by geometry
            const glm::vec3 dir = cam.generateRay(x, y, nx, ny).direction;
            glm::vec3 c;
            if (hasHDRI) c = sky->SampleDir(dir);
            else { float k = std::pow(glm::clamp(dir.y * 0.5f + 0.5f, 0.0f, 1.0f), glm::max(sp.skyExp, 0.01f));
                   c = glm::mix(sp.skyHorizon, sp.skyZenith, k); }
            fb_.color[idx] = glm::pow(glm::clamp(c, 0.0f, 1.0f), invG);
        }

    fb_.ToOutputImage(scene.outputImage);
}
