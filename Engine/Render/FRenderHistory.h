#pragma once

#include "FRenderFeatures.h"

#include <cstdint>

struct FRenderQuality;
struct FRenderScene;

struct FTemporalFrame
{
    std::uint64_t signature = 0;
    std::uint32_t frameIndex = 0;
    bool reset = true;
};

std::uint64_t BuildRenderHistorySignature(
    const FRenderScene& scene,
    const FRenderFeatures& features,
    const FRenderQuality& quality,
    int internalWidth,
    int internalHeight,
    ERayTracingBackend backend,
    std::uint64_t contextGeneration,
    std::uint64_t environmentRevision);

class FTemporalSequence
{
public:
    FTemporalFrame Begin(std::uint64_t signature, std::uint32_t frameCap);
    void Reset() noexcept;

private:
    std::uint64_t signature_ = 0;
    std::uint32_t frameIndex_ = 0;
    std::uint32_t frameCap_ = 0;
    bool valid_ = false;
};
