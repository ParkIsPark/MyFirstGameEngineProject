#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
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
                     const std::vector<float>& mirrors = {},          // per-instance km (0=matte)
                     const std::vector<const Material*>& mats = {});  // per-instance material (diffuse texture)
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
    // Environment light: GI sample count (0=off), tint, and sky gradient colors.
    void SetGI(int samples, const glm::vec3& tint, const glm::vec3& horizon,
               const glm::vec3& zenith, float skyExp, int bounces = 1)
    { giSamples_ = samples; envTint_ = tint; skyHorizon_ = horizon; skyZenith_ = zenith; skyExp_ = skyExp; giBounces_ = bounces; }
    // Render-quality knobs: global reflection multiplier + RT Phong exponent.
    void SetQuality(float reflMul, float shininess) { reflMul_ = reflMul; shininess_ = shininess; }
    // Soft shadows: samples per light (1 = hard) + penumbra radius.
    void SetShadow(int samples, float softness) { shadowSamples_ = samples; shadowSoftness_ = softness; }
    void Cleanup();
    bool ready() const { return prog_ != 0; }

private:
    // Generic TBO (re)upload helpers (each owns a buffer + buffer-texture).
    void uploadBufferTex(unsigned int& tbo, unsigned int& tex, unsigned int fmt,
                         const void* data, size_t bytes);

    // Per-mesh BLAS, built ONCE in mesh-local space and cached by mesh identity.
    // Holds GPU-ready texel arrays so the concatenated upload is a plain append.
    struct MeshBlas {
        std::vector<glm::vec4> nodeTexels;   // 2 RGBA32F per BVH node
        std::vector<float>     triIdx;       // leaf-order, MESH-LOCAL triangle indices
        std::vector<glm::vec4> triTexels;    // 6 per triangle (local pos/normal, uv in .w)
        int       numTris = 0, numNodes = 0;
        glm::vec3 bbMin = glm::vec3(0.0f), bbMax = glm::vec3(0.0f);
    };
    struct MeshOff { int nodeOff = 0, triOff = 0, idxOff = 0; };  // bases into the concat buffers
    const MeshBlas& ensureBlas(const UMesh& mesh);                // build/cache one mesh's BLAS

    unsigned int prog_  = 0;
    unsigned int vao_   = 0;
    unsigned int vbo_   = 0;
    unsigned int tbo_   = 0;   // uTris: concatenated local triangle texels (6/tri)
    unsigned int tex_   = 0;
    unsigned int nodeTbo_ = 0, nodeTex_ = 0;   // uNodes: concatenated BLAS nodes (2 texels/node)
    unsigned int idxTbo_  = 0, idxTex_  = 0;   // uTriIdx: concat leaf -> local triangle index (R32F)
    unsigned int instTbo_ = 0, instTex_ = 0;   // uInstances: 7 texels/instance
    int          numTris_  = 0;
    int          numNodes_ = 0;
    int          numInstances_ = 0;

    // Two-level caches. blas_ is per-mesh (built once). The concatenated BLAS upload
    // + texture array is rebuilt only when blasSig_ (the mesh set + textures) changes;
    // the small instance buffer is rebuilt every call (cheap) so moving objects is fast.
    std::unordered_map<const UMesh*, MeshBlas> blas_;
    std::unordered_map<const UMesh*, MeshOff>  meshOff_;
    size_t           blasSig_ = 0;
    bool             blasUploaded_ = false;
    std::vector<int> instLayer_;     // per-instance diffuse-texture layer (-1 = none)
    Material     mat_;         // material (ks/shininess) of the uploaded mesh
    std::vector<glm::vec3> lightPos_   = { glm::vec3(6.0f, 8.0f, 2.0f) };
    std::vector<glm::vec3> lightColor_ = { glm::vec3(1.0f) };
    unsigned int           skyTex_     = 0;     // equirect HDRI (0 = gradient)
    unsigned int           texArr_     = 0;     // GL_TEXTURE_2D_ARRAY of diffuse textures
    int                    texLayers_  = 0;     // layer count (0 = untextured world)
    int                    giSamples_  = 0;     // hemisphere GI samples (0 = flat ambient)
    int                    giBounces_  = 1;     // GI path bounces
    glm::vec3              envTint_     = glm::vec3(1.0f);
    glm::vec3              skyHorizon_  = glm::vec3(0.10f, 0.12f, 0.16f);
    glm::vec3              skyZenith_   = glm::vec3(0.40f, 0.55f, 0.80f);
    float                  skyExp_      = 1.0f;
    float                  reflMul_     = 1.0f;
    float                  shininess_   = 32.0f;
    int                    shadowSamples_  = 1;
    float                  shadowSoftness_ = 0.0f;
};
