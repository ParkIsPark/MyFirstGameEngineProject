#pragma once
#include "URayTracingPass.h"

class UScene;
class ACamera;

// ---------------------------------------------------------------------------
// MaterialPass
//
// Responsibility: material property getters on the GPU.
//   GLSL  : getDiffuse(), getKa(), getKs(), getKm(), getShiny(), getEmit()
//           These dispatch by (type, idx) to per-type surface data.
//   Upload: none — material data is uploaded by GeometryPass uniforms.
// ---------------------------------------------------------------------------
class MaterialPass : public URayTracingPass
{
public:
    explicit MaterialPass(const UScene& scene);

    std::string getGLSL() const override;
    // No per-frame upload needed — uses base no-op.

private:
    const UScene& scene_;
};
