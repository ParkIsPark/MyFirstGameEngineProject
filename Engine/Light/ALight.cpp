#include "ALight.h"

// CPU illuminate() removed with the CPU ray tracer (GPU-only lighting now);
// ALight is just an actor that owns a LightComponent for GPU shader assembly.

ALight::ALight()
    : lightComp(nullptr)
{
}

ALight::ALight(LightComponent* comp)
    : lightComp(comp)
{
}

ALight::~ALight()
{
    delete lightComp;
}
