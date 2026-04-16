#pragma once
#include <vector>

// ---------------------------------------------------------------------------
// UPostProcessFilter
//
// Applies a chain of image-space effects to the CPU-rendered float image that
// lives in UScene::outputImage (row-major RGB, origin at bottom-left).
//
// Effects applied in order:
//   1. Exposure  — linear multiplier on all channels
//   2. Contrast  — re-centers around 0.5, scales the deviation
//   3. Saturation — lerp between luminance and full-colour
//   4. Gamma     — power-law encoding (sRGB-like when gamma = 2.2)
//   5. Vignette  — radial darkening from screen corners toward center
//
// Usage:
//   filter.Apply(scene.outputImage, scene.width, scene.height);
//   glDrawPixels(Width, Height, GL_RGB, GL_FLOAT, scene.outputImage.data());
// ---------------------------------------------------------------------------
class UPostProcessFilter
{
public:
    // --- Exposure & tone ---
    float exposure   = 1.0f;   // linear gain before all other effects  (>1 = brighter)
    float contrast   = 1.0f;   // deviation scale around 0.5            (>1 = more contrast)

    // --- Color ---
    float saturation = 1.0f;   // 0 = greyscale, 1 = unchanged, >1 = hyper-saturated
    float gamma      = 2.2f;   // gamma encoding exponent               (0 disables)

    // --- Vignette ---
    float vignette   = 0.0f;   // 0 = off, 1 = strong darkening at corners
    float vignetteRadius = 0.75f; // normalised distance from center where vignette starts

    // Apply all enabled effects in-place on `image`.
    // image: flat RGB float array, size width*height*3, values in [0,1].
    void Apply(std::vector<float>& image, int width, int height) const;
};
