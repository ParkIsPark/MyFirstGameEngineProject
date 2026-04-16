#include "UPlayerCharacter.h"
#include "CubeSurface.h"
#include "PhysicalComponent.h"

#include <glm/glm.hpp>

UPlayerCharacter::UPlayerCharacter()
{
    auto* cube = new CubeSurface(glm::vec3(0.4f, 0.9f, 0.4f));
    cube->material.ka        = glm::vec3(0.10f, 0.10f, 0.18f);
    cube->material.kd        = glm::vec3(0.30f, 0.45f, 0.90f);
    cube->material.ks        = glm::vec3(0.40f);
    cube->material.shininess = 32.0f;
    cube->material.km        = glm::vec3(0.05f);
    SetSurface(cube);

    auto* phys = new PhysicalComponent(this);
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
