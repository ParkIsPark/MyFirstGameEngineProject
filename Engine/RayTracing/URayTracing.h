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

    // CPU ray tracer removed -- URayTracing is now a GPU-only renderer
    // (mesh-first: ray tracing moves to GLSL compute over a triangle SSBO).

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

    // (CPU ray tracing internals removed -- GPU-only RT)
};
