#include "URasterizer.h"
#include "FTransform.h"
#include "UFrameBuffer.h"
#include "UGBuffer.h"
#include "UMesh.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <cmath>

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

void URasterizer::DrawMeshGBuffer(const UMesh& mesh, const FTransform& xf,
                                  const glm::vec3& albedo, UGBuffer& gb) const
{
    const glm::mat3 nrmM = glm::inverseTranspose(glm::mat3(xf.model));
    const int nTri = mesh.triangleCount();

    for (int tri = 0; tri < nTri; ++tri)
    {
        const Vertex& a = mesh.vertices[mesh.indices[3 * tri + 0]];
        const Vertex& b = mesh.vertices[mesh.indices[3 * tri + 1]];
        const Vertex& c = mesh.vertices[mesh.indices[3 * tri + 2]];

        // clip -> screen (keep clip.w for perspective-correct interpolation)
        const glm::vec4 c0 = xf.ToClip(a.position);
        const glm::vec4 c1 = xf.ToClip(b.position);
        const glm::vec4 c2 = xf.ToClip(c.position);
        const glm::vec3 s0 = xf.ToScreen(c0);
        const glm::vec3 s1 = xf.ToScreen(c1);
        const glm::vec3 s2 = xf.ToScreen(c2);
        const float iw0 = 1.0f / c0.w, iw1 = 1.0f / c1.w, iw2 = 1.0f / c2.w;

        // world-space attributes at the vertices
        const glm::vec3 wp0 = glm::vec3(xf.model * glm::vec4(a.position, 1.0f));
        const glm::vec3 wp1 = glm::vec3(xf.model * glm::vec4(b.position, 1.0f));
        const glm::vec3 wp2 = glm::vec3(xf.model * glm::vec4(c.position, 1.0f));
        const glm::vec3 wn0 = nrmM * a.normal;
        const glm::vec3 wn1 = nrmM * b.normal;
        const glm::vec3 wn2 = nrmM * c.normal;

        const glm::vec2 p0(s0.x, s0.y), p1(s1.x, s1.y), p2(s2.x, s2.y);
        const float area = EdgeFunction(p0, p1, p2);
        if (area == 0.0f) continue;
        const float inv = 1.0f / area;

        int minX = static_cast<int>(std::floor(std::min({ p0.x, p1.x, p2.x })));
        int maxX = static_cast<int>(std::ceil (std::max({ p0.x, p1.x, p2.x })));
        int minY = static_cast<int>(std::floor(std::min({ p0.y, p1.y, p2.y })));
        int maxY = static_cast<int>(std::ceil (std::max({ p0.y, p1.y, p2.y })));
        minX = std::max(minX, 0);          minY = std::max(minY, 0);
        maxX = std::min(maxX, gb.nx - 1);  maxY = std::min(maxY, gb.ny - 1);

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
            const float z  = al * s0.z + be * s1.z + ga * s2.z;        // screen-linear depth

            // perspective-correct attribute interpolation (weight by 1/w)
            const float pw = al * iw0 + be * iw1 + ga * iw2;
            const glm::vec3 wp =
                (al * wp0 * iw0 + be * wp1 * iw1 + ga * wp2 * iw2) / pw;
            const glm::vec3 wn = glm::normalize(
                (al * wn0 * iw0 + be * wn1 * iw1 + ga * wn2 * iw2) / pw);

            gb.TestAndSet(x, y, z, wp, wn, albedo);
        }
    }
}
