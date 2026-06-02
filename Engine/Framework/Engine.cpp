#include "Engine.h"

#include <iostream>
#include <GL/glew.h>

#define GLFW_INCLUDE_GLU
#define GLFW_DLL
#include <GLFW/glfw3.h>

Engine::~Engine()
{
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

int Engine::Run()
{
    OnStartup();
    handleResize(width_, height_);             // initial viewport / ortho

    while (!glfwWindowShouldClose(window_))
    {
        glClear(GL_COLOR_BUFFER_BIT);
        Render();                              // app draws the frame
        glfwSwapBuffers(window_);
        glfwPollEvents();

        if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS ||
            glfwGetKey(window_, GLFW_KEY_Q)      == GLFW_PRESS)
            glfwSetWindowShouldClose(window_, GL_TRUE);
    }
    return 0;
}
