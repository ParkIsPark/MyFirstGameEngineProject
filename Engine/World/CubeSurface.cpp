#include "CubeSurface.h"
#include "AActor.h"
#include <cmath>
#include <string>
#include <algorithm>
#include <glm/gtc/type_ptr.hpp>
#include <GL/glew.h>

CubeSurface::CubeSurface(float half)
    : halfVec(glm::vec3(half))
{
}

CubeSurface::CubeSurface(glm::vec3 halfExtents)
    : halfVec(halfExtents)
{
}

bool CubeSurface::intersect(const URay& ray, float& tHit) const
{
    glm::vec3 mn = owner->position - halfVec;
    glm::vec3 mx = owner->position + halfVec;

    float tMin = 1e-4f;
    float tMax = 1e30f;

    for (int i = 0; i < 3; ++i)
    {
        float invD = 1.0f / ray.direction[i];
        float t0   = (mn[i] - ray.origin[i]) * invD;
        float t1   = (mx[i] - ray.origin[i]) * invD;
        if (invD < 0.0f) std::swap(t0, t1);
        tMin = glm::max(tMin, t0);
        tMax = glm::min(tMax, t1);
        if (tMax < tMin) return false;
    }

    tHit = tMin;
    return true;
}

glm::vec3 CubeSurface::getNormal(const glm::vec3& hitPoint) const
{
    glm::vec3 local = hitPoint - owner->position;
    // Normalise each axis by its half-extent so the dominant face is the one
    // whose ratio is closest to 1 (i.e. the surface we hit).
    glm::vec3 absL  = glm::abs(local) / halfVec;

    glm::vec3 normal(0.0f);
    if (absL.x >= absL.y && absL.x >= absL.z)
        normal.x = (local.x > 0.0f) ? 1.0f : -1.0f;
    else if (absL.y >= absL.x && absL.y >= absL.z)
        normal.y = (local.y > 0.0f) ? 1.0f : -1.0f;
    else
        normal.z = (local.z > 0.0f) ? 1.0f : -1.0f;

    return normal;
}

void CubeSurface::SetColor(const glm::vec3& TargetColor)
{
    material.kd = TargetColor;
}

// Per-face UV mapping for a cube.
// Uses the same dominant-axis logic as getNormal() to determine which face was hit,
// then maps the two local axes of that face to [0, 1].
//   +X/-X face : uses Z and Y axes
//   +Y/-Y face : uses X and Z axes  (floor / ceiling texture)
//   +Z/-Z face : uses X and Y axes
glm::vec2 CubeSurface::getUV(const glm::vec3& hitPoint) const
{
    glm::vec3 local = hitPoint - owner->position;
    glm::vec3 absL  = glm::abs(local) / halfVec; // normalised per-axis

    float fu, fv, hu, hv;
    if (absL.x >= absL.y && absL.x >= absL.z)
    {
        // +X/-X face: use Z and Y axes
        fu = (local.x > 0.0f) ? -local.z : local.z;
        fv = local.y;
        hu = halfVec.z;
        hv = halfVec.y;
    }
    else if (absL.y >= absL.x && absL.y >= absL.z)
    {
        // +Y/-Y face (floor/ceiling): use X and Z axes
        fu = local.x;
        fv = (local.y > 0.0f) ? local.z : -local.z;
        hu = halfVec.x;
        hv = halfVec.z;
    }
    else
    {
        // +Z/-Z face: use X and Y axes
        fu = (local.z > 0.0f) ? local.x : -local.x;
        fv = local.y;
        hu = halfVec.x;
        hv = halfVec.y;
    }

    // [-h, h] -> [0, 1]  (tiled: large walls repeat the texture naturally)
    float u = fu / (2.0f * hu) + 0.5f;
    float v = fv / (2.0f * hv) + 0.5f;
    return glm::vec2(u, v);
}

void CubeSurface::uploadUniform(unsigned int /*prog*/, int /*idx*/) const
{
    // Cube data is now uploaded as a UBO batch by IntersectionPass — no-op here.
}

void CubeSurface::getUBOData(float* dst) const
{
    // 6 vec4s packed in std140 order (24 floats, 96 bytes per cube):
    //  [0] posAndTex    xyz=pos,   w=hasTex(0) or no-tex(-1)
    //  [1] halfAndShiny xyz=half,  w=shininess
    //  [2] ka           xyz=ka,    w=0
    //  [3] kd           xyz=kd,    w=0
    //  [4] ks           xyz=ks,    w=0
    //  [5] km           xyz=km,    w=0
    const glm::vec3& p = owner->position;
    dst[ 0]=p.x;              dst[ 1]=p.y;              dst[ 2]=p.z;              dst[ 3]=(material.texture!=0)?0.f:-1.f;
    dst[ 4]=halfVec.x;        dst[ 5]=halfVec.y;        dst[ 6]=halfVec.z;        dst[ 7]=material.shininess;
    dst[ 8]=material.ka.x;    dst[ 9]=material.ka.y;    dst[10]=material.ka.z;    dst[11]=0.f;
    dst[12]=material.kd.x;    dst[13]=material.kd.y;    dst[14]=material.kd.z;    dst[15]=0.f;
    dst[16]=material.ks.x;    dst[17]=material.ks.y;    dst[18]=material.ks.z;    dst[19]=0.f;
    dst[20]=material.km.x;    dst[21]=material.km.y;    dst[22]=material.km.z;    dst[23]=0.f;
}

SurfaceGLSLInfo CubeSurface::getGLSLInfo() const
{
    SurfaceGLSLInfo info;
    info.typeId       = 1;
    info.hitFuncName  = "cubeHit";
    info.countUniform = "uNCube";

    // -----------------------------------------------------------------------
    // Cube data stored in a UBO (std140) — no register pressure.
    //   struct CubeData { posAndTex, halfAndShiny, ka, kd, ks, km } (6 vec4s)
    //   MAX_CUBES=128 → 128×96 bytes = 12 KB < 16 KB minimum UBO guarantee
    // -----------------------------------------------------------------------
    info.uniforms =
        "\n#define MAX_CUBES 256\n"
        "struct CubeData {\n"
        "    vec4 posAndTex;\n"       // xyz=pos,   w=hasTex(0=yes,-1=no)
        "    vec4 halfAndShiny;\n"    // xyz=half,  w=shininess
        "    vec4 ka;\n"
        "    vec4 kd;\n"
        "    vec4 ks;\n"
        "    vec4 km;\n"
        "};\n"
        "layout(std140) uniform CubeBlock {\n"
        "    CubeData uCubes[MAX_CUBES];\n"
        "};\n"
        "uniform int uNCube;\n";

    info.hitFunc =
        "\nfloat cubeHit(vec3 ro, vec3 rd, int i)\n"
        "{\n"
        "    vec3  mn   = uCubes[i].posAndTex.xyz - uCubes[i].halfAndShiny.xyz;\n"
        "    vec3  mx   = uCubes[i].posAndTex.xyz + uCubes[i].halfAndShiny.xyz;\n"
        "    float tMin = 1e-4, tMax = 1e30;\n"
        "    for (int k = 0; k < 3; ++k) {\n"
        "        float invD = 1.0 / rd[k];\n"
        "        float t0   = (mn[k] - ro[k]) * invD;\n"
        "        float t1   = (mx[k] - ro[k]) * invD;\n"
        "        if (invD < 0.0) { float tmp = t0; t0 = t1; t1 = tmp; }\n"
        "        tMin = max(tMin, t0);\n"
        "        tMax = min(tMax, t1);\n"
        "        if (tMax < tMin) return -1.0;\n"
        "    }\n"
        "    return tMin;\n"
        "}\n";

    info.normalCase =
        "    if (type == 1) {\n"
        "        vec3 local = p - uCubes[idx].posAndTex.xyz;\n"
        "        vec3 absL  = abs(local) / uCubes[idx].halfAndShiny.xyz;\n"
        "        if (absL.x >= absL.y && absL.x >= absL.z) return vec3(sign(local.x), 0.0, 0.0);\n"
        "        if (absL.y >= absL.x && absL.y >= absL.z) return vec3(0.0, sign(local.y), 0.0);\n"
        "        return vec3(0.0, 0.0, sign(local.z));\n"
        "    }\n";

    info.diffuseCase =
        "    if (type == 1) {\n"
        "        if (int(uCubes[idx].posAndTex.w) == 0) {\n"
        "            vec3  local = p - uCubes[idx].posAndTex.xyz;\n"
        "            vec3  h     = uCubes[idx].halfAndShiny.xyz;\n"
        "            vec3  absL  = abs(local) / h;\n"
        "            float fu, fv, hu, hv;\n"
        "            if (absL.x >= absL.y && absL.x >= absL.z) {\n"
        "                fu = (local.x > 0.0) ? -local.z : local.z;\n"
        "                fv = local.y;  hu = h.z;  hv = h.y;\n"
        "            } else if (absL.y >= absL.x && absL.y >= absL.z) {\n"
        "                fu = local.x;\n"
        "                fv = (local.y > 0.0) ? local.z : -local.z;\n"
        "                hu = h.x;  hv = h.z;\n"
        "            } else {\n"
        "                fu = (local.z > 0.0) ? local.x : -local.x;\n"
        "                fv = local.y;  hu = h.x;  hv = h.y;\n"
        "            }\n"
        "            return texture(uBrickTex, vec2(fu/(2.0*hu)+0.5, fv/(2.0*hv)+0.5)).rgb;\n"
        "        }\n"
        "        return uCubes[idx].kd.xyz;\n"
        "    }\n";

    info.kaCase    = "    if (t == 1) return uCubes[i].ka.xyz;\n";
    info.ksCase    = "    if (t == 1) return uCubes[i].ks.xyz;\n";
    info.shinyCase = "    if (t == 1) return uCubes[i].halfAndShiny.w;\n";
    info.kmCase    = "    if (t == 1) return uCubes[i].km.xyz;\n";
    info.emitCase  = "";

    return info;
}
