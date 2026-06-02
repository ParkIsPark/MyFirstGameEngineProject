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
                     const glm::vec3& lightPos, const glm::vec3& lightColor);
    void RenderFrame(const ACamera& cam, int width, int height) const;
    void Cleanup();
    bool ready() const { return prog_ != 0; }

private:
    void bakeMesh(const UMesh& mesh, const glm::mat4& model,
                  const glm::vec3& albedo, std::vector<glm::vec4>& out);
    void uploadTexels(const std::vector<glm::vec4>& texels);

    unsigned int prog_  = 0;
    unsigned int vao_   = 0;
    unsigned int vbo_   = 0;
    unsigned int tbo_   = 0;   // buffer object holding triangle texels
    unsigned int tex_   = 0;   // GL_TEXTURE_BUFFER view onto tbo_
    int          numTris_ = 0;
    Material     mat_;         // material (ks/shininess) of the uploaded mesh
    glm::vec3    lightPos_   = glm::vec3(6.0f, 8.0f, 2.0f);
    glm::vec3    lightColor_ = glm::vec3(1.0f);
};
