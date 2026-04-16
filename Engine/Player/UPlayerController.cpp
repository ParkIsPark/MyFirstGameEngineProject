#include "UPlayerController.h"
#include "UPlayerCharacter.h"
#include "ACamera.h"

#define GLFW_DLL
#include <GLFW/glfw3.h>

// ---- Possession --------------------------------------------------------------

void UPlayerController::Possess(UPlayerCharacter* p) { pawn   = p; }
void UPlayerController::SetCamera(ACamera* c)        { camera = c; }

// ---- Binding API -------------------------------------------------------------

void UPlayerController::BindAction(int key, EInputTrigger trigger, ActionFn fn)
{
    actions.push_back({ key, trigger, std::move(fn) });
    keyPrev.emplace(key, false);
}

void UPlayerController::BindAxis(const std::string& axisName, int key, float scale)
{
    axes[axisName].contribs.push_back({ key, scale });
}

void UPlayerController::BindAxisHandler(const std::string& axisName, AxisFn fn)
{
    axes[axisName].handlers.push_back(std::move(fn));
}

void UPlayerController::BindMouseLook(LookFn fn)
{
    lookHandlers.push_back(std::move(fn));
}

void UPlayerController::SetupDefaultBindings()
{
    BindAxis("MoveForward", GLFW_KEY_W,  1.0f);
    BindAxis("MoveForward", GLFW_KEY_S, -1.0f);
    BindAxis("MoveRight",   GLFW_KEY_D,  1.0f);
    BindAxis("MoveRight",   GLFW_KEY_A, -1.0f);

    BindAxisHandler("MoveForward", [this](float v){ MoveForward(v); });
    BindAxisHandler("MoveRight",   [this](float v){ MoveRight  (v); });

    BindAction(GLFW_KEY_SPACE,      EInputTrigger::Pressed,  [this]{ Jump(); });
    BindAction(GLFW_KEY_LEFT_SHIFT, EInputTrigger::Pressed,  [this]{ SetSprinting(true);  });
    BindAction(GLFW_KEY_LEFT_SHIFT, EInputTrigger::Released, [this]{ SetSprinting(false); });

    BindMouseLook([this](float dx, float dy){ ApplyLook(dx, dy); });
}

// ---- Per-frame tick ---------------------------------------------------------

void UPlayerController::Tick(GLFWwindow* window, float /*dt*/)
{
    pendingMove = glm::vec3(0.0f);

    // Actions — edge-detected against prev-frame snapshot.
    for (auto& b : actions)
    {
        const bool down = glfwGetKey(window, b.key) == GLFW_PRESS;
        const bool prev = keyPrev[b.key];

        switch (b.trigger)
        {
        case EInputTrigger::Pressed:  if ( down && !prev) b.fn(); break;
        case EInputTrigger::Released: if (!down &&  prev) b.fn(); break;
        case EInputTrigger::Held:     if ( down)          b.fn(); break;
        }
    }
    // Snapshot update after all actions dispatched, so multiple bindings on
    // the same key see the same edge.
    for (auto& kv : keyPrev)
        kv.second = glfwGetKey(window, kv.first) == GLFW_PRESS;

    // Axes — sum contribs, dispatch (handler is called with 0.0 when no
    // contributor is held, which lets the default handlers zero-out velocity).
    for (auto& kv : axes)
    {
        float value = 0.0f;
        for (auto& c : kv.second.contribs)
            if (glfwGetKey(window, c.key) == GLFW_PRESS)
                value += c.scale;

        for (auto& h : kv.second.handlers)
            h(value);
    }

    // Mouse look — deliver pixel delta since last tick.
    if (!lookHandlers.empty())
    {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        if (hasLastMouse)
        {
            const float dx = static_cast<float>(mx - lastMouseX);
            const float dy = static_cast<float>(my - lastMouseY);
            for (auto& h : lookHandlers) h(dx, dy);
        }
        lastMouseX   = mx;
        lastMouseY   = my;
        hasLastMouse = true;
    }

    // Push the accumulated movement into the pawn and follow with the camera.
    ApplyPawnMovement();
    UpdateCameraFollow();
}

// ---- Built-in semantic handlers ---------------------------------------------

void UPlayerController::MoveForward(float value)
{
    if (value == 0.0f || !camera) return;
    const glm::vec3 forward = glm::normalize(glm::vec3(-camera->w.x, 0.0f, -camera->w.z));
    pendingMove += forward * value;
}

void UPlayerController::MoveRight(float value)
{
    if (value == 0.0f || !camera) return;
    const glm::vec3 right = glm::normalize(glm::vec3(camera->u.x, 0.0f, camera->u.z));
    pendingMove += right * value;
}

void UPlayerController::Jump()
{
    if (pawn) pawn->TryJump(jumpForce);
}

void UPlayerController::SetSprinting(bool on)
{
    bSprinting = on;
}

void UPlayerController::ApplyLook(float dx, float dy)
{
    if (!camera) return;
    const float newYaw   = camera->yaw   + dx * lookSensitivity;
    const float newPitch = camera->pitch - dy * lookSensitivity;  // invert → mouse-up = look-up
    camera->SetOrientation(newYaw, newPitch);
}

// ---- Pawn / camera drive ----------------------------------------------------

void UPlayerController::ApplyPawnMovement()
{
    if (!pawn) return;

    glm::vec3 dir = pendingMove;
    if (glm::length(dir) > 0.001f)
        dir = glm::normalize(dir);

    const float speed = bSprinting ? sprintSpeed : moveSpeed;
    pawn->SetHorizontalVelocity(dir.x * speed, dir.z * speed);
}

void UPlayerController::UpdateCameraFollow()
{
    if (!camera || !pawn) return;
    const glm::vec3 forward = -camera->w;
    camera->eye = pawn->position + cameraOffset - forward * cameraDistance;
}
