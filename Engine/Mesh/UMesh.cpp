#include "UMesh.h"
#include "URay.h"
#include "BVH.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <limits>

UMesh::UMesh()  = default;          // out-of-line: BVH is complete here
UMesh::~UMesh() = default;

void UMesh::BuildBVH()
{
    bvh = std::make_unique<BVH>();
    bvh->Build(*this);
}

// ---------------------------------------------------------------------------
// GenerateSphere — same vertex order + index rule as course sphere_scene.cpp.
//   width = segW, height = segH.
//   positions scaled by `radius`; normals are the unit sphere directions.
// ---------------------------------------------------------------------------
UMesh* UMesh::GenerateSphere(float radius, int segW, int segH)
{
    const int width  = segW;
    const int height = segH;
    UMesh* m = new UMesh();

    const int nVerts = (height - 2) * width + 2;
    m->vertices.resize(nVerts);

    const float PI = glm::pi<float>();

    int t = 0;
    for (int j = 1; j < height - 1; ++j)
    {
        for (int i = 0; i < width; ++i)
        {
            const float theta = static_cast<float>(j) / (height - 1) * PI;
            const float phi   = static_cast<float>(i) / (width  - 1) * PI * 2.0f;

            const float x =  sinf(theta) * cosf(phi);
            const float y =  cosf(theta);
            const float z = -sinf(theta) * sinf(phi);

            const glm::vec3 dir(x, y, z);   // unit direction
            Vertex& v   = m->vertices[t];
            v.position  = dir * radius;
            v.normal    = dir;
            v.uv        = glm::vec2(phi / (2.0f * PI), theta / PI);
            ++t;
        }
    }

    // Two poles (north then south) — indices (height-2)*width and +1.
    m->vertices[t].position = glm::vec3(0.0f,  1.0f, 0.0f) * radius;
    m->vertices[t].normal   = glm::vec3(0.0f,  1.0f, 0.0f);
    m->vertices[t].uv       = glm::vec2(0.5f, 0.0f);
    ++t;
    m->vertices[t].position = glm::vec3(0.0f, -1.0f, 0.0f) * radius;
    m->vertices[t].normal   = glm::vec3(0.0f, -1.0f, 0.0f);
    m->vertices[t].uv       = glm::vec2(0.5f, 1.0f);
    ++t;

    auto push = [&](int a, int b, int c)
    {
        m->indices.push_back(static_cast<uint32_t>(a));
        m->indices.push_back(static_cast<uint32_t>(b));
        m->indices.push_back(static_cast<uint32_t>(c));
    };

    for (int j = 0; j < height - 3; ++j)
    {
        for (int i = 0; i < width - 1; ++i)
        {
            push(j * width + i,       (j + 1) * width + (i + 1), j * width + (i + 1));
            push(j * width + i,       (j + 1) * width + i,       (j + 1) * width + (i + 1));
        }
    }
    for (int i = 0; i < width - 1; ++i)
    {
        push((height - 2) * width,     i,                         i + 1);
        push((height - 2) * width + 1, (height - 3) * width + (i + 1), (height - 3) * width + i);
    }

    return m;
}

// ---------------------------------------------------------------------------
// GenerateCube — 6 faces, 4 verts each (per-face normals, no sharing).
// ---------------------------------------------------------------------------
UMesh* UMesh::GenerateCube(const glm::vec3& h)
{
    UMesh* m = new UMesh();

    struct Face { glm::vec3 n, u, v; };
    const Face faces[6] = {
        { { 1, 0, 0}, { 0, 0, 1}, {0, 1,  0} }, // +X
        { {-1, 0, 0}, { 0, 0,-1}, {0, 1,  0} }, // -X
        { { 0, 1, 0}, { 1, 0, 0}, {0, 0,  1} }, // +Y
        { { 0,-1, 0}, { 1, 0, 0}, {0, 0, -1} }, // -Y
        { { 0, 0, 1}, {-1, 0, 0}, {0, 1,  0} }, // +Z
        { { 0, 0,-1}, { 1, 0, 0}, {0, 1,  0} }, // -Z
    };

    uint32_t base = 0;
    for (const Face& f : faces)
    {
        const glm::vec3 c = f.n * h;   // face center
        const glm::vec3 U = f.u * h;   // half-edge along u
        const glm::vec3 V = f.v * h;   // half-edge along v

        m->vertices.push_back({ c - U - V, f.n, {0.0f, 0.0f} });
        m->vertices.push_back({ c + U - V, f.n, {1.0f, 0.0f} });
        m->vertices.push_back({ c + U + V, f.n, {1.0f, 1.0f} });
        m->vertices.push_back({ c - U + V, f.n, {0.0f, 1.0f} });

        m->indices.push_back(base + 0); m->indices.push_back(base + 1); m->indices.push_back(base + 2);
        m->indices.push_back(base + 0); m->indices.push_back(base + 2); m->indices.push_back(base + 3);
        base += 4;
    }
    return m;
}

// ---------------------------------------------------------------------------
// GeneratePlane — horizontal quad on the XZ plane, normal +Y, centered.
//   size = full extent in (x, z); UV spans [0,1].
// ---------------------------------------------------------------------------
UMesh* UMesh::GeneratePlane(const glm::vec2& size)
{
    UMesh* m = new UMesh();
    const float sx = size.x * 0.5f;
    const float sz = size.y * 0.5f;
    const glm::vec3 n(0.0f, 1.0f, 0.0f);

    m->vertices = {
        { {-sx, 0.0f, -sz}, n, {0.0f, 0.0f} },
        { { sx, 0.0f, -sz}, n, {1.0f, 0.0f} },
        { { sx, 0.0f,  sz}, n, {1.0f, 1.0f} },
        { {-sx, 0.0f,  sz}, n, {0.0f, 1.0f} },
    };
    m->indices = { 0, 1, 2, 0, 2, 3 };
    return m;
}

// ---------------------------------------------------------------------------
// Möller-Trumbore ray-triangle intersection (local space).
// ---------------------------------------------------------------------------
static bool rayTriangle(const glm::vec3& ro, const glm::vec3& rd,
                        const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                        float& t, float& u, float& v)
{
    const glm::vec3 e1 = v1 - v0;
    const glm::vec3 e2 = v2 - v0;
    const glm::vec3 p  = glm::cross(rd, e2);
    const float det    = glm::dot(e1, p);
    if (fabsf(det) < 1e-8f) return false;

    const float inv = 1.0f / det;
    const glm::vec3 s = ro - v0;
    u = glm::dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f) return false;

    const glm::vec3 q = glm::cross(s, e1);
    v = glm::dot(rd, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;

    t = glm::dot(e2, q) * inv;
    return t > 1e-4f;   // avoid self-intersection
}

bool UMesh::intersect(const URay& ray, const glm::mat4& worldMat,
                      float& outT, int& outTri, float& outU, float& outV) const
{
    const glm::mat4 inv = glm::inverse(worldMat);
    const glm::vec3 ro  = glm::vec3(inv * glm::vec4(ray.origin,    1.0f));
    const glm::vec3 rd  = glm::vec3(inv * glm::vec4(ray.direction, 0.0f));

    // Accelerated path: traverse the BVH (mesh-local space) when built.
    if (bvh && !bvh->empty())
    {
        const URay localRay(ro, rd);
        return bvh->Intersect(localRay, *this, outT, outTri, outU, outV);
    }

    float closest = std::numeric_limits<float>::max();
    bool  hit     = false;

    const int nTri = triangleCount();
    for (int tri = 0; tri < nTri; ++tri)
    {
        const glm::vec3& v0 = vertices[indices[3 * tri + 0]].position;
        const glm::vec3& v1 = vertices[indices[3 * tri + 1]].position;
        const glm::vec3& v2 = vertices[indices[3 * tri + 2]].position;

        float t, u, v;
        if (rayTriangle(ro, rd, v0, v1, v2, t, u, v) && t < closest)
        {
            closest = t;
            outTri  = tri;
            outU    = u;
            outV    = v;
            hit     = true;
        }
    }

    outT = closest;
    return hit;
}

glm::vec3 UMesh::getNormalAt(int triId, float u, float v) const
{
    const glm::vec3& n0 = vertices[indices[3 * triId + 0]].normal;
    const glm::vec3& n1 = vertices[indices[3 * triId + 1]].normal;
    const glm::vec3& n2 = vertices[indices[3 * triId + 2]].normal;
    return glm::normalize((1.0f - u - v) * n0 + u * n1 + v * n2);
}

glm::vec2 UMesh::getUVAt(int triId, float u, float v) const
{
    const glm::vec2& t0 = vertices[indices[3 * triId + 0]].uv;
    const glm::vec2& t1 = vertices[indices[3 * triId + 1]].uv;
    const glm::vec2& t2 = vertices[indices[3 * triId + 2]].uv;
    return (1.0f - u - v) * t0 + u * t1 + v * t2;
}
