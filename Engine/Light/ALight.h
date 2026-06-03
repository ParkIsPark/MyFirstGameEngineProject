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

    const char* TypeName() const override { return "Light"; }
    void        Serialize(FArchive& ar) override;   // actor name/transform (light comp = child)
};
