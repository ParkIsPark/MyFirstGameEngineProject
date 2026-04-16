#include "UPostProcessFilter.h"
#include <cmath>
#include <algorithm>

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void UPostProcessFilter::Apply(std::vector<float>& image, int width, int height) const
{
    const int pixels = width * height;

    for (int p = 0; p < pixels; ++p)
    {
        float* px = &image[p * 3];

        // 1. Exposure
        px[0] *= exposure;
        px[1] *= exposure;
        px[2] *= exposure;

        // 2. Contrast  (scale deviation around 0.5)
        if (contrast != 1.0f)
        {
            px[0] = (px[0] - 0.5f) * contrast + 0.5f;
            px[1] = (px[1] - 0.5f) * contrast + 0.5f;
            px[2] = (px[2] - 0.5f) * contrast + 0.5f;
        }

        // 3. Saturation  (Rec.709 luminance weights)
        if (saturation != 1.0f)
        {
            float lum = 0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2];
            px[0] = lum + saturation * (px[0] - lum);
            px[1] = lum + saturation * (px[1] - lum);
            px[2] = lum + saturation * (px[2] - lum);
        }

        // Clamp before gamma to keep values in domain for pow()
        px[0] = clamp01(px[0]);
        px[1] = clamp01(px[1]);
        px[2] = clamp01(px[2]);

        // 4. Gamma encoding
        if (gamma > 0.0f && gamma != 1.0f)
        {
            float invGamma = 1.0f / gamma;
            px[0] = std::pow(px[0], invGamma);
            px[1] = std::pow(px[1], invGamma);
            px[2] = std::pow(px[2], invGamma);
        }

        // 5. Vignette  (radial darkening)
        if (vignette > 0.0f)
        {
            // Pixel UV in [0,1], center = (0.5, 0.5)
            float u = (static_cast<float>(p % width)  + 0.5f) / static_cast<float>(width);
            float v = (static_cast<float>(p / width)  + 0.5f) / static_cast<float>(height);
            float du = u - 0.5f;
            float dv = v - 0.5f;
            float dist = std::sqrt(du * du + dv * dv) * 1.4142f; // normalise so corner = 1

            // smoothstep-like fade beyond vignetteRadius
            float fade = (dist - vignetteRadius) / (1.0f - vignetteRadius);
            fade = clamp01(fade);
            float mask = 1.0f - vignette * fade * fade;

            px[0] *= mask;
            px[1] *= mask;
            px[2] *= mask;
        }
    }
}
