#pragma once
#include <glm/glm.hpp>
#include "USceneComponent.h"

class FArchive;

// ---------------------------------------------------------------------------
// LightComponent — base light. Now a USceneComponent, so a light has a
// transform and lives in the scene graph; its world position is
// GetWorldLocation(). (The old getGLSLInfo() GPU shader-assembly machinery was
// dead code -- no callers -- and was removed in P5.)
// ---------------------------------------------------------------------------
class LightComponent : public USceneComponent
{
public:
    glm::vec3 LightColor     = glm::vec3(1.0f);
    glm::vec3 LightIntensity = glm::vec3(1.0f);

    LightComponent(glm::vec3 color = glm::vec3(1.0f), glm::vec3 intensity = glm::vec3(1.0f));

    void Serialize(FArchive& ar) override;   // base transform + color/intensity
};
