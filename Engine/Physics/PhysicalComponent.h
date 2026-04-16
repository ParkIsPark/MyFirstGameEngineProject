#pragma once
#include <glm/glm.hpp>

class AActor;

class PhysicalComponent
{
public:
    float mass        = 1.0f;   // 0 = static object (immovable)
    float restitution = 0.5f;   // bounciness: 0=inelastic, 1=perfectly elastic
    float friction    = 0.3f;   // surface friction coefficient

    bool bAffectedByGravity = true;

    glm::vec3 velocity   = glm::vec3(0.0f);
    glm::vec3 force      = glm::vec3(0.0f); // accumulated per frame
    bool      isGrounded = false;           // set by UPhysicsWorld on floor contact

    AActor* owner = nullptr;

    PhysicalComponent() = default;
    explicit PhysicalComponent(AActor* owner);

    bool IsStatic() const { return mass == 0.0f; }

    void AddForce(const glm::vec3& f);
    void Integrate(float dt);
    void ClearForces();
};
