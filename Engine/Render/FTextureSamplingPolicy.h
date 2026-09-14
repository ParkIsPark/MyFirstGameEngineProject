#pragma once

#include <cstdint>
#include <vector>

struct Material;

struct FTextureSamplingPolicy
{
    bool anisotropySupported = false;
    float requestedAnisotropy = 1.0f;
    float maximumAnisotropy = 1.0f;

    float EffectiveAnisotropy() const;
};

std::vector<unsigned char> ResampleRGBA8ToLayer(
    const Material& source, int targetWidth, int targetHeight);

FTextureSamplingPolicy TextureSamplingPolicyForContext(
    std::uint64_t contextGeneration, float requestedAnisotropy);
