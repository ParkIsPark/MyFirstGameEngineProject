#pragma once
#include <glm/glm.hpp>

class UMesh;
struct FTransform;
class UFrameBuffer;
class UGBuffer;

// ---------------------------------------------------------------------------
// URasterizer — general-purpose software rasterizer (NOT Q1-specific).
// Reusable building blocks: an edge-function triangle fill with depth test.
// The transform pipeline lives in FTransform. Q1, the hybrid renderer's
// primary-visibility pass, and the editor viewport all call these same funcs.
// ---------------------------------------------------------------------------
class URasterizer
{
public:
    // 2D edge function (twice the signed triangle area abp). Sign = winding.
    static float EdgeFunction(const glm::vec2& a, const glm::vec2& b, const glm::vec2& p);

    // Rasterize one SCREEN-space triangle: s*.xy = pixel coords, s*.z = depth[0,1].
    void RasterizeTriangle(const glm::vec3& s0, const glm::vec3& s1, const glm::vec3& s2,
                           const glm::vec3& color, UFrameBuffer& fb) const;

    // Transform an object-space triangle through xf, then rasterize.
    void DrawTriangle(const glm::vec3& o0, const glm::vec3& o1, const glm::vec3& o2,
                      const FTransform& xf, const glm::vec3& color, UFrameBuffer& fb) const;

    // Transform + rasterize every triangle of a mesh.
    void DrawMesh(const UMesh& mesh, const FTransform& xf,
                  const glm::vec3& color, UFrameBuffer& fb) const;

    // ---- G-buffer (deferred primary visibility for the hybrid renderer) ----
    // Rasterize a mesh writing per-pixel world position / world normal /
    // albedo with perspective-correct interpolation (1/w weighting). albedo is
    // the mesh material's kd unless overridden by `albedo`.
    void DrawMeshGBuffer(const UMesh& mesh, const FTransform& xf,
                         UGBuffer& gb) const;
    void DrawMeshGBuffer(const UMesh& mesh, const FTransform& xf,
                         const glm::vec3& albedo, UGBuffer& gb) const;
};
