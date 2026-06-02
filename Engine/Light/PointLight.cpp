#include "PointLight.h"
#include <glm/glm.hpp>

PointLight::PointLight()
    : LightComponent(glm::vec3(1.0f), glm::vec3(1.0f))
    , LightPos(0.0f)
{
}

PointLight::PointLight(glm::vec3 pos, glm::vec3 color, glm::vec3 intensity)
    : LightComponent(color, intensity)
    , LightPos(pos)
{
}

LightGLSLInfo PointLight::getGLSLInfo() const
{
    LightGLSLInfo info;

    info.constants =
        "\nconst float SOFT_OX[8] = float[8]( 1.0, -1.0,  0.0,  0.0,  0.707, -0.707, -0.707,  0.707);\n"
        "const float SOFT_OY[8] = float[8]( 0.0,  0.0,  1.0, -1.0,  0.707,  0.707, -0.707, -0.707);\n";

    info.uniforms =
        "\n#define MAX_POINT_LIGHTS 4\n"
        "uniform int  uNPointLight;\n"
        "uniform vec3 uPLPos   [MAX_POINT_LIGHTS];\n"
        "uniform vec3 uPLEffect[MAX_POINT_LIGHTS];\n";

    info.functions =
        "\nvec3 shadePointLight(vec3 p, vec3 n, int type, int idx, int pi)\n"
        "{\n"
        "    vec3  toLight = uPLPos[pi] - p;\n"
        "    float dist    = length(toLight);\n"
        "    vec3  l       = toLight / dist;\n"
        "    float NdotL   = dot(n, l);\n"
        "    if (NdotL <= 0.0) return vec3(0.0);\n"
        "    vec3 orig = p + 1e-4 * n;\n"
        "    if (occluded(orig, l, dist)) return vec3(0.0);\n"
        "    vec3 up  = (abs(l.x) > 0.9) ? vec3(0,1,0) : vec3(1,0,0);\n"
        "    vec3 tng = normalize(cross(l, up));\n"
        "    vec3 btn = cross(l, tng);\n"
        "    float lit = 1.0;\n"
        "    for (int s = 0; s < SOFT_SHADOW_SAMPLES - 1; ++s) {\n"
        "        vec3  sp = uPLPos[pi] + 0.5 * (SOFT_OX[s]*tng + SOFT_OY[s]*btn);\n"
        "        vec3  ts = sp - p;\n"
        "        float td = length(ts);\n"
        "        if (!occluded(orig, ts / td, td)) lit += 1.0;\n"
        "    }\n"
        "    lit /= float(SOFT_SHADOW_SAMPLES);\n"
        "    vec3 eff = uPLEffect[pi];\n"
        "    vec3 col = getDiffuse(type, idx, p) * eff * NdotL * lit;\n"
        "    float shiny = getShiny(type, idx);\n"
        "    if (shiny > 0.0) {\n"
        "        vec3  v     = normalize(uEye - p);\n"
        "        vec3  h     = normalize(l + v);\n"
        "        float NdotH = max(0.0, dot(n, h));\n"
        "        col += getKs(type, idx) * eff * pow(NdotH, shiny) * lit;\n"
        "    }\n"
        "    return col;\n"
        "}\n";

    // Fix: use SOFT_SHADOW_RADIUS #define (passed from preamble) instead of hardcoded 0.5
    info.functions =
        "\nvec3 shadePointLight(vec3 p, vec3 n, int type, int idx, int pi)\n"
        "{\n"
        "    vec3  toLight = uPLPos[pi] - p;\n"
        "    float dist    = length(toLight);\n"
        "    vec3  l       = toLight / dist;\n"
        "    float NdotL   = dot(n, l);\n"
        "    if (NdotL <= 0.0) return vec3(0.0);\n"
        "    vec3 orig = p + 1e-4 * n;\n"
        "    if (occluded(orig, l, dist)) return vec3(0.0);\n"
        "    vec3 up  = (abs(l.x) > 0.9) ? vec3(0,1,0) : vec3(1,0,0);\n"
        "    vec3 tng = normalize(cross(l, up));\n"
        "    vec3 btn = cross(l, tng);\n"
        "    float lit = 1.0;\n"
        "    for (int s = 0; s < SOFT_SHADOW_SAMPLES - 1; ++s) {\n"
        "        vec3  sp = uPLPos[pi] + SOFT_SHADOW_RADIUS * (SOFT_OX[s]*tng + SOFT_OY[s]*btn);\n"
        "        vec3  ts = sp - p;\n"
        "        float td = length(ts);\n"
        "        if (!occluded(orig, ts / td, td)) lit += 1.0;\n"
        "    }\n"
        "    lit /= float(SOFT_SHADOW_SAMPLES);\n"
        "    vec3 eff = uPLEffect[pi];\n"
        "    vec3 col = getDiffuse(type, idx, p) * eff * NdotL * lit;\n"
        "    float shiny = getShiny(type, idx);\n"
        "    if (shiny > 0.0) {\n"
        "        vec3  v     = normalize(uEye - p);\n"
        "        vec3  h     = normalize(l + v);\n"
        "        float NdotH = max(0.0, dot(n, h));\n"
        "        col += getKs(type, idx) * eff * pow(NdotH, shiny) * lit;\n"
        "    }\n"
        "    return col;\n"
        "}\n";

    info.directContrib =
        "    for (int i = 0; i < uNPointLight; ++i)\n"
        "        col += shadePointLight(p, n, type, idx, i);\n";

    // indirectContrib intentionally empty — PointLight is direct-only

    return info;
}
