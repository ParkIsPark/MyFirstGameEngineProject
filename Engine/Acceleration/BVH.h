#pragma once
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

class UMesh;
struct URay;

// 32-byte node (GPU-friendly std430 layout for a later TBO/SSBO upload).
//   rightOrTriCount > 0  -> leaf  (leftOrTriStart = first index into triIndices,
//                                   rightOrTriCount = triangle count)
//   rightOrTriCount <= 0 -> inner (leftOrTriStart = left child node index,
//                                   right child node index = -rightOrTriCount)
struct BVHNode
{
    glm::vec3 bbMin;            int leftOrTriStart;
    glm::vec3 bbMax;            int rightOrTriCount;
};

// ---------------------------------------------------------------------------
// BVH (page 8) — bounding volume hierarchy over a UMesh's triangles, built in
// MESH-LOCAL space (median split on the longest axis). Stack-based slab
// traversal (no recursion) so it ports 1:1 to GLSL. Drops ray-triangle cost
// from O(N) to ~O(log N); the main consumers are UMesh::intersect (editor
// picking / geometry queries) and the GPU ray tracer.
// ---------------------------------------------------------------------------
class BVH
{
public:
    std::vector<BVHNode>  nodes;
    std::vector<uint32_t> triIndices;     // leaf-ordered triangle indices

    void Build(const UMesh& mesh, int leafSize = 4);

    // Closest hit of a MESH-LOCAL ray. Returns t along the ray + triangle index
    // + barycentric (u,v). Same result as a brute-force loop.
    bool Intersect(const URay& ray, const UMesh& mesh,
                   float& outT, int& outTri, float& outU, float& outV) const;

    int  Depth() const;
    bool empty() const { return nodes.empty(); }

private:
    int buildRange(const UMesh& mesh, const std::vector<glm::vec3>& centroids,
                   int start, int count, int leafSize);
};
