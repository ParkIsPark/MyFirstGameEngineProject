#pragma once
#include "URayTracingPass.h"

class UScene;
class ACamera;

// ---------------------------------------------------------------------------
// IndirectLightPass
//
// Responsibility: indirect (environment / GI) lighting on the GPU.
//   GLSL  : per-light constants/uniforms/functions collected from lights
//            whose indirectContrib is non-empty (EnvironmentLight only);
//            shadeIndirect(p, n, type, idx) — calls shadeDirect() at bounce;
//            shade(p, n, type, idx) = shadeDirect + shadeIndirect combiner.
//   Upload: EnvironmentLight effect uniform.
//
// NOTE: shadeDirect() must be declared before this pass (DirectLightPass first).
// ---------------------------------------------------------------------------
class IndirectLightPass : public URayTracingPass
{
public:
    explicit IndirectLightPass(const UScene& scene);

    std::string getGLSL() const override;

    void uploadUniforms(unsigned int prog,
                        const UScene& scene,
                        const ACamera& cam) const override;

private:
    const UScene& scene_;
};
