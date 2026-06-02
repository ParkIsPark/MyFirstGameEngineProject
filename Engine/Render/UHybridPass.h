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

    // Draw the fullscreen shaded result. lightDir is the (normalized) direction
    // TOWARD the light for a distant light; lightPos is used for local lights.
    void Render(const ACamera& cam, const glm::vec3& lightPos,
                const glm::vec3& lightColor, int width, int height) const;

    void Cleanup();
    bool ready() const { return prog_ != 0; }

private:
    unsigned int prog_ = 0, vao_ = 0, vbo_ = 0;
    unsigned int texWP_ = 0, texN_ = 0, texAlb_ = 0, texDepth_ = 0;
    unsigned int tbo_ = 0, triTex_ = 0;
    int numTris_ = 0;
    int gw_ = 0, gh_ = 0;
};
