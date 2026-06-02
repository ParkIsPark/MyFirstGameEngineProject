#pragma once
#include "LightComponent.h"

// Number of shadow samples per point light (1 center + N-1 edge samples).
// Increasing this gives smoother penumbra at the cost of more shadow rays.
#define SOFT_SHADOW_SAMPLES 3   // 9 for offline quality, 3 for GPU real-time

// Radius of the area-light disk used for soft shadows (world units).
#define SOFT_SHADOW_RADIUS  0.7f

class PointLight : public LightComponent
{
public:
    glm::vec3 LightPos;

    PointLight();
    PointLight(glm::vec3 pos, glm::vec3 color, glm::vec3 intensity);

    LightGLSLInfo getGLSLInfo() const override;
};
