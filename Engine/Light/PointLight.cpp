#include "PointLight.h"
#include "FArchive.h"

REGISTER_COMPONENT("PointLight", PointLight)

PointLight::PointLight()
    : LightComponent(glm::vec3(1.0f), glm::vec3(1.0f))
{
}

PointLight::PointLight(glm::vec3 color, glm::vec3 intensity)
    : LightComponent(color, intensity)
{
}

void PointLight::Serialize(FArchive& ar)
{
    LightComponent::Serialize(ar);   // transform + color/intensity (position is the transform)
}
