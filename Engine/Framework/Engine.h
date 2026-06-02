#pragma once

struct GLFWwindow;

// ---------------------------------------------------------------------------
// Engine (E1) — minimal runtime that absorbs the GLFW window + GL context +
// main loop + resize that every demo's main.cpp used to copy-paste.
//
// A game/demo subclasses it and overrides Render() (draw the frame -- either
// glDrawPixels of a CPU buffer, or a GL draw call for GPU rendering) plus
// optional OnStartup()/OnResize(). main() becomes:
//
//     MyApp app;
//     if (!app.Init()) return -1;
//     return app.Run();
//
// (Later stages add USubsystemManager / UWorld / URenderer / .proj loading;
//  E1 is just the boilerplate absorption -- no globals, member resize callback
//  via a GLFW user-pointer trampoline.)
// ---------------------------------------------------------------------------
class Engine
{
public:
    virtual ~Engine();

    bool Init(int width = 1024, int height = 1024, const char* title = "Engine");
    int  Run();

protected:
    // ---- hooks (override in the app) ----
    virtual void OnStartup() {}                 // once, after GL is ready
    virtual void Render()    = 0;               // draw the frame (called every frame)
    virtual void OnResize(int /*w*/, int /*h*/) {}

    int  Width()  const { return width_; }
    int  Height() const { return height_; }
    bool KeyDown(int glfwKey) const;            // wraps glfwGetKey (app input)

    GLFWwindow* window_ = nullptr;
    int width_  = 1024;
    int height_ = 1024;

private:
    void handleResize(int w, int h);
    static void resizeTrampoline(GLFWwindow* win, int w, int h);
};
