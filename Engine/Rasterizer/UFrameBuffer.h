#pragma once
#include <vector>
#include <algorithm>
#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// UFrameBuffer — color (RGB float) + depth buffer for the software rasterizer.
// Origin bottom-left; pixel (x, y) at index y*nx + x (matches scene.outputImage).
// Depth is screen-space z in [0,1]; smaller = nearer; initialized to 1.0 (far).
// ---------------------------------------------------------------------------
class UFrameBuffer
{
public:
    int nx = 0, ny = 0;
    std::vector<glm::vec3> color;
    std::vector<float>     depth;

    void Init(int w, int h)
    {
        nx = w; ny = h;
        color.assign(static_cast<size_t>(nx) * ny, glm::vec3(0.0f));
        depth.assign(static_cast<size_t>(nx) * ny, 1.0f);
    }

    void Clear(const glm::vec3& bg, float far = 1.0f)
    {
        std::fill(color.begin(), color.end(), bg);
        std::fill(depth.begin(), depth.end(), far);
    }

    // Depth test (smaller = nearer). On pass, writes color + depth.
    bool TestAndSet(int x, int y, float z, const glm::vec3& c)
    {
        const int idx = y * nx + x;
        if (z < depth[idx]) { depth[idx] = z; color[idx] = c; return true; }
        return false;
    }

    // Flatten to interleaved RGB floats (scene.outputImage / glDrawPixels convention).
    void ToOutputImage(std::vector<float>& out) const
    {
        out.resize(static_cast<size_t>(nx) * ny * 3);
        for (int i = 0; i < nx * ny; ++i)
        {
            out[3 * i + 0] = color[i].r;
            out[3 * i + 1] = color[i].g;
            out[3 * i + 2] = color[i].b;
        }
    }
};
