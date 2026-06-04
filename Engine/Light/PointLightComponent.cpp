#include "PointLightComponent.h"
#include "FArchive.h"

REGISTER_COMPONENT("PointLight", PointLightComponent)

PointLightComponent::PointLightComponent()
    : LightComponent(glm::vec3(1.0f), glm::vec3(1.0f))
{
}

PointLightComponent::PointLightComponent(glm::vec3 color, glm::vec3 intensity)
    : LightComponent(color, intensity)
{
}

void PointLightComponent::Serialize(FArchive& ar)
{
    LightComponent::Serialize(ar);   // transform + color/intensity (position is the transform)
}
