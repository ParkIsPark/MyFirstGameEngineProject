#pragma once
#include <string>

class UScene;
class ACamera;

// ---------------------------------------------------------------------------
// Base interface for all render passes.
// Each pass contributes a GLSL code block and an optional uniform upload.
// ---------------------------------------------------------------------------
class URayTracingPass
{
public:
    virtual ~URayTracingPass() = default;

    // Returns the GLSL function/uniform block this pass contributes.
    virtual std::string getGLSL() const = 0;

    // Uploads any uniforms this pass owns, called every frame.
    // Default is a no-op; override when the pass has per-frame data.
    virtual void uploadUniforms(unsigned int prog,
                                const UScene& scene,
                                const ACamera& cam) const {}
};
