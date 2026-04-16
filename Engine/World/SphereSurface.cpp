#include "SphereSurface.h"
#include "AActor.h"
#include <cmath>
#include <string>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <GL/glew.h>

SphereSurface::SphereSurface(float r)
    : radius(r)
{
}

glm::vec3 SphereSurface::getNormal(const glm::vec3& hitPoint) const
{
    return glm::normalize(hitPoint - owner->position);
}

bool SphereSurface::intersect(const URay& ray, float& tHit) const
{
    glm::vec3 oc = ray.origin - owner->position;
    float a    = glm::dot(ray.direction, ray.direction);
    float b    = 2.0f * glm::dot(ray.direction, oc);
    float c    = glm::dot(oc, oc) - radius * radius;
    float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return false;

    float t = (-b - std::sqrt(disc)) / (2.0f * a);
    if (t < 1e-4f) return false;

    tHit = t;
    return true;
}

void SphereSurface::SetColor(const glm::vec3& TargetColor)
{
    material.kd = TargetColor;
}

// Spherical UV (equirectangular): u = longitude [0,1], v = latitude [0,1].
//   u = (atan2(z, x) + pi) / (2*pi)
//   v = 0.5 - asin(y) / pi
glm::vec2 SphereSurface::getUV(const glm::vec3& hitPoint) const
{
    glm::vec3 n = glm::normalize(hitPoint - owner->position);
    float u = (std::atan2(n.z, n.x) + glm::pi<float>()) / (2.0f * glm::pi<float>());
    float v = 0.5f - std::asin(glm::clamp(n.y, -1.0f, 1.0f)) / glm::pi<float>();
    return glm::vec2(u, v);
}

void SphereSurface::uploadUniform(unsigned int prog, int idx) const
{
    GLuint p = static_cast<GLuint>(prog);
    std::string s = std::to_string(idx);
    bool hasTex = (material.texture != 0);

    glUniform3fv(glGetUniformLocation(p, ("uSphPos["   + s + "]").c_str()), 1, glm::value_ptr(owner->position));
    glUniform1f (glGetUniformLocation(p, ("uSphR["     + s + "]").c_str()), radius);
    glUniform3fv(glGetUniformLocation(p, ("uSphKa["    + s + "]").c_str()), 1, glm::value_ptr(material.ka));
    glUniform3fv(glGetUniformLocation(p, ("uSphKd["    + s + "]").c_str()), 1, glm::value_ptr(material.kd));
    glUniform3fv(glGetUniformLocation(p, ("uSphKs["    + s + "]").c_str()), 1, glm::value_ptr(material.ks));
    glUniform1f (glGetUniformLocation(p, ("uSphShiny[" + s + "]").c_str()), material.shininess);
    glUniform3fv(glGetUniformLocation(p, ("uSphKm["    + s + "]").c_str()), 1, glm::value_ptr(material.km));
    glUniform3fv(glGetUniformLocation(p, ("uSphEmit["  + s + "]").c_str()), 1, glm::value_ptr(material.emissive));
    glUniform1i (glGetUniformLocation(p, ("uSphTex["   + s + "]").c_str()), hasTex ? 0 : -1);
}

SurfaceGLSLInfo SphereSurface::getGLSLInfo() const
{
    SurfaceGLSLInfo info;
    info.typeId       = 0;
    info.hitFuncName  = "sphHit";
    info.countUniform = "uNSph";

    info.uniforms =
        "\n#define MAX_SPHERES 8\n"
        "uniform int   uNSph;\n"
        "uniform vec3  uSphPos   [MAX_SPHERES];\n"
        "uniform float uSphR     [MAX_SPHERES];\n"
        "uniform vec3  uSphKa    [MAX_SPHERES];\n"
        "uniform vec3  uSphKd    [MAX_SPHERES];\n"
        "uniform vec3  uSphKs    [MAX_SPHERES];\n"
        "uniform float uSphShiny [MAX_SPHERES];\n"
        "uniform vec3  uSphKm    [MAX_SPHERES];\n"
        "uniform vec3  uSphEmit  [MAX_SPHERES];\n"
        "uniform int   uSphTex   [MAX_SPHERES];\n";

    info.hitFunc =
        "\nfloat sphHit(vec3 ro, vec3 rd, int i)\n"
        "{\n"
        "    vec3  oc   = ro - uSphPos[i];\n"
        "    float b    = dot(rd, oc);\n"
        "    float c    = dot(oc, oc) - uSphR[i] * uSphR[i];\n"
        "    float disc = b*b - c;\n"
        "    if (disc < 0.0) return -1.0;\n"
        "    float t = -b - sqrt(disc);\n"
        "    return (t > 1e-4) ? t : -1.0;\n"
        "}\n";

    info.normalCase = "    if (type == 0) return normalize(p - uSphPos[idx]);\n";

    info.diffuseCase =
        "    if (type == 0) {\n"
        "        if (uSphTex[idx] == 0) {\n"
        "            vec3  n = normalize(p - uSphPos[idx]);\n"
        "            float u = (atan(n.z, n.x) + 3.14159265) / 6.28318530;\n"
        "            float v = 0.5 - asin(clamp(n.y, -1.0, 1.0)) / 3.14159265;\n"
        "            return texture(uBrickTex, vec2(u, v)).rgb;\n"
        "        }\n"
        "        return uSphKd[idx];\n"
        "    }\n";

    info.kaCase    = "    if (t == 0) return uSphKa[i];\n";
    info.ksCase    = "    if (t == 0) return uSphKs[i];\n";
    info.shinyCase = "    if (t == 0) return uSphShiny[i];\n";
    info.kmCase    = "    if (t == 0) return uSphKm[i];\n";
    info.emitCase  = "    if (t == 0) return uSphEmit[i];\n";

    return info;
}
