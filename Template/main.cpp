// main.cpp -- entry point for a project built on MyFirstGameEngine.
//
// The engine (Engine.h) owns the GLFW window, GL context, main loop and resize
// handling -- no global state, no GL bootstrap boilerplate. Subclass Engine and
// override only the hooks you need, then call Init() + Run():
//
//   OnStartup()        once, after GL is ready
//   WorldSetting()     build a UWorld and spawn actors (return it)
//   Tick(float dt)     per-frame update (default: world->Tick then Render)
//   Render()           draw the frame
//   OnResize(w, h)     viewport changed
//
// See Engine.h for the full lifecycle, and the engine's Test demo / EditorEngine
// for examples that build a world and drive URenderer.

#include <GL/glew.h>
#include "Engine.h"

class MyApp : public Engine
{
protected:
    void Render() override
    {
        // Minimal frame: clear to a dark background. Replace with your own
        // rendering -- spawn a world in WorldSetting() and let URenderer draw it.
        glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
};

int main()
{
    MyApp app;
    if (!app.Init(1280, 720, "My Project")) return -1;
    return app.Run();   // no .proj -> default loop calling Render() each frame
}
