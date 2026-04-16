#include "PointLight.h"
#include "AActor.h"
#include "UScene.h"
#include "ACamera.h"
#include "URay.h"
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

glm::vec3 PointLight::illuminate(
    const glm::vec3&   hitPoint,
    const glm::vec3&   normal,
    const AActor*      actor,
    const URay&        ray,
    const UScene&      scene,
    const ACamera&     camera,
    int                depth,
    const URayTracing* tracer) const
{
    glm::vec3 effectiveLight = LightColor * LightIntensity;

    glm::vec3 toLight    = LightPos - hitPoint;
    float     distToLight = glm::length(toLight);
    glm::vec3 l          = toLight / distToLight;

    // Early exit: surface faces away from the light — no contribution possible.
    float NdotL = glm::dot(normal, l);
    if (NdotL <= 0.0f)
        return glm::vec3(0.0f);

    // Center probe: cast one shadow ray toward the light center first.
    // If it is blocked we treat the point as fully in shadow and skip the
    // remaining 8 area-light samples entirely.
    {
        URay shadowRay(hitPoint + 1e-4f * normal, l);
        for (const AActor* other : scene.Actors)
        {
            float st;
            if (other->surface->intersect(shadowRay, st) && st < distToLight)
                return glm::vec3(0.0f);
        }
    }

    // Center was lit — sample the remaining 8 area-light positions to get
    // a soft-shadow fraction.  litFraction starts at 1 (center already lit).
    // Tangent frame for area-light disk sampling
    glm::vec3 up        = (glm::abs(l.x) > 0.9f) ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
    glm::vec3 tangent   = glm::normalize(glm::cross(l, up));
    glm::vec3 bitangent = glm::cross(l, tangent);

    const float offsets[SOFT_SHADOW_SAMPLES - 1][2] = {
        { 1.00f,  0.00f}, {-1.00f,  0.00f}, { 0.00f,  1.00f}, { 0.00f, -1.00f},
        { 0.71f,  0.71f}, {-0.71f,  0.71f}, {-0.71f, -0.71f}, { 0.71f, -0.71f}
    };

    float litFraction = 1.0f;
    for (int s = 0; s < SOFT_SHADOW_SAMPLES - 1; ++s)
    {
        glm::vec3 samplePos = LightPos
            + SOFT_SHADOW_RADIUS * (offsets[s][0] * tangent + offsets[s][1] * bitangent);
        glm::vec3 toSample = samplePos - hitPoint;
        float     sDist    = glm::length(toSample);
        glm::vec3 sl       = toSample / sDist;

        URay shadowRay(hitPoint + 1e-4f * normal, sl);
        bool blocked = false;
        for (const AActor* other : scene.Actors)
        {
            float st;
            if (other->surface->intersect(shadowRay, st) && st < sDist)
            {
                blocked = true;
                break;
            }
        }
        if (!blocked) litFraction += 1.0f;
    }
    litFraction /= static_cast<float>(SOFT_SHADOW_SAMPLES);

    glm::vec3 color = actor->surface->getDiffuseColor(hitPoint) * effectiveLight * NdotL * litFraction;

    if (actor->surface->material.shininess > 0.0f)
    {
        glm::vec3 v     = glm::normalize(camera.eye - hitPoint);
        glm::vec3 h     = glm::normalize(l + v);
        float     NdotH = glm::max(0.0f, glm::dot(normal, h));
        color += actor->surface->material.ks * effectiveLight * glm::pow(NdotH, actor->surface->material.shininess) * litFraction;
    }

    return color;
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

    info.shadeContrib =
        "    for (int i = 0; i < uNPointLight; ++i)\n"
        "        col += shadePointLight(p, n, type, idx, i);\n";

    return info;
}
