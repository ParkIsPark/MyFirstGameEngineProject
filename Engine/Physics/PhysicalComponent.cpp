#include "PhysicalComponent.h"
#include "../World/AActor.h"

PhysicalComponent::PhysicalComponent(AActor* owner)
    : owner(owner)
{
}

void PhysicalComponent::AddForce(const glm::vec3& f)
{
    if (IsStatic()) return;
    force += f;
}

void PhysicalComponent::Integrate(float dt)
{
    if (IsStatic() || !owner) return;

    glm::vec3 acceleration = force / mass;
    velocity        += acceleration * dt;
    owner->position += velocity * dt;
}

void PhysicalComponent::ClearForces()
{
    force = glm::vec3(0.0f);
}
