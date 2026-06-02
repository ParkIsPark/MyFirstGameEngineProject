#pragma once
#include "USurface.h"
#include <glm/glm.hpp>

class UPrimitiveComponent;
class UMeshComponent;

class AActor
{
public:
    AActor();
    ~AActor();

    glm::vec3        position = glm::vec3(0.0f);
    glm::vec3        rotation = glm::vec3(0.0f); // Euler XYZ (degrees)
    glm::vec3        scale    = glm::vec3(1.0f);
    USurface*        surface  = nullptr;         // legacy analytic shape (removed in mesh-first Stage 5)
    UMeshComponent*  mesh     = nullptr;         // mesh-first instance (coexists with surface in Stage 2)
    UPrimitiveComponent* physics = nullptr;      // root primitive: shape (sphere/box) + rigid body

    glm::vec3 GetPosition() const { return position; }

    // Sets surface and wires the owner back-pointer so surface can read position.
    void SetSurface(USurface* s);

    // Sets physics component and wires the owner back-pointer.
    void SetPhysics(UPrimitiveComponent* p);

    virtual void Tick(float DeltaTime);

protected:
    virtual void BeginPlay();
    virtual void EndPlay();
};
