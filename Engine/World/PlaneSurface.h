#pragma once
#include "USurface.h"

class PlaneSurface : public USurface
{
public:
    PlaneSurface() = default;

    bool             intersect(const URay& ray, float& tHit) const override;
    glm::vec3        getNormal(const glm::vec3& hitPoint) const override;
    void             SetColor(const glm::vec3& TargetColor) override;
    glm::vec2        getUV(const glm::vec3& hitPoint) const override;
    SurfaceGLSLInfo  getGLSLInfo() const override;
    void             uploadUniform(unsigned int prog, int idx) const override;
};
