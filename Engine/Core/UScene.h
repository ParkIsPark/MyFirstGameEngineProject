#pragma once
#include <vector>
#include "AActor.h"
#include "../Light/ALight.h"

class UScene
{
public:
    /** Screen dimensions */
    int width, height;

    std::vector<float> outputImage;

    std::vector<AActor*> Actors;
    std::vector<ALight*> Lights;

    UScene();
    ~UScene();
};
