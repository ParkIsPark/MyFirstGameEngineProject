#pragma once
#include "LightComponent.h"

// Number of hemisphere sample rays for AO + indirect GI.
// Higher = softer/more accurate, lower = faster.
#define ENV_LIGHT_SAMPLES 8

// Max GI bounce depth. Each bounce spawns ENV_LIGHT_SAMPLES rays,
// so cost = ENV_LIGHT_SAMPLES^GI_BOUNCE_DEPTH per pixel.
// depth=1 -> 8 rays, depth=2 -> 64 rays, depth=3 -> 512 rays.
#define GI_BOUNCE_DEPTH 2

class EnvironmentLight : public LightComponent
{
public:
    EnvironmentLight();
    EnvironmentLight(glm::vec3 color, glm::vec3 intensity);

    glm::vec3     illuminate(
        const glm::vec3&   hitPoint,
        const glm::vec3&   normal,
        const AActor*      actor,
        const URay&        ray,
        const UScene&      scene,
        const ACamera&     camera,
        int                depth,
        const URayTracing* tracer) const override;

    LightGLSLInfo getGLSLInfo() const override;
};
