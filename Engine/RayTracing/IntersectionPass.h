#pragma once
#include "URayTracingPass.h"

class UScene;
class ACamera;

// ---------------------------------------------------------------------------
// IntersectionPass
//
// Responsibility: everything that touches geometry on the GPU.
//   - GLSL: collects intersectGLSL / normalCase / diffuseCase / propertyCases
//           from each unique Surface type in the scene, then assembles the
//           findClosest(), occluded(), getNormal(), getDiffuse(), and property
//           getter functions automatically.
//   - Uniforms: uploads all geometry arrays (spheres, cubes, planes) and
//               binds the brick texture.
//
// The GLSL code is assembled once in getGLSL() (called during Init).
// uploadUniforms() is called every frame to keep GPU data in sync.
// ---------------------------------------------------------------------------
class IntersectionPass : public URayTracingPass
{
public:
    explicit IntersectionPass(const UScene& scene);
    ~IntersectionPass() override;

    std::string getGLSL() const override;

    void uploadUniforms(unsigned int prog,
                        const UScene& scene,
                        const ACamera& cam) const override;

private:
    const UScene& scene_;
    unsigned int  brickTex_ = 0;  // found during construction
    unsigned int  cubeUBO_  = 0;  // GL_UNIFORM_BUFFER for cube data (std140)
};
