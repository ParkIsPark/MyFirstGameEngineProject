#include "PlaneSurface.h"
#include "AActor.h"
#include <cmath>
#include <string>
#include <glm/gtc/type_ptr.hpp>
#include <GL/glew.h>

glm::vec3 PlaneSurface::getNormal(const glm::vec3& /*hitPoint*/) const
{
    return glm::vec3(0.0f, 1.0f, 0.0f);
}

bool PlaneSurface::intersect(const URay& ray, float& tHit) const
{
    float y = owner->position.y;
    if (std::fabs(ray.direction.y) < 1e-6f) return false;

    float t = (y - ray.origin.y) / ray.direction.y;
    if (t < 1e-4f) return false;

    tHit = t;
    return true;
}

void PlaneSurface::SetColor(const glm::vec3& TargetColor)
{
    material.kd = TargetColor;
}

// Horizontal plane UV: use XZ coords relative to plane center, tiled at scale 1.
glm::vec2 PlaneSurface::getUV(const glm::vec3& hitPoint) const
{
    glm::vec3 pos = getPosition();
    float u = hitPoint.x - pos.x;
    float v = hitPoint.z - pos.z;
    u = u - std::floor(u);
    v = v - std::floor(v);
    return glm::vec2(u, v);
}

void PlaneSurface::uploadUniform(unsigned int prog, int idx) const
{
    GLuint p = static_cast<GLuint>(prog);
    std::string s = std::to_string(idx);
    bool hasTex = (material.texture != 0);

    glUniform1f (glGetUniformLocation(p, ("uPlaneY["     + s + "]").c_str()), owner->position.y);
    glUniform3fv(glGetUniformLocation(p, ("uPlanePos["   + s + "]").c_str()), 1, glm::value_ptr(owner->position));
    glUniform3fv(glGetUniformLocation(p, ("uPlaneKa["    + s + "]").c_str()), 1, glm::value_ptr(material.ka));
    glUniform3fv(glGetUniformLocation(p, ("uPlaneKd["    + s + "]").c_str()), 1, glm::value_ptr(material.kd));
    glUniform3fv(glGetUniformLocation(p, ("uPlaneKs["    + s + "]").c_str()), 1, glm::value_ptr(material.ks));
    glUniform1f (glGetUniformLocation(p, ("uPlaneShiny[" + s + "]").c_str()), material.shininess);
    glUniform3fv(glGetUniformLocation(p, ("uPlaneKm["    + s + "]").c_str()), 1, glm::value_ptr(material.km));
    glUniform1i (glGetUniformLocation(p, ("uPlaneTex["   + s + "]").c_str()), hasTex ? 0 : -1);
}

SurfaceGLSLInfo PlaneSurface::getGLSLInfo() const
{
    SurfaceGLSLInfo info;
    info.typeId       = 2;
    info.hitFuncName  = "planeHit";
    info.countUniform = "uNPlane";

    info.uniforms =
        "\n#define MAX_PLANES 2\n"
        "uniform int   uNPlane;\n"
        "uniform float uPlaneY    [MAX_PLANES];\n"
        "uniform vec3  uPlanePos  [MAX_PLANES];\n"
        "uniform vec3  uPlaneKa   [MAX_PLANES];\n"
        "uniform vec3  uPlaneKd   [MAX_PLANES];\n"
        "uniform vec3  uPlaneKs   [MAX_PLANES];\n"
        "uniform float uPlaneShiny[MAX_PLANES];\n"
        "uniform vec3  uPlaneKm   [MAX_PLANES];\n"
        "uniform int   uPlaneTex  [MAX_PLANES];\n";

    info.hitFunc =
        "\nfloat planeHit(vec3 ro, vec3 rd, int i)\n"
        "{\n"
        "    if (abs(rd.y) < 1e-6) return -1.0;\n"
        "    float t = (uPlaneY[i] - ro.y) / rd.y;\n"
        "    return (t > 1e-4) ? t : -1.0;\n"
        "}\n";

    info.normalCase = "    if (type == 2) return vec3(0.0, 1.0, 0.0);\n";

    info.diffuseCase =
        "    if (type == 2) {\n"
        "        if (uPlaneTex[idx] == 0) {\n"
        "            vec2 uv = vec2(fract(p.x - uPlanePos[idx].x), fract(p.z - uPlanePos[idx].z));\n"
        "            return texture(uBrickTex, uv).rgb;\n"
        "        }\n"
        "        return uPlaneKd[idx];\n"
        "    }\n";

    info.kaCase    = "    if (t == 2) return uPlaneKa[i];\n";
    info.ksCase    = "    if (t == 2) return uPlaneKs[i];\n";
    info.shinyCase = "    if (t == 2) return uPlaneShiny[i];\n";
    info.kmCase    = "    if (t == 2) return uPlaneKm[i];\n";
    info.emitCase  = "";

    return info;
}
