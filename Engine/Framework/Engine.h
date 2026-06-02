#pragma once

#include "FProjectDescriptor.h"
#include "USubsystemManager.h"

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
    virtual ~Engine();

    bool Init(int width = 1024, int height = 1024, const char* title = "Engine");
    int  Run(const char* projPath = nullptr);   // nullptr -> defaults, no world

protected:
    // ---- hooks (override in the app) ----
    virtual void    OnStartup() {}              // once, after GL is ready
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
    void handleResize(int w, int h);
    static void resizeTrampoline(GLFWwindow* win, int w, int h);

    double lastTime_ = 0.0;
};
