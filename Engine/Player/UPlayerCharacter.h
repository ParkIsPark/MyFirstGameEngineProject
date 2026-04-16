#pragma once
#include "AActor.h"

// Thin "pawn": holds visuals + physics, exposes a tiny API that the
// UPlayerController uses to drive it. All input / camera logic lives in
// the controller, not here.
class UPlayerCharacter : public AActor
{
public:
    UPlayerCharacter();

    // Sets the X/Z components of velocity (horizontal movement). Y is left to
    // gravity / TryJump so walking can't cancel a jump mid-air.
    void SetHorizontalVelocity(float vx, float vz);

    // If the character is touching the ground, sets Y velocity to `jumpForce`.
    // Otherwise does nothing.
    void TryJump(float jumpForce);

    bool IsGrounded() const;
};
