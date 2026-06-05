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

    void Clear(const glm::vec3& bg, float farDepth = 1.0f)   // 'far' is a Windows macro
    {
        std::fill(color.begin(), color.end(), bg);
        std::fill(depth.begin(), depth.end(), farDepth);
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

    // Grayscale depth visualization (REQUIRED Q1 deliverable: demonstrates the
    // depth buffer / occlusion -- the white silhouette alone can't). Background
    // (depth == farDepth) is black; the written depth range is normalized so the
    // occlusion gradient is visible, with the NEAREST surface the BRIGHTEST
    // (sphere center brightest). Interleaved RGB, same layout as ToOutputImage.
    void ToDepthImage(std::vector<float>& out, float farDepth = 1.0f) const
    {
        float mn = farDepth, mx = 0.0f;
        bool any = false;
        for (float d : depth)
            if (d < farDepth) { mn = std::min(mn, d); mx = std::max(mx, d); any = true; }
        const float range = (any && mx > mn) ? (mx - mn) : 1.0f;

        out.resize(static_cast<size_t>(nx) * ny * 3);
        for (int i = 0; i < nx * ny; ++i)
        {
            float g = 0.0f;                              // background -> black
            if (depth[i] < farDepth)
            {
                const float t = (depth[i] - mn) / range; // 0 = nearest, 1 = farthest
                g = 1.0f - 0.85f * t;                     // nearest brightest
            }
            out[3 * i + 0] = out[3 * i + 1] = out[3 * i + 2] = g;
        }
    }
};
