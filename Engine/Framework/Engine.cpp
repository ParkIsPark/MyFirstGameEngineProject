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

void Engine::resizeTrampoline(GLFWwindow* win, int w, int h)
{
    static_cast<Engine*>(glfwGetWindowUserPointer(win))->handleResize(w, h);
}

void Engine::handleResize(int w, int h)
{
    width_  = w;
    height_ = h;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(w), 0.0, static_cast<double>(h), 1.0, -1.0);
    OnResize(w, h);
    Render();                              // re-render at the new size
}

int Engine::Run()
{
    OnStartup();
    handleResize(width_, height_);         // initial viewport + first render

    while (!glfwWindowShouldClose(window_))
    {
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawPixels(width_, height_, GL_RGB, GL_FLOAT, outputImage_.data());
        glfwSwapBuffers(window_);
        glfwPollEvents();

        if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS ||
            glfwGetKey(window_, GLFW_KEY_Q)      == GLFW_PRESS)
            glfwSetWindowShouldClose(window_, GL_TRUE);
    }
    return 0;
}
