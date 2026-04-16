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

    // Post-process filter applied after CPU ray-tracing (before glDrawPixels).
    // Default values are identity (no change).
    UPostProcessFilter filter;

    UScene();
    ~UScene();
};
