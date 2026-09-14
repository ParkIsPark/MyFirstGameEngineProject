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
    // Independent from the wrapping shader-visible index. This is saturated
    // at the requested temporal cap and is the only source of averaging weight.
    std::uint32_t accumulatedFrames = 1;
};

FTemporalFrame AdvanceTemporalFrame(const FTemporalFrame& current,
                                    std::uint32_t frameCap) noexcept;
unsigned TemporalHistorySlot(const FTemporalFrame& frame) noexcept;
int TemporalShaderFrameIndex(const FTemporalFrame& frame) noexcept;
float TemporalCurrentWeight(const FTemporalFrame& frame,
                            int temporalFrames) noexcept;

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
    std::uint32_t accumulatedFrames_ = 1;
    std::uint32_t frameCap_ = 0;
    bool valid_ = false;
};
