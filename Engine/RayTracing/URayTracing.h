#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "RenderConfig.h"   // AA_ENABLE, AA_GRID, AA_MODE, GAMMA_ENABLE, GAMMA_VALUE

class UScene;
class ACamera;
class AActor;
class ALight;
struct URay;
class GeometryPass;
class MaterialPass;
class DirectLightPass;
class IndirectLightPass;
class PostProcessPass;

// ------------------------------------------------------------------
// RenderSettings — CPU Render() configuration.
// Defaults come from RenderConfig.h so CPU and GPU stay in sync.
// Override individual fields to deviate (e.g. HW4 key-press toggles).
// ------------------------------------------------------------------
struct RenderSettings {
    // Gamma correction:  output = Reinhard → pow(c, 1/gamma)
    bool  enableGamma = GAMMA_ENABLE != 0;
    float gamma       = GAMMA_VALUE;

    // Anti-aliasing
    bool enableAA = AA_ENABLE != 0;
    int  aaGrid   = AA_GRID;    // grid dim: total samples = aaGrid*aaGrid
    int  aaMode   = AA_MODE;    // 1=random  2=stratified  3=halton
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
    void RenderFrame(const UScene& scene, const ACamera& cam,
                     const RenderSettings& s = RenderSettings{});
    void Cleanup();

    // ------------------------------------------------------------------
    // CPU ray tracer
    //   mode 0 = Phong + shadow  (Blinn-Phong, scene.Lights)
    //   mode 1 = Phong + shadow + mirror reflection (recursive km)
    //
    // AA and gamma settings come from RenderSettings (defaults = RenderConfig.h).
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

    std::unique_ptr<GeometryPass>      geometryPass_;
    std::unique_ptr<MaterialPass>      materialPass_;
    std::unique_ptr<DirectLightPass>   directLightPass_;
    std::unique_ptr<IndirectLightPass> indirectLightPass_;
    std::unique_ptr<PostProcessPass>   postProcessPass_;

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

    // AA: casts aaGrid*aaGrid rays through pixel (ix,iy) with the chosen mode
    //   aaMode 1 = random jitter
    //   aaMode 2 = stratified jittered grid
    //   aaMode 3 = Halton low-discrepancy sequence
    glm::vec3 TracePixelAA(int ix, int iy, int renderMode,
                            const UScene& scene, const ACamera& cam,
                            int aaGrid, int aaMode) const;

    // Reinhard tone map + gamma post-process applied in-place to outputImage
    void ApplyToneGamma(UScene& scene, float gamma) const;
};
