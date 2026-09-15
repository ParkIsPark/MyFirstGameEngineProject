#include "FTextureSamplingPolicy.h"
#include "Material.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace
{
bool Near(float lhs, float rhs)
{
    return std::fabs(lhs - rhs) < 1.0e-6f;
}
}

int main()
{
    assert(Near(FTextureSamplingPolicy{false, 8.0f, 16.0f}.
        EffectiveAnisotropy(), 1.0f));
    assert(Near(FTextureSamplingPolicy{true, 8.0f, 16.0f}.
        EffectiveAnisotropy(), 8.0f));
    assert(Near(FTextureSamplingPolicy{true, 8.0f, 4.0f}.
        EffectiveAnisotropy(), 4.0f));
    assert(Near(FTextureSamplingPolicy{true, NAN, 16.0f}.
        EffectiveAnisotropy(), 1.0f));
    assert(Near(FTextureSamplingPolicy{true, 8.0f, INFINITY}.
        EffectiveAnisotropy(), 1.0f));
    assert(Near(FTextureSamplingPolicy{true, 8.0f, 0.0f}.
        EffectiveAnisotropy(), 1.0f));

    Material source;
    source.texWidth = 2;
    source.texHeight = 1;
    source.texChannels = 4;
    source.texData = {255, 10, 20, 255, 30, 200, 40, 128};
    const std::vector<unsigned char> layer =
        ResampleRGBA8ToLayer(source, 7, 5);
    assert(layer.size() == 7u * 5u * 4u);
    for (std::size_t texel = 0; texel < layer.size(); texel += 4u)
    {
        assert(layer[texel + 3u] != 0u);
        assert(layer[texel] != 0u || layer[texel + 1u] != 0u ||
               layer[texel + 2u] != 0u);
    }
    assert(layer[0] == 255u && layer[1] == 10u && layer[2] == 20u);
    const std::size_t last = layer.size() - 4u;
    assert(layer[last] == 30u && layer[last + 1u] == 200u &&
           layer[last + 2u] == 40u && layer[last + 3u] == 128u);

    Material gray;
    gray.texWidth = 2;
    gray.texHeight = 1;
    gray.texChannels = 1;
    gray.texData = {17u, 231u};
    const auto grayLayer = ResampleRGBA8ToLayer(gray, 2, 1);
    assert((grayLayer == std::vector<unsigned char>{
        17u, 17u, 17u, 255u, 231u, 231u, 231u, 255u}));

    Material grayAlpha;
    grayAlpha.texWidth = 2;
    grayAlpha.texHeight = 1;
    grayAlpha.texChannels = 2;
    grayAlpha.texData = {29u, 41u, 199u, 211u};
    const auto grayAlphaLayer = ResampleRGBA8ToLayer(grayAlpha, 2, 1);
    assert((grayAlphaLayer == std::vector<unsigned char>{
        29u, 29u, 29u, 41u, 199u, 199u, 199u, 211u}));

    std::puts("TextureSamplingPolicyTest passed");
    return 0;
}
