#include "GameEngine.h"

#include <GL/glew.h>
#include <cstdio>

#include "UWorld.h"
#include "UScene.h"
#include "ACamera.h"
#include "UPhysicsWorld.h"
#include "FWorldSerializer.h"

void GameEngine::OnStartup()
{
    worldRenderer_.Init();   // compile GPU passes now that GL is ready
}

UWorld* GameEngine::WorldSetting()
{
    // Empty path -> let Engine::Run boot the .proj's StartupWorld (data-driven).
    if (worldPath_.empty())
    {
        std::printf("[Game] no explicit world -- using project StartupWorld.\n");
        return nullptr;
    }
    UWorld* w = FWorldSerializer::LoadFromFile(worldPath_.c_str());
    if (!w)
    {
        std::printf("[Game] could not load '%s' -- starting empty.\n", worldPath_.c_str());
        return new UWorld();
    }
    std::printf("[Game] loaded '%s' (%d actors)\n",
                worldPath_.c_str(), (int)w->GetScene().Actors.size());
    return w;
}

void GameEngine::Render()
{
    UWorld* w = World();
    if (!w) return;

    // Play semantics: run with a ground plane so dynamic bodies land (mirrors the
    // editor's in-window PIE). Done once, after the world (explicit or StartupWorld
    // boot) exists. Gravity is whatever the world serialized.
    if (!worldInit_)
    {
        w->GetPhysics().enableFloor = true;
        w->GetPhysics().floorY      = -2.5f;
        worldInit_ = true;
    }

    UScene&  scene = w->GetScene();
    ACamera& cam   = w->GetCamera();
    scene.width  = Width();
    scene.height = Height();

    // The .world load restored eye/yaw/pitch/fov; recompute the frustum planes
    // for the current window aspect (FWorldSerializer does not store l/r/b/t).
    cam.SetOrientation(cam.yaw, cam.pitch);
    cam.SetFOV(cam.fov, (float)Width() / (float)Height());

    worldRenderer_.Render(*w, scene.renderMode, Width(), Height());   // render-mode honored
}
