#include "LightComponent.h"
#include "FArchive.h"

LightComponent::LightComponent(glm::vec3 color, glm::vec3 intensity)
    : LightColor(color)
    , LightIntensity(intensity)
{
}

void LightComponent::Serialize(FArchive& ar)
{
    USceneComponent::Serialize(ar);
    ar.Color("LightColor",     LightColor);
    ar.Field("LightIntensity", LightIntensity);
}
