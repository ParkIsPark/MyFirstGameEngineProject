#include "EnvironmentLight.h"
#include "AActor.h"
#include "UScene.h"
#include "ACamera.h"
#include "URay.h"
#include "URayTracing.h"
#include <cfloat>
#include <cmath>
#include <string>
#include <glm/gtc/constants.hpp>

// ---------------------------------------------------------------------------
// genBounceGLSL — C++ recursive helper that builds GLSL nested-loop code for
// multi-bounce hemisphere GI without using GLSL recursion.
//
//   d        : current bounce index (0 = first hemisphere from primary hit)
//   maxDepth : GI_BOUNCE_DEPTH - 1  (0 → 1 level, 1 → 2 levels, …)
//   ind      : current GLSL indentation string
//
// Variables emitted per level d (all names suffixed with d to avoid clashes):
//   up{d}, tng{d}, btn{d}   — tangent frame around source normal
//   acc{d}                  — accumulator for this level's samples
//   i{d}, cosT{d}, sinT{d}, phi{d}, dir{d}  — loop / sample vars
//   hType{d}, hIdx{d}, t{d} — intersection result
//   hp{d}, hn{d}, c{d}      — hit point / normal / color (hit branch only)
//
// Source point/normal for depth d:
//   d == 0  →  "p" / "n"  (shadeEnvLight parameters)
//   d > 0   →  "hp{d-1}" / "hn{d-1}"  (previous bounce hit)
// ---------------------------------------------------------------------------
static std::string genBounceGLSL(int d, int maxDepth, const std::string& ind)
{
    const std::string D    = std::to_string(d);
    const std::string srcP = (d == 0) ? "p"  : ("hp" + std::to_string(d - 1));
    const std::string srcN = (d == 0) ? "n"  : ("hn" + std::to_string(d - 1));
    const std::string in1  = ind + "    ";
    const std::string in2  = in1 + "    ";

    std::string c;

    // Tangent frame around srcN
    c += ind + "vec3 up"  + D + "  = (abs(" + srcN + ".x) > 0.9) ? vec3(0,1,0) : vec3(1,0,0);\n";
    c += ind + "vec3 tng" + D + " = normalize(cross(" + srcN + ", up" + D + "));\n";
    c += ind + "vec3 btn" + D + " = cross(" + srcN + ", tng" + D + ");\n";
    c += ind + "vec3 acc" + D + " = vec3(0.0);\n";

    // Hemisphere sample loop
    c += ind + "for (int i" + D + " = 0; i" + D + " < ENV_LIGHT_SAMPLES; ++i" + D + ") {\n";
    c += in1 + "float cosT" + D + " = sqrt(1.0 - (float(i" + D + ") + 0.5) / float(ENV_LIGHT_SAMPLES));\n";
    c += in1 + "float sinT" + D + " = sqrt(max(0.0, 1.0 - cosT" + D + "*cosT" + D + "));\n";
    c += in1 + "float phi"  + D + "  = GOLDEN_ANGLE * float(i" + D + ");\n";
    c += in1 + "vec3  dir"  + D + "  = normalize("
               "sinT" + D + "*cos(phi" + D + ")*tng" + D + " + "
               "sinT" + D + "*sin(phi" + D + ")*btn" + D + " + "
               "cosT" + D + "*" + srcN + ");\n";
    c += in1 + "int   hType" + D + ", hIdx" + D + ";\n";
    c += in1 + "float t" + D + " = findClosest(" + srcP + " + 1e-4*" + srcN
               + ", dir" + D + ", hType" + D + ", hIdx" + D + ");\n";

    // Hit branch
    c += in1 + "if (hType" + D + " >= 0) {\n";
    c += in2 + "vec3 hp" + D + " = " + srcP + " + 1e-4*" + srcN + " + t" + D + "*dir" + D + ";\n";
    c += in2 + "vec3 hn" + D + " = getNormal(hType" + D + ", hIdx" + D + ", hp" + D + ");\n";
    c += in2 + "vec3 c"  + D + "  = getEmit(hType" + D + ", hIdx" + D + ")\n";
    c += in2 + "         + getKa(hType"  + D + ", hIdx" + D + ")\n";
    c += in2 + "         + shadeDirect(hp" + D + ", hn" + D + ", hType" + D + ", hIdx" + D + ");\n";

    if (d < maxDepth) {
        // Nest the next bounce level inside this hit block
        c += genBounceGLSL(d + 1, maxDepth, in2);
        // Add the deeper indirect contribution weighted by this surface's ka
        c += in2 + "c" + D + " += getKa(hType" + D + ", hIdx" + D + ") * uEnvEffect"
                 + " * (acc" + std::to_string(d + 1) + " / float(ENV_LIGHT_SAMPLES));\n";
    }

    c += in2 + "acc" + D + " += c" + D + ";\n";

    // Miss branch — sky gradient
    c += in1 + "} else {\n";
    c += in2 + "float blend" + D + " = 0.5 * (dir" + D + ".y + 1.0);\n";
    c += in2 + "acc" + D + " += mix(vec3(0.3, 0.2, 0.1), vec3(0.5, 0.7, 1.0), blend" + D + ");\n";
    c += in1 + "}\n";
    c += ind + "}\n";   // end for

    return c;
}

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

    // Dynamically generate shadeEnvLight with GI_BOUNCE_DEPTH levels of
    // nested hemisphere loops — ENV_LIGHT_SAMPLES used in every loop level.
    // GI_BOUNCE_DEPTH=1 → single loop (same as before)
    // GI_BOUNCE_DEPTH=2 → N² rays/pixel for indirect
    // GI_BOUNCE_DEPTH=3 → N³ rays/pixel for indirect  (N = ENV_LIGHT_SAMPLES)
    info.functions  = "\nvec3 shadeEnvLight(vec3 p, vec3 n, int type, int idx)\n{\n";
    info.functions += genBounceGLSL(0, GI_BOUNCE_DEPTH - 1, "    ");
    info.functions += "    return getKa(type, idx) * uEnvEffect * (acc0 / float(ENV_LIGHT_SAMPLES));\n}\n";

    info.indirectContrib = "    col += shadeEnvLight(p, n, type, idx);\n";
    // directContrib intentionally empty — EnvironmentLight is indirect-only

    return info;
}
