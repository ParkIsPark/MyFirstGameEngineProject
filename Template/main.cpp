#include <Windows.h>
#include <iostream>
#include <GL/glew.h>
#include <GL/freeglut.h>

#define GLFW_INCLUDE_GLU
#define GLFW_DLL
#include <GLFW/glfw3.h>
#include <vector>

#define GLM_SWIZZLE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>

using namespace glm;

// Engine
#include "UScene.h"
#include "ACamera.h"
#include "URayTracing.h"

// -------------------------------------------------
// Global Variables
// -------------------------------------------------
int Width  = 1280;
int Height = 720;
std::vector<float> OutputImage;

UScene      scene;
ACamera     camera;
URayTracing rayTracer;
// -------------------------------------------------


void render()
{
    // Fill OutputImage with per-pixel RGB floats in [0,1].
    // Row-major, origin at bottom-left (matches glDrawPixels).
    //
    // Example (CPU ray tracing):
    //   rayTracer.Render(scene, camera, /*mode=*/0);
    //   OutputImage = scene.outputImage;
    //
    // Stub: grey background
    OutputImage.assign(Width * Height * 3, 0.5f);
}


void resize_callback(GLFWwindow*, int nw, int nh)
{
    Width  = nw;
    Height = nh;
    glViewport(0, 0, nw, nh);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(Width),
            0.0, static_cast<double>(Height),
            1.0, -1.0);

    OutputImage.reserve(Width * Height * 3);
    render();
}


int main(int argc, char* argv[])
{
    GLFWwindow* window;

    if (!glfwInit())
        return -1;

    window = glfwCreateWindow(Width, Height, "OpenGL Viewer", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    if (glewInit() != GLEW_OK)
    {
        std::cerr << "GLEW init failed\n";
        return -1;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    glfwSetFramebufferSizeCallback(window, resize_callback);
    resize_callback(NULL, Width, Height);

    while (!glfwWindowShouldClose(window))
    {
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawPixels(Width, Height, GL_RGB, GL_FLOAT, OutputImage.data());
        glfwSwapBuffers(window);
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_Q)      == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GL_TRUE);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
