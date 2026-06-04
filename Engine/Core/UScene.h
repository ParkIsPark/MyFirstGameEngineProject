#pragma once
#include <vector>
#include "AActor.h"
#include "../Light/ALight.h"
#include "UPostProcessFilter.h"

class UScene
{
public:
    /** Screen dimensions */
    int width, height;

    std::vector<float> outputImage;

    std::vector<AActor*> Actors;
    std::vector<ALight*> Lights;

    // World render option (EShadingModel order: 0 Flat, 1 Gouraud, 2 Phong).
    // Stored as int to avoid a Core->Rasterizer dependency; serialized in [World].
    int shadingModel = 2;

    // Render mode the world is authored for (0 Rasterizer, 1 GPU RT, 2 Hybrid).
    // The editor toolbar drives it; the standalone game honors it. Serialized in
    // [World] so a saved/played world renders the same way it did in the editor.
    int renderMode = 0;

    // Post-process filter applied after CPU ray-tracing (before glDrawPixels).
    // Default values are identity (no change).
    UPostProcessFilter filter;

    UScene();
    ~UScene();
};
