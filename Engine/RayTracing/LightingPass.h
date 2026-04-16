#pragma once
#include "URayTracingPass.h"

class UScene;
class ACamera;

// ---------------------------------------------------------------------------
// LightingPass
//
// Responsibility: everything that computes lighting on the GPU.
//   - GLSL: collects constants / uniforms / functions from each unique
//           LightComponent type in the scene, assembles skyColor() and the
//           shade() dispatcher automatically.
//   - Uniforms: uploads all light data (point light positions/effects,
//               environment effect).
// ---------------------------------------------------------------------------
class LightingPass : public URayTracingPass
{
public:
    explicit LightingPass(const UScene& scene);

    std::string getGLSL() const override;

    void uploadUniforms(unsigned int prog,
                        const UScene& scene,
                        const ACamera& cam) const override;

private:
    const UScene& scene_;
};
