#pragma once
#include <vector>
#include <glm/glm.hpp>

class UGBuffer;
class ACamera;

// ---------------------------------------------------------------------------
// UHybridPass (page 4, Phase A) — GPU shadow + shading pass of the hybrid
// renderer. It consumes the CPU rasterizer's G-buffer (uploaded as float
// textures) and, per pixel, casts one shadow ray toward the light against the
// scene's world-space triangles (Moller-Trumbore), then shades with the
// engine's Blinn-Phong model (ambient = ka*sky(N), diffuse + specular * shadow).
//
// Uses a #version 330 FRAGMENT shader on a fullscreen quad -- NO compute shader
// / GL 4.3 -- so it runs on the grading machine's GL 3.3 baseline.
// ---------------------------------------------------------------------------
class UHybridPass
{
public:
    void Init();

    // Flat world-space triangle positions (3 vertices per triangle) used for
    // shadow-ray occlusion. Upload once per scene (or when geometry changes).
    void UploadSceneTriangles(const std::vector<glm::vec3>& worldTriVerts);

    // Upload the per-frame G-buffer (worldPos / normal / albedo / depth).
    void UploadGBuffer(const UGBuffer& gb);

    // Draw the fullscreen shaded result. Single-light convenience overload
    // (delegates to the multi-light version with one light).
    void Render(const ACamera& cam, const glm::vec3& lightPos,
                const glm::vec3& lightColor, int width, int height) const;
    // Multi-light: each light casts its own shadow ray and contributes
    // Blinn-Phong direct light (ambient added once). Up to MAX_LIGHTS (8).
    void Render(const ACamera& cam, const std::vector<glm::vec3>& lightPos,
                const std::vector<glm::vec3>& lightColor, int width, int height) const;

    // Equirectangular HDRI sky texture (0 = none -> procedural gradient).
    void SetSky(unsigned int tex) { skyTex_ = tex; }
    // Environment light: GI sample count (0=off), tint, and sky gradient colors.
    void SetGI(int samples, const glm::vec3& tint, const glm::vec3& horizon,
               const glm::vec3& zenith, float skyExp)
    { giSamples_ = samples; envTint_ = tint; skyHorizon_ = horizon; skyZenith_ = zenith; skyExp_ = skyExp; }

    void Cleanup();
    bool ready() const { return prog_ != 0; }

private:
    unsigned int prog_ = 0, vao_ = 0, vbo_ = 0;
    unsigned int texWP_ = 0, texN_ = 0, texAlb_ = 0, texDepth_ = 0;
    unsigned int tbo_ = 0, triTex_ = 0;
    unsigned int nodeTbo_ = 0, nodeTex_ = 0;   // BVH nodes
    unsigned int idxTbo_  = 0, idxTex_  = 0;   // BVH leaf -> triangle index
    unsigned int skyTex_  = 0;                 // equirect HDRI (0 = gradient)
    int       giSamples_  = 0;
    glm::vec3 envTint_     = glm::vec3(1.0f);
    glm::vec3 skyHorizon_  = glm::vec3(0.10f, 0.12f, 0.16f);
    glm::vec3 skyZenith_   = glm::vec3(0.40f, 0.55f, 0.80f);
    float     skyExp_      = 1.0f;
    int numTris_  = 0;
    int numNodes_ = 0;
    int gw_ = 0, gh_ = 0;
};
