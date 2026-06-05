#pragma once
#include <string>
#include "LightComponent.h"

// Environment (sky/ambient) light component. Position-less; drives the sky
// gradient / HDRI used by the GPU shading paths' skyColor() + hemisphere ambient.
// (Serialized TypeName stays "EnvLight" for .world back-compat.)
class EnvironmentLightComponent : public LightComponent
{
public:
    glm::vec3 horizonColor = glm::vec3(0.95f, 0.92f, 0.82f); // warm hazy horizon
    glm::vec3 zenithColor  = glm::vec3(0.30f, 0.60f, 1.00f); // vivid sky blue
    float     skyExp       = 0.6f;                            // gradient curve exponent
    // Equirectangular HDRI image for the sky (Content path; empty = gradient).
    // This makes the sky image an actor-placed asset that serializes with the
    // world, instead of only the global UScene::skyHDRI.
    std::string skyTexPath;

    EnvironmentLightComponent();
    EnvironmentLightComponent(glm::vec3 color, glm::vec3 intensity);

    const char* TypeName() const override { return "EnvLight"; }
    void        Serialize(FArchive& ar) override;

    // Time-of-day preset: tod in [-10,+10] (-10 midnight, 0 sunrise, +10 noon).
    static void applyTimeOfDay(EnvironmentLightComponent& light, float tod);
};
