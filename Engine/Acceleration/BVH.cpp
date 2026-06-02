#include "BVH.h"
#include "UMesh.h"
#include "URay.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace
{
    inline glm::vec3 triV(const UMesh& m, int tri, int k)
    {
        return m.vertices[m.indices[3 * tri + k]].position;
    }
    inline glm::vec3 triCentroid(const UMesh& m, int tri)
    {
        return (triV(m, tri, 0) + triV(m, tri, 1) + triV(m, tri, 2)) / 3.0f;
    }

    // Moller-Trumbore in mesh-local space.
    bool rayTri(const glm::vec3& ro, const glm::vec3& rd,
                const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                float& t, float& u, float& v)
    {
        const glm::vec3 e1 = v1 - v0, e2 = v2 - v0;
        const glm::vec3 p = glm::cross(rd, e2);
        const float det = glm::dot(e1, p);
        if (std::fabs(det) < 1e-8f) return false;
        const float inv = 1.0f / det;
        const glm::vec3 s = ro - v0;
        u = glm::dot(s, p) * inv;            if (u < 0.0f || u > 1.0f) return false;
        const glm::vec3 q = glm::cross(s, e1);
        v = glm::dot(rd, q) * inv;           if (v < 0.0f || u + v > 1.0f) return false;
        t = glm::dot(e2, q) * inv;           return t > 1e-4f;
    }

    // Slab test: does the ray meet the AABB within [0, tMax]? Per-axis so an
    // axis-parallel ray (direction component 0) is handled without 0*inf = NaN.
    bool rayAABB(const glm::vec3& ro, const glm::vec3& rd,
                 const glm::vec3& mn, const glm::vec3& mx, float tMax)
    {
        float tmin = 0.0f, tmax = tMax;
        for (int i = 0; i < 3; ++i)
        {
            if (std::fabs(rd[i]) < 1e-9f)
            {
                if (ro[i] < mn[i] || ro[i] > mx[i]) return false;   // parallel & outside the slab
            }
            else
            {
                const float inv = 1.0f / rd[i];
                float t0 = (mn[i] - ro[i]) * inv;
                float t1 = (mx[i] - ro[i]) * inv;
                if (t0 > t1) std::swap(t0, t1);
                tmin = std::max(tmin, t0);
                tmax = std::min(tmax, t1);
                if (tmin > tmax) return false;
            }
        }
        return true;
    }
}

void BVH::Build(const UMesh& mesh, int leafSize)
{
    nodes.clear();
    triIndices.clear();
    const int nTri = mesh.triangleCount();
    if (nTri == 0) return;

    triIndices.resize(nTri);
    for (int i = 0; i < nTri; ++i) triIndices[i] = static_cast<uint32_t>(i);

    std::vector<glm::vec3> centroids(nTri);
    for (int i = 0; i < nTri; ++i) centroids[i] = triCentroid(mesh, i);

    nodes.reserve(static_cast<size_t>(nTri) * 2);
    buildRange(mesh, centroids, 0, nTri, std::max(1, leafSize));
}

int BVH::buildRange(const UMesh& mesh, const std::vector<glm::vec3>& centroids,
                    int start, int count, int leafSize)
{
    const int nodeIdx = static_cast<int>(nodes.size());
    nodes.push_back(BVHNode{});

    glm::vec3 mn(FLT_MAX), mx(-FLT_MAX);
    for (int i = 0; i < count; ++i)
    {
        const int tri = static_cast<int>(triIndices[start + i]);
        for (int k = 0; k < 3; ++k) { const glm::vec3 p = triV(mesh, tri, k); mn = glm::min(mn, p); mx = glm::max(mx, p); }
    }

    if (count <= leafSize)
    {
        nodes[nodeIdx].bbMin = mn; nodes[nodeIdx].bbMax = mx;
        nodes[nodeIdx].leftOrTriStart  = start;
        nodes[nodeIdx].rightOrTriCount = count;            // > 0 -> leaf
        return nodeIdx;
    }

    const glm::vec3 ext = mx - mn;
    const int axis = (ext.x > ext.y && ext.x > ext.z) ? 0 : (ext.y > ext.z ? 1 : 2);
    const int mid = start + count / 2;
    std::nth_element(triIndices.begin() + start, triIndices.begin() + mid,
                     triIndices.begin() + start + count,
                     [&](uint32_t a, uint32_t b) { return centroids[a][axis] < centroids[b][axis]; });

    const int leftChild  = buildRange(mesh, centroids, start, mid - start, leafSize);
    const int rightChild = buildRange(mesh, centroids, mid, start + count - mid, leafSize);

    nodes[nodeIdx].bbMin = mn; nodes[nodeIdx].bbMax = mx;
    nodes[nodeIdx].leftOrTriStart  = leftChild;
    nodes[nodeIdx].rightOrTriCount = -rightChild;          // <= 0 -> inner, right = -value
    return nodeIdx;
}

bool BVH::Intersect(const URay& ray, const UMesh& mesh,
                    float& outT, int& outTri, float& outU, float& outV) const
{
    if (nodes.empty()) return false;

    int stack[64];
    int sp = 0;
    stack[sp++] = 0;

    float closest = FLT_MAX;
    int   hitTri  = -1;
    float hu = 0.0f, hv = 0.0f;

    while (sp > 0)
    {
        const BVHNode& n = nodes[stack[--sp]];
        if (!rayAABB(ray.origin, ray.direction, n.bbMin, n.bbMax, closest)) continue;

        if (n.rightOrTriCount > 0)                        // leaf
        {
            for (int i = 0; i < n.rightOrTriCount; ++i)
            {
                const int tri = static_cast<int>(triIndices[n.leftOrTriStart + i]);
                float t, u, v;
                if (rayTri(ray.origin, ray.direction, triV(mesh, tri, 0), triV(mesh, tri, 1), triV(mesh, tri, 2), t, u, v)
                    && t < closest)
                { closest = t; hitTri = tri; hu = u; hv = v; }
            }
        }
        else if (sp + 2 <= 64)                            // inner
        {
            stack[sp++] = n.leftOrTriStart;
            stack[sp++] = -n.rightOrTriCount;
        }
    }

    if (hitTri < 0) return false;
    outT = closest; outTri = hitTri; outU = hu; outV = hv;
    return true;
}

int BVH::Depth() const
{
    if (nodes.empty()) return 0;
    // iterative depth via (node, depth) stack
    int maxD = 0;
    std::vector<std::pair<int,int>> st{ { 0, 1 } };
    while (!st.empty())
    {
        auto [ni, d] = st.back(); st.pop_back();
        maxD = std::max(maxD, d);
        const BVHNode& n = nodes[ni];
        if (n.rightOrTriCount <= 0)
        {
            st.push_back({ n.leftOrTriStart,    d + 1 });
            st.push_back({ -n.rightOrTriCount,  d + 1 });
        }
    }
    return maxD;
}
