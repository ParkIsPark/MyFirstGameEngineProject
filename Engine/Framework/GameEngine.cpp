#include "GameEngine.h"

#include <GL/glew.h>
#include <cstdio>

#include "UWorld.h"
#include "UScene.h"
#include "ACamera.h"
#include "FWorldSerializer.h"
#include "FRenderShowFlag.h"

UWorld* GameEngine::WorldSetting()
{
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

    UScene&  scene = w->GetScene();
    ACamera& cam   = w->GetCamera();
    scene.width  = Width();
    scene.height = Height();

    // The .world load restored eye/yaw/pitch/fov; recompute the frustum planes
    // for the current window aspect (FWorldSerializer does not store l/r/b/t).
    cam.SetOrientation(cam.yaw, cam.pitch);
    cam.SetFOV(cam.fov, (float)Width() / (float)Height());

    FRenderShowFlag flag;
    flag.shading = (EShadingModel)scene.shadingModel;     // world's chosen model
    renderer_.RasterShaded(*w, flag);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!scene.outputImage.empty())
        glDrawPixels(Width(), Height(), GL_RGB, GL_FLOAT, scene.outputImage.data());
}
