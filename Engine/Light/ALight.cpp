#include "ALight.h"
#include "FArchive.h"

REGISTER_ACTOR("Light", ALight)

// ALight is an actor that owns a LightComponent. The light component is a
// USceneComponent attached under the actor's root, so the light's world
// position follows the actor transform (GetWorldLocation()). On load the world
// serializer recreates the light as a child [Component] and wires lightComp.

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

void ALight::Serialize(FArchive& ar)
{
    AActor::Serialize(ar);   // name + root transform; the light is a child component
}
