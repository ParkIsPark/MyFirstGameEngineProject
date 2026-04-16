#pragma once
#include "URayTracingPass.h"

class UScene;
class ACamera;

// ---------------------------------------------------------------------------
// DirectLightPass
//
// Responsibility: direct (point) lighting on the GPU.
//   GLSL  : per-light constants/uniforms/functions collected from lights
//            whose directContrib is non-empty (PointLight only);
//            skyColor() background function;
//            shadeDirect(p, n, type, idx) dispatcher.
//   Upload: PointLight positions and effects.
// ---------------------------------------------------------------------------
class DirectLightPass : public URayTracingPass
{
public:
    explicit DirectLightPass(const UScene& scene);

    std::string getGLSL() const override;

    void uploadUniforms(unsigned int prog,
                        const UScene& scene,
                        const ACamera& cam) const override;

private:
    const UScene& scene_;
};
