#pragma once
#include <vector>
#include <algorithm>
#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// UGBuffer — deferred geometry buffer written by the CPU rasterizer's primary
// visibility pass and consumed by the GPU shadow/reflection pass (hybrid
// renderer, page 4). Per pixel: world position, world normal, albedo, depth.
// Origin bottom-left; pixel (x,y) at index y*nx + x (matches UFrameBuffer /
// scene.outputImage). depth in [0,1], 1.0 = background (no surface).
// ---------------------------------------------------------------------------
class UGBuffer
{
public:
    int nx = 0, ny = 0;
    std::vector<glm::vec3> worldPos;
    std::vector<glm::vec3> normal;
    std::vector<glm::vec3> albedo;
    std::vector<float>     depth;

    void Init(int w, int h)
    {
        nx = w; ny = h;
        const size_t n = static_cast<size_t>(nx) * ny;
        worldPos.assign(n, glm::vec3(0.0f));
        normal.assign  (n, glm::vec3(0.0f));
        albedo.assign  (n, glm::vec3(0.0f));
        depth.assign   (n, 1.0f);
    }

    void Clear()
    {
        std::fill(worldPos.begin(), worldPos.end(), glm::vec3(0.0f));
        std::fill(normal.begin(),   normal.end(),   glm::vec3(0.0f));
        std::fill(albedo.begin(),   albedo.end(),   glm::vec3(0.0f));
        std::fill(depth.begin(),    depth.end(),    1.0f);
    }

    // Depth test (smaller = nearer). On pass, writes all surface attributes.
    bool TestAndSet(int x, int y, float z, const glm::vec3& wp,
                    const glm::vec3& n, const glm::vec3& alb)
    {
        const int idx = y * nx + x;
        if (z < depth[idx])
        {
            depth[idx]    = z;
            worldPos[idx] = wp;
            normal[idx]   = n;
            albedo[idx]   = alb;
            return true;
        }
        return false;
    }
};
