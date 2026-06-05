#include "UPrimitiveComponent.h"
#include "../World/AActor.h"

glm::vec3 UPrimitiveComponent::WorldCenter() const
{
    return owner ? owner->GetActorLocation() + localOffset : localOffset;
}

// Dynamics are identical to the old PhysicalComponent -- this is the promotion
// target, so behaviour stays bit-for-bit the same.
void UPrimitiveComponent::AddForce(const glm::vec3& f)
{
    if (IsStatic()) return;
    force += f;
}

void UPrimitiveComponent::Integrate(float dt)
{
    if (IsStatic() || !owner) return;

    glm::vec3 acceleration = force / mass;
    velocity += acceleration * dt;
    owner->SetActorLocation(owner->GetActorLocation() + velocity * dt);
}

void UPrimitiveComponent::ClearForces()
{
    force = glm::vec3(0.0f);
}
