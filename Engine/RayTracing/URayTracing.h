#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <vector>

class UScene;
class ACamera;
class AActor;
class ALight;
struct URay;
class IntersectionPass;
class LightingPass;

// ------------------------------------------------------------------
// Post-processing / sampling settings for CPU Render().
// Stored here so callers only need to #include "URayTracing.h".
// ------------------------------------------------------------------
struct RenderSettings {
    // Gamma correction:  output = pow(linear, 1/gamma)
    bool  enableGamma = false;
    float gamma       = 2.2f;   // typical sRGB gamma

    // Supersampling AA: cast aaSamples random rays per pixel, box-filter average
    bool enableAA   = false;
    int  aaSamples  = 64;       
};

class URayTracing
{
public:
    URayTracing();
    ~URayTracing();

    // ------------------------------------------------------------------
    // GPU renderer lifecycle
    //   Init()        — assemble shader from passes, compile, build quad
    //   RenderFrame() — upload uniforms via passes, draw
    //   Cleanup()     — delete GL objects
    // ------------------------------------------------------------------
    void Init(const UScene& scene);
    void RenderFrame(const UScene& scene, const ACamera& cam);
    void Cleanup();

    // ------------------------------------------------------------------
    // CPU ray tracer
    //   mode 0 = Phong + shadow  (Blinn-Phong, scene.Lights)
    //   mode 1 = Phong + shadow + mirror reflection (recursive km)
    //
    // Post-processing applied inside Render() after all pixels are traced:
    //   settings.enableAA    → cast N jittered rays per pixel, average (box filter)
    //   settings.enableGamma → apply pow(c, 1/gamma) to the linear buffer
    // ------------------------------------------------------------------
    void Render(UScene& scene, ACamera& camera, int mode,
                const RenderSettings& settings = RenderSettings{}) const;

    // Public: used by EnvironmentLight for GI bounce
    glm::vec3 TraceQ3(const URay& ray, const UScene& scene,
        const ACamera& camera, int depth = 0) const;

private:
    // ------------------------------------------------------------------
    // GPU state  (unsigned int == GLuint)
    // ------------------------------------------------------------------
    unsigned int prog_ = 0;
    unsigned int vao_  = 0;
    unsigned int vbo_  = 0;

    std::unique_ptr<IntersectionPass> intersectionPass_;
    std::unique_ptr<LightingPass>     lightingPass_;

    void uploadCameraUniforms(const ACamera& cam, const UScene& scene) const;

    // ------------------------------------------------------------------
    // CPU ray tracing internals
    // ------------------------------------------------------------------
    bool      FindClosestHit(const URay& ray, const UScene& scene,
                             float& outT, const AActor*& outActor) const;

    // Blinn-Phong shading from scene.Lights (no reflection)
    glm::vec3 TraceQ2(const URay& ray, const UScene& scene,
                      const ACamera& camera) const;

    // Per-point Phong+shadow contribution from all scene lights (used by Q3)
    glm::vec3 ComputePhong(const glm::vec3& p, const glm::vec3& n,
                           const AActor* actor, const URay& ray,
                           const UScene& scene, const ACamera& camera,
                           int depth) const;

    // AA: casts N jittered rays through pixel (ix,iy) and returns their average
    glm::vec3 TracePixelAA(int ix, int iy, int mode,
                            const UScene& scene, const ACamera& cam, int N) const;

    // Gamma post-process: applies pow(c, 1/gamma) to every value in outputImage
    void ApplyGamma(UScene& scene, float gamma) const;
};
