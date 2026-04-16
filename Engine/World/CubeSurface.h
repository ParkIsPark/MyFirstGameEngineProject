#pragma once
#include "USurface.h"

// Axis-aligned box (AABB) defined by center and per-axis half-extents.
// CubeSurface(float)     -> uniform cube (halfVec = vec3(half))
// CubeSurface(vec3)      -> non-uniform box (wall, slab, etc.)
class CubeSurface : public USurface
{
public:
    glm::vec3 halfVec; // per-axis half-extents

    explicit CubeSurface(float half);
    explicit CubeSurface(glm::vec3 halfExtents);

    bool             intersect(const URay& ray, float& tHit) const override;
    glm::vec3        getNormal(const glm::vec3& hitPoint) const override;
    void             SetColor(const glm::vec3& TargetColor) override;
    glm::vec2        getUV(const glm::vec3& hitPoint) const override;
    SurfaceGLSLInfo  getGLSLInfo() const override;
    void             uploadUniform(unsigned int prog, int idx) const override;

    // Packs this cube's data into 24 floats (6 vec4s, std140 layout) for UBO upload.
    void getUBOData(float* dst) const;
};
