#include "UPlayerCharacter.h"
#include "CubeSurface.h"
#include "ACamera.h"
#include "../Physics/PhysicalComponent.h"

#define GLFW_DLL
#include <GLFW/glfw3.h>
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

void UPlayerCharacter::HandleInput(GLFWwindow* window, const ACamera* cam)
{
    if (!physics) return;

    glm::vec3 forward = glm::normalize(glm::vec3(-cam->w.x, 0.0f, -cam->w.z));
    glm::vec3 right   = glm::normalize(glm::vec3( cam->u.x, 0.0f,  cam->u.z));

    glm::vec3 moveDir(0.0f);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) moveDir += forward;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) moveDir -= forward;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveDir -= right;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveDir += right;

    if (glm::length(moveDir) > 0.001f)
        moveDir = glm::normalize(moveDir);

    physics->velocity.x = moveDir.x * moveSpeed;
    physics->velocity.z = moveDir.z * moveSpeed;

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && physics->isGrounded)
        physics->velocity.y = jumpForce;
}

void UPlayerCharacter::UpdateCamera(ACamera* cam) const
{
    glm::vec3 forward = -cam->w;
    cam->eye = position + glm::vec3(0.0f, 1.2f, 0.0f) - forward * 2.5f;
}
