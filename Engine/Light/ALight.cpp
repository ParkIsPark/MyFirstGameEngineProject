#include "ALight.h"
#include "UScene.h"
#include "ACamera.h"
#include "URay.h"
#include "URayTracing.h"

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

glm::vec3 ALight::illuminate(
    const glm::vec3&   hitPoint,
    const glm::vec3&   normal,
    const AActor*      actor,
    const URay&        ray,
    const UScene&      scene,
    const ACamera&     camera,
    int                depth,
    const URayTracing* tracer) const
{
    if (lightComp)
        return lightComp->illuminate(hitPoint, normal, actor, ray, scene, camera, depth, tracer);
    return glm::vec3(0.0f);
}
