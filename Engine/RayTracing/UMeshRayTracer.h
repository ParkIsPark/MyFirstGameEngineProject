#pragma once
#include <glm/glm.hpp>
#include <vector>
#include "Material.h"

class UMesh;
class ACamera;

// ---------------------------------------------------------------------------
// UMeshRayTracer (Stage 4) — minimal GPU mesh ray tracer.
//
// A full-screen-quad fragment shader casts one camera ray per pixel and
// brute-force intersects a WORLD-SPACE triangle buffer (Moller-Trumbore in
// GLSL), then shades with one directional light + a sky gradient. Triangles are
// passed via a texture buffer object (samplerBuffer, #version 330 / GL 3.1+) so
// no GL 4.3 / SSBO support is assumed on the target machine. This replaces the
// legacy analytic-surface GPU path with a mesh-based one. BVH acceleration and
// the hybrid G-buffer path are later stages (pages 8 / 4).
// ---------------------------------------------------------------------------
class UMeshRayTracer
{
public:
    void Init();                                                    // compile shader + quad
    void UploadMesh(const UMesh& mesh, const glm::mat4& model);     // single mesh (demo)
    // Whole scene: bake every (mesh, model) with a per-instance albedo into one
    // triangle buffer, lit by a single point light. meshes/models/albedos are
    // parallel arrays. Used by the editor's GPU-RT play mode.
    void UploadWorld(const std::vector<const UMesh*>& meshes,
                     const std::vector<glm::mat4>&    models,
                     const std::vector<glm::vec3>&    albedos,
                     const glm::vec3& lightPos, const glm::vec3& lightColor,
                     const std::vector<float>& mirrors = {});   // per-instance km (0=matte)
    void RenderFrame(const ACamera& cam, int width, int height) const;
    // Update the light(s) without re-uploading geometry (lights are per-frame
    // uniforms, not baked into the triangle buffer) -- lets callers cache the
    // BVH/TBO upload across frames while still animating lights.
    void SetLight(const glm::vec3& pos, const glm::vec3& color)
    { lightPos_ = { pos }; lightColor_ = { color }; }
    // Multiple point lights (editor). Each gets its own shadow ray in the shader.
    void SetLights(const std::vector<glm::vec3>& pos, const std::vector<glm::vec3>& color)
    { lightPos_ = pos; lightColor_ = color; }
    // Equirectangular HDRI sky texture (0 = none -> procedural gradient).
    void SetSky(unsigned int tex) { skyTex_ = tex; }
    void Cleanup();
    bool ready() const { return prog_ != 0; }

private:
    void uploadTexels(const std::vector<glm::vec4>& texels);
    void uploadBVH(const class BVH& bvh);

    unsigned int prog_  = 0;
    unsigned int vao_   = 0;
    unsigned int vbo_   = 0;
    unsigned int tbo_   = 0;   // triangle texels (7/tri)
    unsigned int tex_   = 0;
    unsigned int nodeTbo_ = 0, nodeTex_ = 0;   // BVH nodes (2 texels/node)
    unsigned int idxTbo_  = 0, idxTex_  = 0;   // BVH leaf -> triangle index (R32F)
    int          numTris_  = 0;
    int          numNodes_ = 0;
    Material     mat_;         // material (ks/shininess) of the uploaded mesh
    std::vector<glm::vec3> lightPos_   = { glm::vec3(6.0f, 8.0f, 2.0f) };
    std::vector<glm::vec3> lightColor_ = { glm::vec3(1.0f) };
    unsigned int           skyTex_     = 0;     // equirect HDRI (0 = gradient)
};
