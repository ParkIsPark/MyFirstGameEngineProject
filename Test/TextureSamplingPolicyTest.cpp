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

    std::puts("TextureSamplingPolicyTest passed");
    return 0;
}
