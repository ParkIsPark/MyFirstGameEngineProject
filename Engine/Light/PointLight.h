#pragma once
#include "LightComponent.h"

// Point light. World position comes from the scene-graph transform
// (GetWorldLocation()); color/intensity are inherited from LightComponent.
class PointLight : public LightComponent
{
public:
    PointLight();
    PointLight(glm::vec3 color, glm::vec3 intensity);

    const char* TypeName() const override { return "PointLight"; }
    void        Serialize(FArchive& ar) override;
};
