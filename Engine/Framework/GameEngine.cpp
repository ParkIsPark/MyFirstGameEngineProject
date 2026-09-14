#include "GameEngine.h"

#include <GL/glew.h>
#include <cstdio>

#include "UWorld.h"
#include "UScene.h"
#include "ACamera.h"
#include "UPhysicsWorld.h"
#include "FWorldSerializer.h"
#include "FIniFile.h"
#include "FRenderQualitySettings.h"

void GameEngine::OnStartup()
{
    worldRenderer_.Init();   // compile GPU passes now that GL is ready
    backbufferTarget_ = FRenderTarget::DefaultFramebuffer(Width(), Height(), ContextGeneration());
    FIniFile defaults;
    if (defaults.LoadFromFile("Setting/DefaultGame.ini"))
        renderQuality_ = ReadRenderQuality(defaults, "Render", renderQuality_);
    FIniFile overrides;
    if (overrides.LoadFromFile("Config/GameSettings.ini"))
        renderQuality_ = ReadRenderQuality(overrides, "Render", renderQuality_);
}

void GameEngine::OnShutdown()
{
    worldRenderer_.Shutdown();
    backbufferTarget_.Release();
}

UWorld* GameEngine::WorldSetting()
{
    // Empty path -> let Engine::Run boot the .proj's StartupWorld (data-driven).
    if (worldPath_.empty())
    {
        std::printf("[Game] no explicit world -- using project StartupWorld.\n");
        return nullptr;
    }
    UWorld* w = FWorldSerializer::LoadFromFile(worldPath_.c_str(), proj_.defaultRenderFeatures);
    if (!w)
    {
        std::printf("[Game] could not load '%s' -- starting empty.\n", worldPath_.c_str());
        w = new UWorld();
        w->GetScene().renderFeatures = proj_.defaultRenderFeatures;
        return w;
    }
    std::printf("[Game] loaded '%s' (%d actors)\n",
                worldPath_.c_str(), (int)w->GetScene().Actors.size());
    return w;
}

void GameEngine::Render()
{
    UWorld* w = World();
    if (!w) return;

    UScene&  scene = w->GetScene();
    ACamera& cam   = w->GetCamera();
    scene.width  = Width();
    scene.height = Height();

    // The .world load restored eye/yaw/pitch/fov; recompute the frustum planes
    // for the current window aspect (FWorldSerializer does not store l/r/b/t).
    cam.SetOrientation(cam.yaw, cam.pitch);
    cam.SetFOV(cam.fov, (float)Width() / (float)Height());

    if (!backbufferTarget_.Resize(Width(), Height(), ContextGeneration())) return;
    const FBackendSelection& backend =
        ResolveRayTracingBackend(scene.renderFeatures.rayTracingBackend);
    worldRenderer_.Render(*w, cam, backbufferTarget_, scene.renderFeatures,
                          renderQuality_, backend, ContextGeneration());
}
