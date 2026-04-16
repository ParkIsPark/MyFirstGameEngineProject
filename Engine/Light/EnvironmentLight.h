#pragma once
#include "LightComponent.h"

// Number of hemisphere sample rays for AO + indirect GI.
// Higher = softer/more accurate, lower = faster.
#define ENV_LIGHT_SAMPLES 4

// Max GI bounce depth. Each bounce spawns ENV_LIGHT_SAMPLES rays,
// so cost = ENV_LIGHT_SAMPLES^GI_BOUNCE_DEPTH per pixel.
// depth=1 -> 8 rays, depth=2 -> 64 rays, depth=3 -> 512 rays.
#define GI_BOUNCE_DEPTH 1

class EnvironmentLight : public LightComponent
{
public:
    // Sky gradient colors — used for both CPU illumination miss branch
    // and GPU skyColor() uniform upload.
    glm::vec3 horizonColor = glm::vec3(0.95f, 0.92f, 0.82f); // warm hazy horizon
    glm::vec3 zenithColor  = glm::vec3(0.30f, 0.60f, 1.00f); // vivid sky blue
    float     skyExp       = 0.6f;                            // pow exponent for gradient curve

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

    // Time-of-day wrapper: tod in [-10, +10] where -10=midnight, 0=sunrise, +10=noon.
    // Adjusts horizonColor, zenithColor, skyExp, LightIntensity, and LightColor.
    static void applyTimeOfDay(EnvironmentLight& light, float tod);
};
