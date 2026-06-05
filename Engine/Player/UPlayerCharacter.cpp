#include "UPlayerCharacter.h"
#include "UBoxComponent.h"

#include <glm/glm.hpp>

UPlayerCharacter::UPlayerCharacter()
{
    // Visual mesh is attached later (mesh-first); here we set up the collider.
    // Box collider half-extents (capsule comes in P6).
    auto* phys = new UBoxComponent(this);
    phys->halfExtents        = glm::vec3(0.4f, 0.9f, 0.4f);
    phys->mass               = 70.0f;
    phys->restitution        = 0.05f;
    phys->friction           = 0.8f;
    phys->bAffectedByGravity = true;
    SetPhysics(phys);
}

void UPlayerCharacter::SetHorizontalVelocity(float vx, float vz)
{
    if (!physics) return;
    physics->velocity.x = vx;
    physics->velocity.z = vz;
}

void UPlayerCharacter::TryJump(float jumpForce)
{
    if (physics && physics->isGrounded)
        physics->velocity.y = jumpForce;
}

bool UPlayerCharacter::IsGrounded() const
{
    return physics && physics->isGrounded;
}
