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
};
