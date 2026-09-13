#pragma once

#include "FProjectDescriptor.h"
#include "USubsystemManager.h"
#include "../Render/FGraphicsCapabilities.h"
#include <cstdint>

struct GLFWwindow;
class  UWorld;

// ---------------------------------------------------------------------------
// Engine (E1–E4) — runtime that absorbs the GLFW window + GL context + main
// loop + resize, and drives a project/world lifecycle.
//
// Minimal use (single-draw demo):
//     MyApp app;                       // overrides Render()
//     if (!app.Init()) return -1;
//     return app.Run();                // no project -> just loops Render()
//
// Full lifecycle (project + world):
//     MyGame game;                     // overrides WorldSetting()
//     if (!game.Init()) return -1;
//     return game.Run("My.proj");      // load .proj -> build world -> loop
//
// Run() flow:  OnStartup -> WorldSetting -> subsystems.InitAll ->
//              world.BeginPlay -> loop{ TickAll -> world.Tick -> Render } ->
//              world.EndPlay -> subsystems.ShutdownAll.
// No global state: the resize callback dispatches through a GLFW user-pointer
// trampoline to a member.
// ---------------------------------------------------------------------------
class Engine
{
public:
    enum class Role { Game, Editor };
    explicit Engine(Role role = Role::Game);
    virtual ~Engine();

    bool Init(int width = 1024, int height = 1024, const char* title = "Engine");
    int  Run(const char* projPath = nullptr);   // nullptr -> defaults, no world

    const FGraphicsCapabilities& GraphicsCapabilities() const { return graphicsCapabilities_; }
    const FBackendSelection& RayTracingBackendSelection() const { return backendSelection_; }
    const FBackendSelection& ResolveRayTracingBackend(ERayTracingBackend requested);
    std::uint64_t ContextGeneration() const { return contextGeneration_; }

protected:
    void BootWorld(const std::string& projectRoot);
    // ---- hooks (override in the app) ----
    virtual void    OnStartup() {}              // once, after GL is ready
    virtual void    OnShutdown() {}             // finish derived worlds before subsystem shutdown
    virtual UWorld* WorldSetting() { return nullptr; } // build world + spawn (code hook)
    virtual void    Tick(float dt);             // world_->Tick(dt) then Render()
    virtual void    Render() {}                 // draw the frame (every frame)
    virtual void    OnResize(int /*w*/, int /*h*/) {}

    int  Width()  const { return width_; }
    int  Height() const { return height_; }
    bool KeyDown(int glfwKey) const;            // wraps glfwGetKey (app input)
    UWorld* World() const { return world_; }

    GLFWwindow*        window_ = nullptr;
    int                width_  = 1024;
    int                height_ = 1024;
    FProjectDescriptor proj_;
    UWorld*            world_ = nullptr;
    USubsystemManager  subsystems_;

private:
    const Role role_;
    void cleanupGraphics();
    void resolveRayTracingBackend();
    void handleResize(int w, int h);
    static void resizeTrampoline(GLFWwindow* win, int w, int h);

    double lastTime_ = 0.0;
    bool glfwInitialized_ = false;
    FBackendWarningDeduplicator backendWarnings_;
    FGraphicsCapabilities graphicsCapabilities_;
    FBackendSelection backendSelection_;
    std::uint64_t contextGeneration_ = 0;
};
