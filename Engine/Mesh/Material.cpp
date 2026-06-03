#include "Material.h"
#include "FArchive.h"

#include <cmath>
#include <algorithm>

// Numeric Blinn-Phong fields + diffuse-texture metadata (path / wrap / tiling).
// Texture pixels are never serialized here — only the Content-relative path;
// CPU/GL texture data stays runtime-only.
void Material::Serialize(FArchive& ar)
{
    ar.Color("kd",         kd);
    ar.Color("ks",         ks);
    ar.Color("ka",         ka);
    ar.Field("shininess",  shininess);
    ar.Color("km",         km);
    ar.Color("emissive",   emissive);
    ar.Field("diffuseTex", diffuseTexPath);
    int wm = static_cast<int>(wrapMode);
    ar.Field("wrap", wm);
    wrapMode = static_cast<EWrapMode>(wm);     // no-op when saving
    ar.Field("uvTiling", uvTiling);
}

glm::vec3 Material::SampleDiffuse(glm::vec2 uv) const
{
    if (texData.empty() || texWidth <= 0 || texHeight <= 0) return kd;

    uv *= uvTiling;
    if (wrapMode == EWrapMode::Repeat) { uv.x -= std::floor(uv.x); uv.y -= std::floor(uv.y); }
    else                               { uv.x = glm::clamp(uv.x, 0.0f, 1.0f); uv.y = glm::clamp(uv.y, 0.0f, 1.0f); }

    const int ch = texChannels > 0 ? texChannels : 3;
    const float fx = uv.x * (texWidth  - 1);
    const float fy = uv.y * (texHeight - 1);
    const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    const int x1 = std::min(x0 + 1, texWidth  - 1), y1 = std::min(y0 + 1, texHeight - 1);
    const float tx = fx - x0, ty = fy - y0;

    auto texel = [&](int x, int y) -> glm::vec3
    {
        const size_t idx = ((size_t)y * texWidth + x) * ch;
        glm::vec3 c(0.0f);
        if (idx + 2 < texData.size())
            c = glm::vec3(texData[idx], texData[idx + 1], texData[idx + 2]) / 255.0f;
        else if (idx < texData.size())
            c = glm::vec3(texData[idx] / 255.0f);
        return glm::pow(c, glm::vec3(2.2f));     // sRGB -> linear
    };

    const glm::vec3 cx0 = glm::mix(texel(x0, y0), texel(x1, y0), tx);
    const glm::vec3 cx1 = glm::mix(texel(x0, y1), texel(x1, y1), tx);
    return glm::mix(cx0, cx1, ty);
}
