#pragma once
#include "LightComponent.h"

// Point light component. World position comes from the scene-graph transform
// (GetWorldLocation()); color/intensity are inherited from LightComponent.
// (Serialized TypeName stays "PointLight" for .world back-compat.)
class PointLightComponent : public LightComponent
{
public:
    PointLightComponent();
    PointLightComponent(glm::vec3 color, glm::vec3 intensity);

    const char* TypeName() const override { return "PointLight"; }
    void        Serialize(FArchive& ar) override;
};
