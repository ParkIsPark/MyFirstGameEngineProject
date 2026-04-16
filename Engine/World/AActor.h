#pragma once
#include "USurface.h"
#include <glm/glm.hpp>

class PhysicalComponent;

class AActor
{
public:
    AActor();
    ~AActor();

    glm::vec3        position = glm::vec3(0.0f);
    USurface*        surface  = nullptr;
    PhysicalComponent* physics = nullptr;

    glm::vec3 GetPosition() const { return position; }

    // Sets surface and wires the owner back-pointer so surface can read position.
    void SetSurface(USurface* s);

    // Sets physics component and wires the owner back-pointer.
    void SetPhysics(PhysicalComponent* p);

    virtual void Tick(float DeltaTime);

protected:
    virtual void BeginPlay();
    virtual void EndPlay();
};
