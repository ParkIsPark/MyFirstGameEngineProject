#include "UWorldRenderer.h"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <vector>

#include "UWorld.h"
#include "UScene.h"
#include "ACamera.h"
#include "AActor.h"
#include "UMesh.h"
#include "UMeshComponent.h"
#include "ALight.h"
#include "PointLightComponent.h"
#include "EnvironmentLightComponent.h"
#include "FTransform.h"
#include "FRenderShowFlag.h"
#include "FIniFile.h"

void UWorldRenderer::Init()
{
    worldRT_.Init();
    hybrid_.Init();
    ready_ = true;
}

void UWorldRenderer::Render(UWorld& world, int mode, int w, int h)
{
    if (mode == 0 || !ready_) renderRaster(world, w, h);
    else                      renderGPU(world, mode, w, h);
}

void UWorldRenderer::renderRaster(UWorld& world, int w, int h)
{
    UScene& scene = world.GetScene();
    scene.width = w; scene.height = h;

    FRenderShowFlag flag;
    flag.shading = (EShadingModel)scene.shadingModel;
    sky_.GetOrLoad(scene.skyHDRI);
    raster_.RasterShaded(world, flag, &sky_);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!scene.outputImage.empty())
        glDrawPixels(w, h, GL_RGB, GL_FLOAT, scene.outputImage.data());
}

void UWorldRenderer::renderGPU(UWorld& world, int mode, int w, int h)
{
    UScene&  scene = world.GetScene();
    ACamera& cam   = world.GetCamera();

    std::vector<const UMesh*> meshes;
    std::vector<glm::mat4>    models;
    std::vector<glm::vec3>    albedos;
    std::vector<float>        mirrors;
    std::vector<const Material*> mats;
    std::vector<glm::vec3>    lightPos, lightColor;
    for (AActor* a : scene.Actors)
    {
        if (UMeshComponent* mc = a->mesh)
            if (mc->mesh)
            {
                meshes.push_back(mc->mesh);
                models.push_back(mc->GetWorldMatrix());
                const Material& mat = mc->GetMaterial();   // shared asset > override > mesh default
                albedos.push_back(mat.kd);
                mirrors.push_back(glm::max(mat.km.x, glm::max(mat.km.y, mat.km.z)));
                mats.push_back(&mat);
            }
        if (ALight* L = dynamic_cast<ALight*>(a))
            if (PointLightComponent* pl = dynamic_cast<PointLightComponent*>(L->lightComp))
            { lightPos.push_back(pl->GetWorldLocation()); lightColor.push_back(pl->LightColor * pl->LightIntensity); }
    }
    if (lightPos.empty()) { lightPos = { glm::vec3(6, 8, 2) }; lightColor = { glm::vec3(1.0f) }; }

    // Geometry signature (mesh identity + world transform + albedo) -> skip the
    // BVH/TBO rebuild while the scene is static.
    size_t geomSig = 1469598103934665603ull;
    {
        auto mix = [&](const void* p, size_t n) {
            const unsigned char* b = static_cast<const unsigned char*>(p);
            for (size_t i = 0; i < n; ++i) { geomSig ^= b[i]; geomSig *= 1099511628211ull; }
        };
        for (size_t i = 0; i < meshes.size(); ++i)
        { mix(&meshes[i], sizeof(meshes[i])); mix(&models[i], sizeof(glm::mat4)); mix(&albedos[i], sizeof(glm::vec3)); mix(&mirrors[i], sizeof(float));
          size_t ts = mats[i] ? mats[i]->texData.size() : 0; mix(&ts, sizeof(ts)); }
    }

    const unsigned int skyTex = sky_.GetOrLoad(scene.skyHDRI);

    // Game render profile (GI/shadow/reflection quality) -- loaded once from the
    // ini the editor wrote, so a packaged build honors the Game settings tab.
    if (!qualityLoaded_)
    {
        FIniFile ini;
        if (ini.LoadFromFile("Config/GameSettings.ini"))
        {
            quality_.giSamples      = ini.GetInt  ("Render", "GISamples", quality_.giSamples);
            quality_.giBounces      = ini.GetInt  ("Render", "GIBounces", quality_.giBounces);
            quality_.giStrength     = ini.GetFloat("Render", "GIStrength", quality_.giStrength);
            quality_.reflStrength   = ini.GetFloat("Render", "ReflectionStrength", quality_.reflStrength);
            quality_.shininess      = ini.GetFloat("Render", "Shininess", quality_.shininess);
            quality_.shadowSamples  = ini.GetInt  ("Render", "ShadowSamples", quality_.shadowSamples);
            quality_.shadowSoftness = ini.GetFloat("Render", "ShadowSoftness", quality_.shadowSoftness);
        }
        qualityLoaded_ = true;
    }
    const FRenderQuality& q = quality_;

    // Environment light -> hemisphere GI + sky gradient (both GPU passes).
    int giN = 0;
    glm::vec3 envTint(1.0f), horizon(0.10f, 0.12f, 0.16f), zenith(0.40f, 0.55f, 0.80f);
    float skyExp = 1.0f;
    for (AActor* a : scene.Actors)
        if (ALight* L = dynamic_cast<ALight*>(a))
            if (auto* el = dynamic_cast<EnvironmentLightComponent*>(L->lightComp))
            { giN = q.giSamples; envTint = el->LightColor * el->LightIntensity; horizon = el->horizonColor;
              zenith = el->zenithColor; skyExp = el->skyExp; break; }
    envTint *= q.giStrength;
    worldRT_.SetGI(giN, envTint, horizon, zenith, skyExp, q.giBounces);
    hybrid_.SetGI(giN, envTint, horizon, zenith, skyExp, q.giBounces);
    worldRT_.SetQuality(q.reflStrength, q.shininess);
    hybrid_.SetQuality(q.shininess);
    worldRT_.SetShadow(q.shadowSamples, q.shadowSoftness);
    hybrid_.SetShadow(q.shadowSamples, q.shadowSoftness);

    glViewport(0, 0, w, h);
    glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (mode == 1)                                   // GPU RT
    {
        if (!rtUp_ || geomSig != rtSig_)
        {
            worldRT_.UploadWorld(meshes, models, albedos, lightPos[0], lightColor[0], mirrors, mats);
            rtSig_ = geomSig; rtUp_ = true;
        }
        worldRT_.SetLights(lightPos, lightColor);
        worldRT_.SetSky(skyTex);
        worldRT_.RenderFrame(cam, w, h);
    }
    else                                             // Hybrid: CPU G-buffer + GPU shadow
    {
        gbuf_.Init(w, h); gbuf_.Clear();

        std::vector<FTransform> xfs(meshes.size());
        for (size_t i = 0; i < meshes.size(); ++i)
        {
            xfs[i].model    = models[i];
            xfs[i].view     = FTransform::MakeView(cam);
            xfs[i].proj     = FTransform::MakeProjFCG(cam.l, cam.r, cam.b, cam.t, -cam.d, -1000.0f);
            xfs[i].viewport = FTransform::MakeViewport(w, h);
        }

        if (!hyUp_ || geomSig != hySig_)
        {
            std::vector<glm::vec3> tris;
            for (size_t i = 0; i < meshes.size(); ++i)
            {
                const int nt = meshes[i]->triangleCount();
                for (int t = 0; t < nt; ++t)
                    for (int k = 0; k < 3; ++k)
                        tris.push_back(glm::vec3(models[i] * glm::vec4(meshes[i]->vertices[meshes[i]->indices[3*t+k]].position, 1.0f)));
            }
            hybrid_.UploadSceneTriangles(tris);
            hySig_ = geomSig; hyUp_ = true;
        }

        const int TILE = 64;
        const int ntx  = (w + TILE - 1) / TILE;
        const int nty  = (h + TILE - 1) / TILE;
        const int nTiles = ntx * nty;
        pool_.ParallelForChunks(nTiles, [&](int begin, int end)
        {
            for (int tile = begin; tile < end; ++tile)
            {
                const int tx = tile % ntx, ty = tile / ntx;
                const int cx0 = tx * TILE, cy0 = ty * TILE;
                const int cx1 = std::min(cx0 + TILE - 1, w - 1);
                const int cy1 = std::min(cy0 + TILE - 1, h - 1);
                for (size_t i = 0; i < meshes.size(); ++i)
                    rast_.DrawMeshGBuffer(*meshes[i], xfs[i], albedos[i], gbuf_,
                                          cx0, cy0, cx1, cy1, /*countStats=*/false, mats[i]);
            }
        });

        hybrid_.SetSky(skyTex);
        hybrid_.UploadGBuffer(gbuf_);
        hybrid_.Render(cam, lightPos, lightColor, w, h);
    }
}
