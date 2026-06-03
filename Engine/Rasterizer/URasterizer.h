#pragma once
#include <glm/glm.hpp>

class UMesh;
struct FTransform;
class UFrameBuffer;
class UGBuffer;
struct Material;

// CPU shading model (HW6 Q1-Q3). Flat = per-triangle normal shaded at the
// centroid; Gouraud = per-vertex shade, color interpolated; Phong = per-pixel
// interpolated normal shaded per fragment.
enum class EShadingModel { Flat, Gouraud, Phong };

// Lighting inputs for the CPU shaded raster (single point light + ambient,
// Blinn-Phong). Matches the HW6 formula L = ka*Ia + kd*I*max(0,n.l) + ks*I*max(0,n.h)^p.
struct FShadeParams
{
    glm::vec3 lightPos   = glm::vec3(0.0f);
    glm::vec3 lightColor = glm::vec3(1.0f);   // I  (color * intensity)
    glm::vec3 ambient    = glm::vec3(0.2f);   // Ia (white ambient)
    glm::vec3 eye        = glm::vec3(0.0f);
};

// ---------------------------------------------------------------------------
// URasterizer — general-purpose software rasterizer (NOT Q1-specific).
// Reusable building blocks: an edge-function triangle fill with depth test.
// The transform pipeline lives in FTransform. Q1, the hybrid renderer's
// primary-visibility pass, and the editor viewport all call these same funcs.
// ---------------------------------------------------------------------------
class URasterizer
{
public:
    // ---- clip / cull stage (CPU software-raster front end, page 10) ----
    // nearClip is ACCURACY (a triangle crossing the camera plane has a vertex
    // with w >= 0 -> the perspective divide flips it; clip in clip space first).
    // frustumCull / backfaceCull are PERFORMANCE only -- they must not change the
    // rendered image (depth handles occlusion).
    bool nearClip     = true;
    bool frustumCull  = true;
    bool backfaceCull = false;

    struct CullStats { int trianglesIn = 0, frustumCulled = 0, backfaceCulled = 0, rasterized = 0; };
    mutable CullStats stats;
    void ResetStats() const { stats = CullStats{}; }

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
    // cx0..cy1 restrict writes to a tile (for multithreaded fill -- disjoint
    // tiles need no sync). countStats=false skips the shared CullStats counters
    // (they would race / double-count across tiles).
    void DrawMeshGBuffer(const UMesh& mesh, const FTransform& xf,
                         const glm::vec3& albedo, UGBuffer& gb,
                         int cx0 = 0, int cy0 = 0, int cx1 = 0x7fffffff, int cy1 = 0x7fffffff,
                         bool countStats = true) const;

    // ---- CPU shaded raster (HW6 Q1-Q3) ----
    // Rasterize a mesh shaded with the given model + Blinn-Phong, writing the
    // gamma-corrected (gamma 2.2) lit color into fb (depth-tested). Uses the
    // mesh's per-triangle material slot when `mat` is null.
    void DrawMeshShaded(const UMesh& mesh, const FTransform& xf, const Material* mat,
                        const FShadeParams& sp, EShadingModel model, UFrameBuffer& fb) const;
};
