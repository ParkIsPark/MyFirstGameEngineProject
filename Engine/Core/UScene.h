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

    // Post-process filter applied after CPU ray-tracing (before glDrawPixels).
    // Default values are identity (no change).
    UPostProcessFilter filter;

    UScene();
    ~UScene();
};
