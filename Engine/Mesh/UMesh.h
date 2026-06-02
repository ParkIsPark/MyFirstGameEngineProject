#pragma once
#include <vector>
#include <cstdint>
#include <memory>
#include <glm/glm.hpp>

#include "Vertex.h"
#include "Material.h"   // per-asset default material (moved out of deleted USurface)

struct URay;
class  BVH;

// ---------------------------------------------------------------------------
// UMesh — triangle mesh asset (Unreal StaticMesh analogue).
//
// Owns CPU geometry (vertices + indices), a default Material, the analytic
// generators that replace SphereSurface / CubeSurface / PlaneSurface, and a
// brute-force ray-triangle intersect used for editor picking / queries
// (NOT a renderer — the CPU ray tracer is removed; GPU RT lives in GLSL).
//
// Shared by multiple actors through UMeshComponent (added in a later stage).
// ---------------------------------------------------------------------------
class UMesh
{
public:
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;   // 3 indices per triangle
    Material              material;   // per-asset default material

    UMesh();
    ~UMesh();                        // both out-of-line for unique_ptr<BVH> (incomplete)

    int triangleCount() const { return static_cast<int>(indices.size()) / 3; }

    // Optional acceleration structure. BuildBVH() builds it; intersect() then
    // traverses it instead of brute-forcing all triangles.
    std::unique_ptr<BVH> bvh;
    void BuildBVH();

    // -------- static generators (analytic-shape replacements) --------
    // GenerateSphere reproduces the EXACT vertex order / index rules of the
    // course-provided sphere_scene.cpp so the rasterizer output matches the
    // reference image pixel-for-pixel:
    //   vertices = (segH-2)*segW + 2     (default 32x16 -> 450)
    //   triangles = (segH-2)*(segW-1)*2  (default 32x16 -> 868)
    static UMesh* GenerateSphere(float radius, int segW = 32, int segH = 16);
    static UMesh* GenerateCube(const glm::vec3& halfExtents);
    static UMesh* GeneratePlane(const glm::vec2& size);

    // -------- CPU geometric query (picking; not a render path) --------
    // Closest ray-triangle hit. The ray is transformed into mesh-local space
    // by inverse(worldMat); pass identity for an already-local ray.
    // On hit, returns t (along the ORIGINAL ray), triangle index, and the
    // barycentric (u, v) of that triangle.
    bool intersect(const URay& ray, const glm::mat4& worldMat,
                   float& outT, int& outTri, float& outU, float& outV) const;

    // Interpolated attributes at a (triangle, barycentric) location.
    glm::vec3 getNormalAt(int triId, float u, float v) const;
    glm::vec2 getUVAt(int triId, float u, float v) const;
};
