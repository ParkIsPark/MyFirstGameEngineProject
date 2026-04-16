#pragma once
#include "AActor.h"
#include "LightComponent.h"

class ALight : public AActor
{
public:
    LightComponent* lightComp;

    ALight();
    explicit ALight(LightComponent* comp);
    virtual ~ALight();

    virtual glm::vec3 illuminate(
        const glm::vec3&   hitPoint,
        const glm::vec3&   normal,
        const AActor*      actor,
        const URay&        ray,
        const UScene&      scene,
        const ACamera&     camera,
        int                depth,
        const URayTracing* tracer) const;
};
