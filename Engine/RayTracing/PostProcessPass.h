#pragma once
#include "URayTracingPass.h"

class UScene;
class ACamera;

// ---------------------------------------------------------------------------
// PostProcessPass
//
// Responsibility: trace loop + screen entry point.
//   GLSL  : _hash / _halton helpers
//            trace() — reflection loop with ambient + shade() calls
//            main()  — AA sampling (mode from RenderConfig.h) + Reinhard
//                      tone mapping + gamma correction
//
// All AA and gamma parameters are compile-time constants from RenderConfig.h
// injected into the shader preamble by URayTracing::Init().
// No per-frame uniforms are uploaded by this pass.
// ---------------------------------------------------------------------------
class PostProcessPass : public URayTracingPass
{
public:
    PostProcessPass() = default;

    std::string getGLSL() const override;

    // No per-frame state to upload — intentional no-op override.
    void uploadUniforms(unsigned int /*prog*/,
                        const UScene& /*scene*/,
                        const ACamera& /*cam*/) const override {}
};
