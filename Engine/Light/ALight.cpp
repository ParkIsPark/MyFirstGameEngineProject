#include "ALight.h"

// ALight is an actor that owns a LightComponent. The light component is a
// USceneComponent attached under the actor's root, so the light's world
// position follows the actor transform (GetWorldLocation()).

ALight::ALight()
    : lightComp(nullptr)
{
}

ALight::ALight(LightComponent* comp)
    : lightComp(comp)
{
    if (comp)
    {
        comp->owner = this;
        comp->AttachTo(&rootComponent);
    }
}

ALight::~ALight()
{
    delete lightComp;
}
