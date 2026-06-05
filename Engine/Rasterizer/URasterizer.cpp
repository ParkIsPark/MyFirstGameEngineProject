#include "URasterizer.h"
#include "FTransform.h"
#include "UFrameBuffer.h"
#include "UGBuffer.h"
#include "UMesh.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

float URasterizer::EdgeFunction(const glm::vec2& a, const glm::vec2& b, const glm::vec2& p)
{
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

void URasterizer::RasterizeTriangle(const glm::vec3& s0, const glm::vec3& s1, const glm::vec3& s2,
                                    const glm::vec3& color, UFrameBuffer& fb) const
{
    const glm::vec2 p0(s0.x, s0.y);
    const glm::vec2 p1(s1.x, s1.y);
    const glm::vec2 p2(s2.x, s2.y);

    const float area = EdgeFunction(p0, p1, p2);
    if (area == 0.0f) return;                 // degenerate
    const float inv = 1.0f / area;

    int minX = static_cast<int>(std::floor(std::min({ p0.x, p1.x, p2.x })));
    int maxX = static_cast<int>(std::ceil (std::max({ p0.x, p1.x, p2.x })));
    int minY = static_cast<int>(std::floor(std::min({ p0.y, p1.y, p2.y })));
    int maxY = static_cast<int>(std::ceil (std::max({ p0.y, p1.y, p2.y })));
    minX = std::max(minX, 0);        minY = std::max(minY, 0);
    maxX = std::min(maxX, fb.nx - 1); maxY = std::min(maxY, fb.ny - 1);

    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            const glm::vec2 p(x + 0.5f, y + 0.5f);
            const float w0 = EdgeFunction(p1, p2, p);
            const float w1 = EdgeFunction(p2, p0, p);
            const float w2 = EdgeFunction(p0, p1, p);

            const bool inside = (area > 0.0f)
                ? (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                : (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
            if (!inside) continue;

            const float a = w0 * inv, b = w1 * inv, c = w2 * inv;   // barycentric
            const float z = a * s0.z + b * s1.z + c * s2.z;
            fb.TestAndSet(x, y, z, color);
        }
    }
}

void URasterizer::DrawTriangle(const glm::vec3& o0, const glm::vec3& o1, const glm::vec3& o2,
                               const FTransform& xf, const glm::vec3& color, UFrameBuffer& fb) const
{
    const glm::vec3 s0 = xf.ToScreen(xf.ToClip(o0));
    const glm::vec3 s1 = xf.ToScreen(xf.ToClip(o1));
    const glm::vec3 s2 = xf.ToScreen(xf.ToClip(o2));
    RasterizeTriangle(s0, s1, s2, color, fb);
}

void URasterizer::DrawMesh(const UMesh& mesh, const FTransform& xf,
                           const glm::vec3& color, UFrameBuffer& fb) const
{
    const int nTri = mesh.triangleCount();
    for (int tri = 0; tri < nTri; ++tri)
    {
        const glm::vec3& o0 = mesh.vertices[mesh.indices[3 * tri + 0]].position;
        const glm::vec3& o1 = mesh.vertices[mesh.indices[3 * tri + 1]].position;
        const glm::vec3& o2 = mesh.vertices[mesh.indices[3 * tri + 2]].position;
        DrawTriangle(o0, o1, o2, xf, color, fb);
    }
}

// ---------------------------------------------------------------------------
// G-buffer rasterization — same edge-function fill, but each covered pixel
// stores perspective-correct world position + normal + albedo (deferred).
// ---------------------------------------------------------------------------
void URasterizer::DrawMeshGBuffer(const UMesh& mesh, const FTransform& xf,
                                  UGBuffer& gb) const
{
    DrawMeshGBuffer(mesh, xf, mesh.material.kd, gb);
}

namespace
{
    // A vertex carried through clip space with the attributes we interpolate.
    struct ClipV { glm::vec4 clip; glm::vec3 wp; glm::vec3 wn; glm::vec2 uv; };

    ClipV lerpClip(const ClipV& A, const ClipV& B, float t)
    {
        return ClipV{ A.clip + t * (B.clip - A.clip),
                      A.wp   + t * (B.wp   - A.wp),
                      A.wn   + t * (B.wn   - A.wn),
                      A.uv   + t * (B.uv   - A.uv) };
    }

    // Inside (visible side) when the vertex is in FRONT of the camera plane,
    // i.e. clip.w (= z_eye for this FCG projection) is sufficiently negative.
    // Crossing this plane is exactly where the perspective divide flips, so we
    // clip here BEFORE the divide.
    constexpr float NEAR_EPS = 1e-4f;
    inline float frontDist(const glm::vec4& c) { return -NEAR_EPS - c.w; }

    // Sutherland-Hodgman against the single near/front plane. poly may grow to 4.
    void clipNear(const ClipV in[3], std::vector<ClipV>& out)
    {
        out.clear();
        for (int i = 0; i < 3; ++i)
        {
            const ClipV& A = in[i];
            const ClipV& B = in[(i + 1) % 3];
            const float dA = frontDist(A.clip), dB = frontDist(B.clip);
            const bool inA = dA > 0.0f, inB = dB > 0.0f;
            if (inA) out.push_back(A);
            if (inA != inB) out.push_back(lerpClip(A, B, dA / (dA - dB)));
        }
    }

    // Cohen-Sutherland-style trivial reject: all three vertices outside the same
    // clip-volume plane (handles the negative-w convention via abs(w)).
    bool frustumReject(const glm::vec4& a, const glm::vec4& b, const glm::vec4& c)
    {
        auto out = [](const glm::vec4& p) {
            const float w = std::fabs(p.w);
            int o = 0;
            if (p.x >  w) o |= 1;  if (p.x < -w) o |= 2;
            if (p.y >  w) o |= 4;  if (p.y < -w) o |= 8;
            return o;
        };
        return (out(a) & out(b) & out(c)) != 0;
    }
}

void URasterizer::DrawMeshGBuffer(const UMesh& mesh, const FTransform& xf,
                                  const glm::vec3& albedo, UGBuffer& gb,
                                  int cx0, int cy0, int cx1, int cy1, bool countStats,
                                  const Material* mat, glm::vec2 uvScale) const
{
    const glm::mat3 nrmM = glm::inverseTranspose(glm::mat3(xf.model));
    const int nTri = mesh.triangleCount();
    const bool textured = mat && !mat->texData.empty();

    // Tile bounds (clamped to the G-buffer). A multithreaded fill gives each tile
    // a disjoint pixel rect so TestAndSet never races.
    const int tx0 = std::max(cx0, 0),          ty0 = std::max(cy0, 0);
    const int tx1 = std::min(cx1, gb.nx - 1),  ty1 = std::min(cy1, gb.ny - 1);

    // Rasterize one screen-space sub-triangle (with its clip-space attributes).
    auto rasterTri = [&](const ClipV& A, const ClipV& B, const ClipV& C)
    {
        const glm::vec3 s0 = xf.ToScreen(A.clip);
        const glm::vec3 s1 = xf.ToScreen(B.clip);
        const glm::vec3 s2 = xf.ToScreen(C.clip);
        const float iw0 = 1.0f / A.clip.w, iw1 = 1.0f / B.clip.w, iw2 = 1.0f / C.clip.w;

        const glm::vec2 p0(s0.x, s0.y), p1(s1.x, s1.y), p2(s2.x, s2.y);
        const float area = EdgeFunction(p0, p1, p2);
        if (area == 0.0f) return;
        if (backfaceCull && area < 0.0f) { if (countStats) ++stats.backfaceCulled; return; }
        if (countStats) ++stats.rasterized;
        const float inv = 1.0f / area;

        int minX = static_cast<int>(std::floor(std::min({ p0.x, p1.x, p2.x })));
        int maxX = static_cast<int>(std::ceil (std::max({ p0.x, p1.x, p2.x })));
        int minY = static_cast<int>(std::floor(std::min({ p0.y, p1.y, p2.y })));
        int maxY = static_cast<int>(std::ceil (std::max({ p0.y, p1.y, p2.y })));
        minX = std::max(minX, tx0);  minY = std::max(minY, ty0);
        maxX = std::min(maxX, tx1);  maxY = std::min(maxY, ty1);

        for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x)
        {
            const glm::vec2 p(x + 0.5f, y + 0.5f);
            const float w0 = EdgeFunction(p1, p2, p);
            const float w1 = EdgeFunction(p2, p0, p);
            const float w2 = EdgeFunction(p0, p1, p);
            const bool inside = (area > 0.0f)
                ? (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                : (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
            if (!inside) continue;

            const float al = w0 * inv, be = w1 * inv, ga = w2 * inv;
            const float z  = al * s0.z + be * s1.z + ga * s2.z;
            const float pw = al * iw0 + be * iw1 + ga * iw2;
            const glm::vec3 wp = (al * A.wp * iw0 + be * B.wp * iw1 + ga * C.wp * iw2) / pw;
            const glm::vec3 wn = glm::normalize(
                                 (al * A.wn * iw0 + be * B.wn * iw1 + ga * C.wn * iw2) / pw);
            glm::vec3 alb = albedo;
            if (textured)
            {
                const glm::vec2 uv = (al * A.uv * iw0 + be * B.uv * iw1 + ga * C.uv * iw2) / pw;
                alb = mat->SampleDiffuse(uv * uvScale);
            }
            gb.TestAndSet(x, y, z, wp, wn, alb);
        }
    };

    for (int tri = 0; tri < nTri; ++tri)
    {
        if (countStats) ++stats.trianglesIn;
        const Vertex& a = mesh.vertices[mesh.indices[3 * tri + 0]];
        const Vertex& b = mesh.vertices[mesh.indices[3 * tri + 1]];
        const Vertex& c = mesh.vertices[mesh.indices[3 * tri + 2]];

        ClipV v[3] = {
            { xf.ToClip(a.position), glm::vec3(xf.model * glm::vec4(a.position, 1.0f)), nrmM * a.normal, a.uv },
            { xf.ToClip(b.position), glm::vec3(xf.model * glm::vec4(b.position, 1.0f)), nrmM * b.normal, b.uv },
            { xf.ToClip(c.position), glm::vec3(xf.model * glm::vec4(c.position, 1.0f)), nrmM * c.normal, c.uv },
        };

        if (frustumCull && frustumReject(v[0].clip, v[1].clip, v[2].clip))
        { if (countStats) ++stats.frustumCulled; continue; }

        if (nearClip)
        {
            std::vector<ClipV> poly;
            clipNear(v, poly);
            for (size_t k = 1; k + 1 < poly.size(); ++k)   // fan-triangulate
                rasterTri(poly[0], poly[k], poly[k + 1]);
        }
        else
        {
            rasterTri(v[0], v[1], v[2]);
        }
    }
}

// ---------------------------------------------------------------------------
// CPU shaded raster (HW6 Q1-Q3): Flat / Gouraud / Phong + Blinn-Phong + gamma.
// ---------------------------------------------------------------------------
namespace
{
    // Shaded vertex carried through clip space (col used by Gouraud, uv for texture).
    struct SV { glm::vec4 clip; glm::vec3 wp; glm::vec3 wn; glm::vec3 col; glm::vec2 uv; };

    SV lerpSV(const SV& A, const SV& B, float t)
    {
        return SV{ A.clip + t * (B.clip - A.clip), A.wp + t * (B.wp - A.wp),
                   A.wn + t * (B.wn - A.wn), A.col + t * (B.col - A.col),
                   A.uv + t * (B.uv - A.uv) };
    }
    void clipNearSV(const SV in[3], std::vector<SV>& out)
    {
        out.clear();
        for (int i = 0; i < 3; ++i)
        {
            const SV& A = in[i];
            const SV& B = in[(i + 1) % 3];
            const float dA = frontDist(A.clip), dB = frontDist(B.clip);
            const bool inA = dA > 0.0f, inB = dB > 0.0f;
            if (inA) out.push_back(A);
            if (inA != inB) out.push_back(lerpSV(A, B, dA / (dA - dB)));
        }
    }
    // Blinn-Phong (LINEAR; gamma is applied at the pixel). HW6 formula:
    //   L = ka*Ia + kd*I*max(0,n.l) + ks*I*max(0,n.h)^p
    glm::vec3 shadeLinear(const glm::vec3& P, const glm::vec3& N,
                          const Material& m, const FShadeParams& sp, const glm::vec3& kd)
    {
        const glm::vec3 v = glm::normalize(sp.eye - P);
        const float     p = glm::max(m.shininess, 1.0f);

        // Per-light Blinn-Phong diffuse + specular (no ambient -- added once below).
        // kd is the (possibly textured) diffuse color; ka/ks/shininess come from m.
        auto contrib = [&](const glm::vec3& lpos, const glm::vec3& lcol) -> glm::vec3
        {
            const glm::vec3 toL = lpos - P;
            const float     d   = glm::length(toL);
            const glm::vec3 l   = (d > 1e-6f) ? toL / d : glm::vec3(0, 1, 0);
            const glm::vec3 h   = glm::normalize(l + v);
            const float ndl = glm::max(0.0f, glm::dot(N, l));
            const float ndh = glm::max(0.0f, glm::dot(N, h));
            return kd * lcol * ndl + m.ks * lcol * std::pow(ndh, p);
        };

        // Ambient: environment gradient (matches the GPU matte ambient) or the
        // HW6 flat ka*Ia.
        glm::vec3 amb;
        if (sp.envAmbient)
        {
            const float k = std::pow(glm::clamp(N.y * 0.5f + 0.5f, 0.0f, 1.0f), glm::max(sp.skyExp, 0.01f));
            amb = kd * glm::mix(sp.skyHorizon, sp.skyZenith, k) * 0.5f * sp.ambientMul;
        }
        else amb = m.ka * sp.ambient;

        glm::vec3 col = amb + contrib(sp.lightPos, sp.lightColor);
        for (size_t i = 0; i < sp.extraLightPos.size(); ++i)
            col += contrib(sp.extraLightPos[i], sp.extraLightColor[i]);
        return col;
    }
}

void URasterizer::DrawMeshShaded(const UMesh& mesh, const FTransform& xf, const Material* matOverride,
                                 const FShadeParams& sp, EShadingModel model, UFrameBuffer& fb,
                                 int triBegin, int triEnd, glm::vec2 uvScale) const
{
    const glm::mat3 nrmM = glm::inverseTranspose(glm::mat3(xf.model));
    const int nTri  = mesh.triangleCount();
    const int tLo   = std::max(0, triBegin);
    const int tHi   = std::min(nTri, triEnd);
    const glm::vec3 invG(1.0f / 2.2f);
    const Material* curMat = nullptr;

    auto rasterTri = [&](const SV& A, const SV& B, const SV& C,
                         const glm::vec3& flatCol, bool flat)
    {
        const glm::vec3 s0 = xf.ToScreen(A.clip), s1 = xf.ToScreen(B.clip), s2 = xf.ToScreen(C.clip);
        const float iw0 = 1.0f / A.clip.w, iw1 = 1.0f / B.clip.w, iw2 = 1.0f / C.clip.w;
        const glm::vec2 p0(s0.x, s0.y), p1(s1.x, s1.y), p2(s2.x, s2.y);
        const float area = EdgeFunction(p0, p1, p2);
        if (area == 0.0f) return;
        if (backfaceCull && area < 0.0f) return;
        const float inv = 1.0f / area;

        int minX = (int)std::floor(std::min({ p0.x, p1.x, p2.x }));
        int maxX = (int)std::ceil (std::max({ p0.x, p1.x, p2.x }));
        int minY = (int)std::floor(std::min({ p0.y, p1.y, p2.y }));
        int maxY = (int)std::ceil (std::max({ p0.y, p1.y, p2.y }));
        minX = std::max(minX, 0);            minY = std::max(minY, 0);
        maxX = std::min(maxX, fb.nx - 1);    maxY = std::min(maxY, fb.ny - 1);

        for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x)
        {
            const glm::vec2 p(x + 0.5f, y + 0.5f);
            const float w0 = EdgeFunction(p1, p2, p);
            const float w1 = EdgeFunction(p2, p0, p);
            const float w2 = EdgeFunction(p0, p1, p);
            const bool inside = (area > 0.0f)
                ? (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                : (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
            if (!inside) continue;

            const float al = w0 * inv, be = w1 * inv, ga = w2 * inv;
            const float z  = al * s0.z + be * s1.z + ga * s2.z;
            glm::vec3 lin;
            if (flat)
                lin = flatCol;
            else
            {
                const float pw = al * iw0 + be * iw1 + ga * iw2;
                if (model == EShadingModel::Gouraud)
                    lin = (al * A.col * iw0 + be * B.col * iw1 + ga * C.col * iw2) / pw;
                else // Phong
                {
                    const glm::vec3 wp = (al * A.wp * iw0 + be * B.wp * iw1 + ga * C.wp * iw2) / pw;
                    const glm::vec3 wn = glm::normalize((al * A.wn * iw0 + be * B.wn * iw1 + ga * C.wn * iw2) / pw);
                    glm::vec3 kd = curMat->kd;
                    if (!curMat->texData.empty())
                    {
                        const glm::vec2 uv = (al * A.uv * iw0 + be * B.uv * iw1 + ga * C.uv * iw2) / pw;
                        kd = curMat->SampleDiffuse(uv * uvScale);
                    }
                    lin = shadeLinear(wp, wn, *curMat, sp, kd);
                }
            }
            fb.TestAndSet(x, y, z, glm::pow(glm::clamp(lin, 0.0f, 1.0f), invG));
        }
    };

    for (int tri = tLo; tri < tHi; ++tri)
    {
        const Material& M = matOverride ? *matOverride : mesh.materialForTri(tri);
        curMat = &M;

        const Vertex& a = mesh.vertices[mesh.indices[3 * tri + 0]];
        const Vertex& b = mesh.vertices[mesh.indices[3 * tri + 1]];
        const Vertex& c = mesh.vertices[mesh.indices[3 * tri + 2]];

        SV v[3];
        v[0] = { xf.ToClip(a.position), glm::vec3(xf.model * glm::vec4(a.position, 1.0f)), nrmM * a.normal, glm::vec3(0.0f), a.uv };
        v[1] = { xf.ToClip(b.position), glm::vec3(xf.model * glm::vec4(b.position, 1.0f)), nrmM * b.normal, glm::vec3(0.0f), b.uv };
        v[2] = { xf.ToClip(c.position), glm::vec3(xf.model * glm::vec4(c.position, 1.0f)), nrmM * c.normal, glm::vec3(0.0f), c.uv };

        const bool textured = !M.texData.empty();

        if (model == EShadingModel::Gouraud)
            for (int i = 0; i < 3; ++i)
                v[i].col = shadeLinear(v[i].wp, glm::normalize(v[i].wn), M, sp,
                                       textured ? M.SampleDiffuse(v[i].uv * uvScale) : M.kd);

        const bool flat = (model == EShadingModel::Flat);
        glm::vec3 flatCol(0.0f);
        if (flat)
        {
            const glm::vec3 cen = (v[0].wp + v[1].wp + v[2].wp) / 3.0f;
            glm::vec3 fn = glm::normalize(glm::cross(v[1].wp - v[0].wp, v[2].wp - v[0].wp));
            if (glm::dot(fn, v[0].wn + v[1].wn + v[2].wn) < 0.0f) fn = -fn; // outward
            const glm::vec2 cuv = (v[0].uv + v[1].uv + v[2].uv) / 3.0f;
            flatCol = shadeLinear(cen, fn, M, sp, textured ? M.SampleDiffuse(cuv * uvScale) : M.kd);
        }

        if (frustumCull && frustumReject(v[0].clip, v[1].clip, v[2].clip)) continue;

        if (nearClip)
        {
            SV in[3] = { v[0], v[1], v[2] };
            std::vector<SV> poly;
            clipNearSV(in, poly);
            for (size_t k = 1; k + 1 < poly.size(); ++k)
                rasterTri(poly[0], poly[k], poly[k + 1], flatCol, flat);
        }
        else
        {
            rasterTri(v[0], v[1], v[2], flatCol, flat);
        }
    }
}
