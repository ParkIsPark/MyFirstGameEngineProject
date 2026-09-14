#pragma once

#include "FRenderHistory.h"
#include "FRenderOutputs.h"

#include <cstddef>
#include <cstdint>
#include <string>

struct FRenderQuality;
class UHardwareGBuffer;

struct FRayEffectsReconstructionStats
{
    std::uint64_t resourceAllocations = 0;
    std::uint64_t releasedResources = 0;
    std::uint64_t reconstructionPasses = 0;
    std::size_t ownedTextures = 0;
    std::size_t ownedFramebuffers = 0;
};

class URayEffectsReconstruction
{
public:
    URayEffectsReconstruction() = default;
    ~URayEffectsReconstruction() noexcept;
    URayEffectsReconstruction(const URayEffectsReconstruction&) = delete;
    URayEffectsReconstruction& operator=(const URayEffectsReconstruction&) = delete;

    bool Reconstruct(const UHardwareGBuffer& gbuffer,
                     const FRayEffectOutputs& raw,
                     const FTemporalFrame& frame,
                     const FRenderQuality& quality,
                     std::uint64_t contextGeneration,
                     FRayEffectOutputs& reconstructed,
                     std::string* diagnostic = nullptr);
    void Reset() noexcept;
    void Shutdown() noexcept;
    const FRayEffectsReconstructionStats& Stats() const { return stats_; }

private:
    bool EnsureResources(int width, int height, std::uint64_t contextGeneration,
                         std::string* diagnostic);
    void DeleteCurrentResources() noexcept;
    void ForgetCurrentResources() noexcept;

    unsigned accumulateProgram_ = 0;
    unsigned filterProgram_ = 0;
    unsigned fullscreenVAO_ = 0;
    unsigned framebuffer_ = 0;
    unsigned shadowHistory_[2] = {};
    unsigned giHistory_[2] = {};
    unsigned scratch_[4] = {};
    int width_ = 0;
    int height_ = 0;
    std::uint64_t contextGeneration_ = 0;
    bool historyValid_ = false;
    FRayEffectsReconstructionStats stats_;
};
