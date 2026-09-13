#pragma once
#include "AActor.h"
#include "LightComponent.h"

class ALight : public AActor
{
public:
    LightComponent* lightComp;   // non-owning alias into AActor::Components()

    ALight();
    explicit ALight(LightComponent* comp);
    virtual ~ALight() = default;
    // Like AActor's typed setters, throws std::logic_error during lifecycle dispatch.
    void SetLightComponent(LightComponent* comp);

    const char* TypeName() const override { return "Light"; }
    void        Serialize(FArchive& ar) override;   // actor name/transform (light comp = child)
};
