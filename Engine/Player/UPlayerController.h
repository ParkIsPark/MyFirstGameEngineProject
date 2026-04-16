#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

struct GLFWwindow;
class UPlayerCharacter;
class ACamera;

// Unreal-lite player controller.
//
// Owns all control logic: reads input, drives its possessed pawn's velocity,
// and poses the camera behind the pawn every frame.
//
// Minimal setup (main loop):
//     UPlayerController pc;
//     pc.Possess(player);
//     pc.SetCamera(&camera);
//     pc.SetupDefaultBindings();           // WASD + Space + Shift + mouse
//     // pc.cameraDistance = 4.0f;          // tweak camera if desired
//     // pc.cameraOffset   = {0, 1.5f, 0};
//
//     while (!glfwWindowShouldClose(w)) {
//         pc.Tick(w, dt);                  // reads input → moves pawn → moves camera
//         ...
//     }
//
// If you want to bind extra keys, the generic BindAction/BindAxis/BindMouseLook
// API below is available — the default bindings are just one possible layout.

enum class EInputTrigger
{
    Pressed,   // fires once on the down-edge
    Released,  // fires once on the up-edge
    Held       // fires every tick while held
};

class UPlayerController
{
public:
    // --- Possession ---
    void Possess(UPlayerCharacter* pawn);
    void SetCamera(ACamera* cam);

    UPlayerCharacter* GetPawn()   const { return pawn; }
    ACamera*          GetCamera() const { return camera; }

    // --- Movement tuning ---
    float moveSpeed       =  5.0f;   // units/s while walking
    float sprintSpeed     = 10.0f;   // units/s while sprinting
    float jumpForce       =  8.0f;   // Y velocity applied on jump
    float lookSensitivity =  0.1f;   // degrees per pixel of mouse delta

    // --- Camera follow (third-person) ---
    // Camera eye = pawn->position + cameraOffset - cameraForward * cameraDistance.
    // Tweak freely at runtime (e.g. to zoom in/out or change shoulder height).
    glm::vec3 cameraOffset   = glm::vec3(0.0f, 1.2f, 0.0f);
    float     cameraDistance = 2.5f;

    // --- Generic binding API ---
    using ActionFn = std::function<void()>;
    using AxisFn   = std::function<void(float)>;
    using LookFn   = std::function<void(float, float)>;

    void BindAction(int key, EInputTrigger trigger, ActionFn fn);

    // Contributes `scale` to the named axis while `key` is held. Bind a key
    // with +1 and another with -1 to get a [-1..1] axis value.
    void BindAxis(const std::string& axisName, int key, float scale);
    void BindAxisHandler(const std::string& axisName, AxisFn fn);

    // Invoked every Tick with (dx, dy) pixel delta since the previous Tick.
    void BindMouseLook(LookFn fn);

    // Wires the standard WASD + Space + Left-Shift + mouse-look bindings to
    // the controller's built-in semantic handlers. Call after Possess/SetCamera.
    void SetupDefaultBindings();

    // Per-frame update. Samples input, dispatches bindings, writes the
    // resulting velocity to the pawn, and moves the camera to follow.
    void Tick(GLFWwindow* window, float dt);

private:
    // Built-in semantic handlers used by the default binding layout.
    void MoveForward(float value);   // axis [-1..1]: walks along camera forward
    void MoveRight  (float value);   // axis [-1..1]: walks along camera right
    void Jump();                     // action: requests a jump if grounded
    void SetSprinting(bool on);      // action: toggles sprint speed
    void ApplyLook(float dx, float dy);

    // Drives the possessed pawn from the per-tick movement accumulator.
    void ApplyPawnMovement();

    // Places the camera behind the pawn using cameraOffset / cameraDistance.
    void UpdateCameraFollow();

    struct ActionBinding
    {
        int           key;
        EInputTrigger trigger;
        ActionFn      fn;
    };

    struct AxisContribution
    {
        int   key;
        float scale;
    };

    struct AxisBinding
    {
        std::vector<AxisContribution> contribs;
        std::vector<AxisFn>           handlers;
    };

    UPlayerCharacter* pawn   = nullptr;
    ACamera*          camera = nullptr;

    std::vector<ActionBinding>                   actions;
    std::unordered_map<std::string, AxisBinding> axes;
    std::unordered_map<int, bool>                keyPrev;
    std::vector<LookFn>                          lookHandlers;

    // Per-tick state.
    glm::vec3 pendingMove = glm::vec3(0.0f);
    bool      bSprinting  = false;

    bool   hasLastMouse = false;
    double lastMouseX   = 0.0;
    double lastMouseY   = 0.0;
};
