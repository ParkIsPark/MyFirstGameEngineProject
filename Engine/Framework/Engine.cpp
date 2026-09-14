#include "Engine.h"
#include "UWorld.h"
#include "FWorldSerializer.h"
#include "../Script/UScriptSubsystem.h"
#include "FRenderTarget.h"

#include <iostream>
#include <filesystem>
#include <cstdint>
#include <GL/glew.h>

#define GLFW_INCLUDE_GLU
#define GLFW_DLL
#include <GLFW/glfw3.h>

namespace
{
std::uint64_t NextContextGeneration()
{
    static std::uint64_t generation = 0;
    ++generation;
    if (generation == 0) ++generation;
    return generation;
}

GLFWwindow* CreateCompatibilityWindow(int major,
                                      int minor,
                                      int width,
                                      int height,
                                      const char* title)
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    return glfwCreateWindow(width, height, title, nullptr, nullptr);
}

class FCurrentOpenGLCapabilitySource final : public IGraphicsCapabilitySource
{
public:
    bool QueryIntegerVersion(int& major, int& minor) const override
    {
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        return glGetError() == GL_NO_ERROR && major > 0;
    }

    const char* QueryVersionString() const override
    {
        return reinterpret_cast<const char*>(glGetString(GL_VERSION));
    }

    bool HasExtension(const char* extensionName) const override
    {
        return glewIsSupported(extensionName) == GL_TRUE;
    }

    bool HasRequiredComputeEntryPoints() const override
    {
        static const char* requiredEntryPoints[] = {
            "glDispatchCompute",
            "glMemoryBarrier",
            "glBindBufferBase",
            "glShaderStorageBlockBinding",
            "glBindImageTexture",
        };
        for (const char* entryPoint : requiredEntryPoints)
        {
            if (!glfwGetProcAddress(entryPoint)) return false;
        }
        return true;
    }
};
} // namespace

Engine::Engine(Role role) : role_(role)
{
    subsystems_.Register(new UScriptSubsystem());
}

Engine::~Engine()
{
    delete world_;
    cleanupGraphics();
}

bool Engine::Init(int width, int height, const char* title)
{
    width_  = width;
    height_ = height;

    cleanupGraphics();
    backendWarnings_.Reset();

    if (!glfwInit())
    {
        std::cerr << "[Engine] GLFW initialization failed\n";
        return false;
    }
    glfwInitialized_ = true;

    // Prefer a 4.3 compatibility context for the optional Compute backend,
    // then recreate at the supported 3.3 minimum. Compatibility is temporary
    // while the deprecated fixed-function renderers remain available.
    window_ = CreateCompatibilityWindow(4, 3, width_, height_, title);
    if (!window_)
        window_ = CreateCompatibilityWindow(3, 3, width_, height_, title);
    if (!window_)
    {
        std::cerr << "[Engine] Unable to create an OpenGL 4.3 or 3.3 compatibility context; "
                     "this engine requires OpenGL 3.3 or newer.\n";
        cleanupGraphics();
        return false;
    }
    glfwMakeContextCurrent(window_);

    glewExperimental = GL_TRUE;
    const GLenum glewStatus = glewInit();
    glGetError(); // GLEW may leave one benign GL_INVALID_ENUM on core-capable drivers.
    if (glewStatus != GLEW_OK)
    {
        std::cerr << "[Engine] GLEW initialization failed: "
                  << reinterpret_cast<const char*>(glewGetErrorString(glewStatus)) << "\n";
        cleanupGraphics();
        return false;
    }

    const FCurrentOpenGLCapabilitySource capabilitySource;
    graphicsCapabilities_ = ProbeGraphicsCapabilities(capabilitySource);
    if (!graphicsCapabilities_.MeetsOpenGL33())
    {
        std::cerr << "[Engine] Detected OpenGL " << graphicsCapabilities_.major << "."
                  << graphicsCapabilities_.minor
                  << "; this engine requires OpenGL 3.3 or newer.\n";
        cleanupGraphics();
        return false;
    }
    contextGeneration_ = NextContextGeneration();
    SetActiveRenderTargetContextGeneration(contextGeneration_);
    backendSelection_ = SelectRayTracingBackend(
        proj_.defaultRenderFeatures.rayTracingBackend, graphicsCapabilities_);
    std::cout << "[Engine] OpenGL " << graphicsCapabilities_.major << "."
              << graphicsCapabilities_.minor << " initialized\n";

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    // member resize callback via user-pointer trampoline (no global state)
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, &Engine::resizeTrampoline);
    return true;
}

void Engine::cleanupGraphics()
{
    SetActiveRenderTargetContextGeneration(0);
    if (window_)
    {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    if (glfwInitialized_)
    {
        glfwTerminate();
        glfwInitialized_ = false;
    }
    graphicsCapabilities_ = FGraphicsCapabilities{};
    backendSelection_ = FBackendSelection{};
    contextGeneration_ = 0;
}

void Engine::resolveRayTracingBackend()
{
    ResolveRayTracingBackend(proj_.defaultRenderFeatures.rayTracingBackend);
}

const FBackendSelection& Engine::ResolveRayTracingBackend(ERayTracingBackend requested)
{
    const FBackendSelection next = SelectRayTracingBackend(requested, graphicsCapabilities_);
    const bool changed = next.requested != backendSelection_.requested ||
        next.selected != backendSelection_.selected ||
        next.available != backendSelection_.available ||
        next.rayTracingEnabled != backendSelection_.rayTracingEnabled ||
        next.fallbackReason != backendSelection_.fallbackReason;
    backendSelection_ = next;

    if (changed)
    {
        std::cout << "[Engine] ray backend requested="
                  << FProjectDescriptor::RayTracingBackendName(backendSelection_.requested)
                  << " selected="
                  << FProjectDescriptor::RayTracingBackendName(backendSelection_.selected)
                  << " available=" << (backendSelection_.available ? "yes" : "no") << "\n";
    }

    if (backendWarnings_.ShouldEmit(backendSelection_))
    {
        std::cerr << "[Engine] warning: " << backendSelection_.fallbackReason << "\n";
    }
    return backendSelection_;
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
    OnResize(w, h);
}

void Engine::Tick(float dt)
{
    if (role_ == Role::Game && world_) world_->Tick(dt);
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
                      << " hardwareRaster=on"
                      << " rayTracing=" << (proj_.defaultRenderFeatures.rayTracing ? "on" : "off")
                      << " shadows=" << (proj_.defaultRenderFeatures.rayTracedShadows ? "on" : "off")
                      << " gi=" << (proj_.defaultRenderFeatures.rayTracedGI ? "on" : "off")
                      << " reflections=" << (proj_.defaultRenderFeatures.rayTracedReflections ? "on" : "off")
                      << " backend=" << FProjectDescriptor::RayTracingBackendName(proj_.defaultRenderFeatures.rayTracingBackend)
                      << " startup=" << (proj_.startupWorld.empty() ? "(none)" : proj_.startupWorld)
                      << "\n";
        }
        else std::cout << "[Engine] project load failed -> defaults\n";
    }

    resolveRayTracingBackend();

    OnStartup();
    handleResize(width_, height_);          // initial viewport / ortho

    BootWorld(projPath ? std::filesystem::absolute(projPath).parent_path().string()
                      : std::filesystem::current_path().string());

    lastTime_ = glfwGetTime();
    while (!glfwWindowShouldClose(window_))
    {
        const double now = glfwGetTime();
        float dt = static_cast<float>(now - lastTime_);
        lastTime_ = now;

        glClear(GL_COLOR_BUFFER_BIT);
        subsystems_.TickAll(dt);
        Tick(dt);
        glfwSwapBuffers(window_);
        glfwPollEvents();

        if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS ||
            glfwGetKey(window_, GLFW_KEY_Q)      == GLFW_PRESS)
            glfwSetWindowShouldClose(window_, GL_TRUE);
    }

    if (world_) world_->EndPlay();
    OnShutdown();
    subsystems_.ShutdownAll();
    delete world_;
    world_ = nullptr;
    return 0;
}

void Engine::BootWorld(const std::string& projectRoot)
{
    if (role_ == Role::Game) world_ = WorldSetting();

    // Data-driven startup world: if the app didn't build one in code and the
    // settings name a StartupWorld, load Content/<StartupWorld>.world.
    if (role_ == Role::Game && !world_ && !proj_.startupWorld.empty())
    {
        namespace fs = std::filesystem;
        const std::string wp =
            (fs::path(projectRoot) / "Content" / (proj_.startupWorld + ".world")).string();
        world_ = FWorldSerializer::LoadFromFile(wp.c_str(), proj_.defaultRenderFeatures);
        std::cout << "[Engine] startup world '" << wp << "' "
                  << (world_ ? "loaded" : "not found") << "\n";
    }
    auto* scripts = subsystems_.Get<UScriptSubsystem>();
    scripts->SetProjectRoot(projectRoot);
    subsystems_.InitAll();
    if (role_ == Role::Game && world_) { world_->SetScriptSubsystem(scripts); world_->BeginPlay(); }

}
