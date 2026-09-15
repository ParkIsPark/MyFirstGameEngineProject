#include "FTextureSamplingPolicy.h"

#include "Material.h"

#include <GL/glew.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace
{
std::uint64_t GCachedContextGeneration = 0;
bool GHasCachedCapabilities = false;
bool GCachedAnisotropySupported = false;
float GCachedMaximumAnisotropy = 1.0f;
}

float FTextureSamplingPolicy::EffectiveAnisotropy() const
{
    if (!anisotropySupported || !std::isfinite(requestedAnisotropy) ||
        !std::isfinite(maximumAnisotropy) || requestedAnisotropy < 1.0f ||
        maximumAnisotropy < 1.0f)
        return 1.0f;
    return std::min(requestedAnisotropy, maximumAnisotropy);
}

std::vector<unsigned char> ResampleRGBA8ToLayer(
    const Material& source, int targetWidth, int targetHeight)
{
    const int channels = source.texChannels > 0 ? source.texChannels : 3;
    const std::size_t required = source.texWidth > 0 && source.texHeight > 0 &&
        channels >= 1 && channels <= 4
        ? static_cast<std::size_t>(source.texWidth) *
            static_cast<std::size_t>(source.texHeight) *
            static_cast<std::size_t>(channels)
        : 0u;
    if (targetWidth <= 0 || targetHeight <= 0 || required == 0u ||
        source.texData.size() < required)
        return {};

    std::vector<unsigned char> result(
        static_cast<std::size_t>(targetWidth) *
        static_cast<std::size_t>(targetHeight) * 4u);
    for (int y = 0; y < targetHeight; ++y)
    {
        const int sourceY = std::min(source.texHeight - 1,
            static_cast<int>((static_cast<long long>(y) * source.texHeight) /
                             targetHeight));
        for (int x = 0; x < targetWidth; ++x)
        {
            const int sourceX = std::min(source.texWidth - 1,
                static_cast<int>((static_cast<long long>(x) * source.texWidth) /
                                 targetWidth));
            const std::size_t sourceOffset =
                (static_cast<std::size_t>(sourceY) * source.texWidth + sourceX) *
                static_cast<std::size_t>(channels);
            const std::size_t targetOffset =
                (static_cast<std::size_t>(y) * targetWidth + x) * 4u;
            const unsigned char grayOrRed = source.texData[sourceOffset];
            result[targetOffset] = grayOrRed;
            result[targetOffset + 1u] = channels <= 2
                ? grayOrRed : source.texData[sourceOffset + 1u];
            result[targetOffset + 2u] = channels <= 2
                ? grayOrRed : source.texData[sourceOffset + 2u];
            result[targetOffset + 3u] = channels == 2
                ? source.texData[sourceOffset + 1u]
                : channels == 4 ? source.texData[sourceOffset + 3u] : 255u;
        }
    }
    return result;
}

FTextureSamplingPolicy TextureSamplingPolicyForContext(
    std::uint64_t contextGeneration, float requestedAnisotropy)
{
    if (!GHasCachedCapabilities || GCachedContextGeneration != contextGeneration)
    {
        GHasCachedCapabilities = true;
        GCachedContextGeneration = contextGeneration;
        GCachedAnisotropySupported = false;
        GCachedMaximumAnisotropy = 1.0f;
        if (contextGeneration != 0 && GLEW_EXT_texture_filter_anisotropic)
        {
            GLfloat maximum = 1.0f;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maximum);
            if (glGetError() == GL_NO_ERROR && std::isfinite(maximum) &&
                maximum >= 1.0f)
            {
                GCachedAnisotropySupported = true;
                GCachedMaximumAnisotropy = maximum;
            }
        }
    }
    FTextureSamplingPolicy result;
    result.anisotropySupported = GCachedAnisotropySupported;
    result.requestedAnisotropy = requestedAnisotropy;
    result.maximumAnisotropy = GCachedMaximumAnisotropy;
    return result;
}
