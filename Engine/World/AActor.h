#pragma once
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
    UMeshComponent*  mesh     = nullptr;         // mesh-first render instance
    UPrimitiveComponent* physics = nullptr;      // root primitive: shape (sphere/box) + rigid body

    glm::vec3 GetPosition() const { return position; }

    // Sets physics component and wires the owner back-pointer.
    void SetPhysics(UPrimitiveComponent* p);

    virtual void Tick(float DeltaTime);

protected:
    virtual void BeginPlay();
    virtual void EndPlay();
};
