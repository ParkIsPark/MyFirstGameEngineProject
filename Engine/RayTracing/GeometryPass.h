#pragma once
#include "URayTracingPass.h"

class UScene;
class ACamera;

// ---------------------------------------------------------------------------
// GeometryPass
//
// Responsibility: pure geometry on the GPU.
//   GLSL  : surface-type uniforms, per-type hit functions,
//            findClosest(), occluded(), getNormal()
//   Upload: geometry arrays (spheres, planes) + cube UBO + brick texture
// ---------------------------------------------------------------------------
class GeometryPass : public URayTracingPass
{
public:
    explicit GeometryPass(const UScene& scene);
    ~GeometryPass() override;

    std::string getGLSL() const override;

    void uploadUniforms(unsigned int prog,
                        const UScene& scene,
                        const ACamera& cam) const override;

private:
    const UScene& scene_;
    unsigned int  brickTex_ = 0;
    unsigned int  cubeUBO_  = 0;
};
