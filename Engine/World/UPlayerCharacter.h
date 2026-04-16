#pragma once
#include "AActor.h"

struct GLFWwindow;
class ACamera;

class UPlayerCharacter : public AActor
{
public:
    float moveSpeed = 5.0f;
    float jumpForce = 8.0f;

    UPlayerCharacter();

    // Read WASD + Space, apply movement velocity relative to camera direction.
    void HandleInput(GLFWwindow* window, const ACamera* cam);

    // Move camera to follow this character (third-person, behind + above).
    void UpdateCamera(ACamera* cam) const;
};
