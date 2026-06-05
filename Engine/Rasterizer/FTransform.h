#pragma once
#include <glm/glm.hpp>

class ACamera;

// ---------------------------------------------------------------------------
// FTransform — model -> view -> proj -> viewport stack for the software
// rasterizer. All matrices are built directly (no glm::perspective / lookAt)
// to honor the course FCG (Shirley) convention: n/f are SIGNED z (negative),
// and the projection bottom row is [0 0 1 0] (so clip.w = z_eye, negative for
// visible points).
// ---------------------------------------------------------------------------
struct FTransform
{
    glm::mat4 model    = glm::mat4(1.0f);
    glm::mat4 view     = glm::mat4(1.0f);
    glm::mat4 proj     = glm::mat4(1.0f);
    glm::mat4 viewport = glm::mat4(1.0f);

    // ACamera (u, v, w, eye) -> world->eye matrix. Q1 (e=0, axis basis) = identity.
    static glm::mat4 MakeView(const ACamera& cam);
    // FCG asymmetric perspective. (l,r,b,t) frustum, (n,f) signed-negative z.
    static glm::mat4 MakeProjFCG(float l, float r, float b, float t, float n, float f);
    // NDC [-1,1] -> screen [0,nx] x [0,ny], z [-1,1] -> [0,1]. Origin bottom-left.
    static glm::mat4 MakeViewport(int nx, int ny);

    // object-space position -> clip space (applies model, view, proj).
    glm::vec4 ToClip(const glm::vec3& objPos) const
    {
        return proj * view * model * glm::vec4(objPos, 1.0f);
    }

    // clip -> perspective divide -> viewport -> screen (x, y, depth).
    // NOTE: w may be negative (FCG); Q1 spheres are fully inside the frustum so
    // a plain divide is correct. Near-plane clipping is a later stage.
    glm::vec3 ToScreen(const glm::vec4& clip) const
    {
        const glm::vec3 ndc = glm::vec3(clip.x, clip.y, clip.z) / clip.w;
        const glm::vec4 s   = viewport * glm::vec4(ndc, 1.0f);
        return glm::vec3(s.x, s.y, s.z);
    }
};
