#include "URasterizer.h"
#include "FTransform.h"
#include "UFrameBuffer.h"
#include "UMesh.h"

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
