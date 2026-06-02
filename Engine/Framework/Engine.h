#pragma once
#include <vector>

struct GLFWwindow;

// ---------------------------------------------------------------------------
// Engine (E1) — minimal runtime that absorbs the GLFW window + GL context +
// main loop + resize handling that every demo's main.cpp used to copy-paste.
//
// A game/demo subclasses it and overrides Render() (fill OutputImage() with
// RGB floats) and optionally OnStartup()/OnResize(). main() becomes:
//
//     MyApp app;
//     if (!app.Init()) return -1;
//     return app.Run();
//
// (Later stages add USubsystemManager / UWorld / URenderer / .proj loading;
//  E1 is just the boilerplate absorption -- no global variables, member
//  resize callback via a GLFW user-pointer trampoline.)
// ---------------------------------------------------------------------------
class Engine
{
public:
    virtual ~Engine();

    // Create window + GL context. Returns false on failure.
    bool Init(int width = 1024, int height = 1024, const char* title = "Engine");

    // Run the main loop until the window closes (ESC / Q). Returns exit code.
    int  Run();

protected:
    // ---- hooks (override in the app) ----
    virtual void OnStartup() {}            // once, after GL is ready (build scene)
    virtual void Render() = 0;             // fill OutputImage() (size w*h*3 RGB)
    virtual void OnResize(int /*w*/, int /*h*/) {}

    int  Width()  const { return width_; }
    int  Height() const { return height_; }
    std::vector<float>& OutputImage() { return outputImage_; }

    GLFWwindow* window_ = nullptr;
    int width_  = 1024;
    int height_ = 1024;
    std::vector<float> outputImage_;

private:
    void handleResize(int w, int h);
    static void resizeTrampoline(GLFWwindow* win, int w, int h);
};
