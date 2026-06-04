#include "Engine.h"
#include "UWorld.h"
#include "FWorldSerializer.h"

#include <iostream>
#include <filesystem>
#include <GL/glew.h>

#define GLFW_INCLUDE_GLU
#define GLFW_DLL
#include <GLFW/glfw3.h>

Engine::~Engine()
{
    delete world_;
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
}

bool Engine::Init(int width, int height, const char* title)
{
    width_  = width;
    height_ = height;

    if (!glfwInit()) return false;

    // Request a 3.3 *compatibility* context. 3.3 is the engine's GPU baseline
    // (all shaders are #version 330; the mesh ray tracer feeds triangles via a
    // texture buffer, core since 3.1 -- no SSBO/4.3 assumption). Compatibility
    // keeps fixed-function glOrtho/glDrawPixels alive for the CPU raster path.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);

    window_ = glfwCreateWindow(width_, height_, title, nullptr, nullptr);
    if (!window_) { glfwTerminate(); return false; }
    glfwMakeContextCurrent(window_);

    if (glewInit() != GLEW_OK) { std::cerr << "GLEW init failed\n"; return false; }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    // member resize callback via user-pointer trampoline (no global state)
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, &Engine::resizeTrampoline);
    return true;
}

bool Engine::KeyDown(int glfwKey) const
{
    return window_ && glfwGetKey(window_, glfwKey) == GLFW_PRESS;
}

void Engine::resizeTrampoline(GLFWwindow* win, int w, int h)
{
    static_cast<Engine*>(glfwGetWindowUserPointer(win))->handleResize(w, h);
}

void Engine::handleResize(int w, int h)
{
    width_  = w;
    height_ = h;
    glViewport(0, 0, w, h);
    // fixed-function ortho kept for the glDrawPixels (CPU buffer) path
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(w), 0.0, static_cast<double>(h), 1.0, -1.0);
    OnResize(w, h);
}

void Engine::Tick(float dt)
{
    if (world_) world_->Tick(dt);
    Render();
}

int Engine::Run(const char* projPath)
{
    // Two-stage data-driven boot (P7): the .proj manifest (ProjectName/Engine
    // Version) + the project's Setting/DefaultEngine.ini ([Display]/[Render]/
    // [Startup]); fall back to a legacy flat .proj. Missing/garbage -> defaults
    // (never crash). Window size/title applied only when something loaded, so the
    // no-project demo path keeps the size passed to Init().
    std::string projDir;
    if (projPath)
    {
        namespace fs = std::filesystem;
        proj_.LoadProject(projPath);                       // identity (manifest)
        projDir = fs::path(projPath).parent_path().string();

        const std::string settingIni =
            (fs::path(projDir) / "Setting" / "DefaultEngine.ini").string();
        bool loaded = proj_.LoadSettings(settingIni.c_str());
        if (!loaded) loaded = proj_.LoadFromFile(projPath);   // legacy flat .proj

        if (loaded)
        {
            glfwSetWindowSize(window_, proj_.width, proj_.height);
            glfwSetWindowTitle(window_, proj_.windowTitle.c_str());
            handleResize(proj_.width, proj_.height);
            std::cout << "[Engine] project '" << proj_.projectName << "' / '"
                      << proj_.windowTitle << "' " << proj_.width << "x" << proj_.height
                      << " mode=" << FProjectDescriptor::RenderModeName(proj_.renderMode)
                      << " startup=" << (proj_.startupWorld.empty() ? "(none)" : proj_.startupWorld)
                      << "\n";
        }
        else std::cout << "[Engine] project load failed -> defaults\n";
    }

    OnStartup();
    handleResize(width_, height_);          // initial viewport / ortho

    world_ = WorldSetting();                 // project builds the world (or null)

    // Data-driven startup world: if the app didn't build one in code and the
    // settings name a StartupWorld, load Content/<StartupWorld>.world.
    if (!world_ && !proj_.startupWorld.empty())
    {
        namespace fs = std::filesystem;
        const std::string wp =
            (fs::path(projDir) / "Content" / (proj_.startupWorld + ".world")).string();
        world_ = FWorldSerializer::LoadFromFile(wp.c_str());
        std::cout << "[Engine] startup world '" << wp << "' "
                  << (world_ ? "loaded" : "not found") << "\n";
    }
    subsystems_.InitAll();
    if (world_) world_->BeginPlay();

    lastTime_ = glfwGetTime();
    while (!glfwWindowShouldClose(window_))
    {
        const double now = glfwGetTime();
        float dt = static_cast<float>(now - lastTime_);
        lastTime_ = now;

        glClear(GL_COLOR_BUFFER_BIT);
        subsystems_.TickAll(dt);
        Tick(dt);                            // world sim + draw
        glfwSwapBuffers(window_);
        glfwPollEvents();

        if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS ||
            glfwGetKey(window_, GLFW_KEY_Q)      == GLFW_PRESS)
            glfwSetWindowShouldClose(window_, GL_TRUE);
    }

    if (world_) world_->EndPlay();
    subsystems_.ShutdownAll();
    delete world_;
    world_ = nullptr;
    return 0;
}
