#include "EnvironmentLight.h"
#include "AActor.h"
#include "UScene.h"
#include "ACamera.h"
#include "URay.h"
#include "URayTracing.h"
#include <cfloat>
#include <cmath>
#include <glm/gtc/constants.hpp>

EnvironmentLight::EnvironmentLight()
    : LightComponent(glm::vec3(1.0f), glm::vec3(1.0f))
{
}

EnvironmentLight::EnvironmentLight(glm::vec3 color, glm::vec3 intensity)
    : LightComponent(color, intensity)
{
}

glm::vec3 EnvironmentLight::illuminate(
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

    // Build orthonormal basis (tangent frame) around the surface normal
    glm::vec3 up        = (std::abs(normal.x) > 0.9f) ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
    glm::vec3 tangent   = glm::normalize(glm::cross(normal, up));
    glm::vec3 bitangent = glm::cross(normal, tangent);

    // Fibonacci spiral gives a well-distributed hemisphere sample set
    const float goldenAngle = glm::pi<float>() * (3.0f - std::sqrt(5.0f));

    glm::vec3 accumulated(0.0f);
    for (int i = 0; i < ENV_LIGHT_SAMPLES; ++i)
    {
        // cosTheta uniformly distributed in [0,1] -> uniform solid-angle hemisphere
        float cosTheta = std::sqrt(1.0f - (float(i) + 0.5f) / float(ENV_LIGHT_SAMPLES));
        float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
        float phi      = goldenAngle * float(i);

        glm::vec3 dir = sinTheta * std::cos(phi) * tangent
                      + sinTheta * std::sin(phi) * bitangent
                      + cosTheta * normal;
        dir = glm::normalize(dir);

        URay sampleRay(hitPoint + 1e-4f * normal, dir);

        // Find closest blocker
        float         minT     = FLT_MAX;
        const AActor* hitActor = nullptr;
        for (const AActor* other : scene.Actors)
        {
            float t;
            if (other->surface->intersect(sampleRay, t) && t < minT)
            {
                minT     = t;
                hitActor = other;
            }
        }

        if (hitActor)
        {
            if (tracer && depth < GI_BOUNCE_DEPTH)
            {
                // GI: recursively shade the hit point (multi-bounce indirect light)
                accumulated += tracer->TraceQ3(sampleRay, scene, camera, depth + 1);
            }
            else
            {
                // Depth limit reached: fall back to raw diffuse color
                glm::vec3 hp = sampleRay.origin + minT * sampleRay.direction;
                accumulated += hitActor->surface->getDiffuseColor(hp);
            }
        }
        else
        {
            // Unoccluded: sample sky gradient
            float blend = 0.5f * (dir.y + 1.0f);
            accumulated += glm::mix(glm::vec3(0.3f, 0.2f, 0.1f),
                                    glm::vec3(0.5f, 0.7f, 1.0f), blend);
        }
    }

    // Average over all samples — AO falls out naturally:
    // fully occluded surfaces get dark occluder colors, open surfaces get bright sky
    glm::vec3 avgColor = accumulated / float(ENV_LIGHT_SAMPLES);
    return actor->surface->material.ka * effectiveLight * avgColor;
}

LightGLSLInfo EnvironmentLight::getGLSLInfo() const
{
    LightGLSLInfo info;

    info.constants =
        "\nconst float GOLDEN_ANGLE = 3.14159265 * (3.0 - sqrt(5.0));\n";

    info.uniforms =
        "\nuniform vec3 uEnvEffect;\n";

    info.functions =
        "\nvec3 shadeEnvLight(vec3 p, vec3 n, int type, int idx)\n"
        "{\n"
        "    vec3 up  = (abs(n.x) > 0.9) ? vec3(0,1,0) : vec3(1,0,0);\n"
        "    vec3 tng = normalize(cross(n, up));\n"
        "    vec3 btn = cross(n, tng);\n"
        "    vec3 acc = vec3(0.0);\n"
        "    for (int i = 0; i < ENV_LIGHT_SAMPLES; ++i) {\n"
        "        float cosT = sqrt(1.0 - (float(i) + 0.5) / float(ENV_LIGHT_SAMPLES));\n"
        "        float sinT = sqrt(max(0.0, 1.0 - cosT*cosT));\n"
        "        float phi  = GOLDEN_ANGLE * float(i);\n"
        "        vec3  dir  = normalize(sinT*cos(phi)*tng + sinT*sin(phi)*btn + cosT*n);\n"
        "        int   hType, hIdx;\n"
        "        float t = findClosest(p + 1e-4*n, dir, hType, hIdx);\n"
        "        if (hType >= 0) {\n"
        "            // 1-bounce indirect: evaluate PointLight direct lighting at the hit point.\n"
        "            // EnvironmentLight is skipped here to avoid recursion.\n"
        "            vec3  hp   = p + 1e-4*n + t*dir;\n"
        "            vec3  hn   = getNormal(hType, hIdx, hp);\n"
        "            vec3  orig = hp + 1e-4*hn;\n"
        "            vec3  directCol = vec3(0.0);\n"
        "            for (int pi = 0; pi < uNPointLight; ++pi) {\n"
        "                vec3  toL   = uPLPos[pi] - hp;\n"
        "                float dist  = length(toL);\n"
        "                vec3  l     = toL / dist;\n"
        "                float NdotL = dot(hn, l);\n"
        "                if (NdotL <= 0.0) continue;\n"
        "                if (occluded(orig, l, dist)) continue;\n"
        "                vec3  eff   = uPLEffect[pi];\n"
        "                directCol  += getDiffuse(hType, hIdx, hp) * eff * NdotL;\n"
        "                float shiny = getShiny(hType, hIdx);\n"
        "                if (shiny > 0.0) {\n"
        "                    vec3 v = normalize(uEye - hp);\n"
        "                    vec3 h = normalize(l + v);\n"
        "                    directCol += getKs(hType, hIdx) * eff\n"
        "                               * pow(max(0.0, dot(hn, h)), shiny);\n"
        "                }\n"
        "            }\n"
        "            acc += directCol;\n"
        "        } else {\n"
        "            float blend = 0.5 * (dir.y + 1.0);\n"
        "            acc += mix(vec3(0.3, 0.2, 0.1), vec3(0.5, 0.7, 1.0), blend);\n"
        "        }\n"
        "    }\n"
        "    return getKa(type, idx) * uEnvEffect * (acc / float(ENV_LIGHT_SAMPLES));\n"
        "}\n";

    info.shadeContrib = "    col += shadeEnvLight(p, n, type, idx);\n";

    return info;
}
