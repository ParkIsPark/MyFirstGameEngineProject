#pragma once

#include "FRenderOutputs.h"

#include <cstdint>
#include <string>

struct FRenderQuality;
class FRenderTarget;
class UHardwareGBuffer;

struct FInternalRenderSize
{
    int width = 0;
    int height = 0;
};

FInternalRenderSize ResolveInternalRenderSize(
    int outputWidth, int outputHeight, int ssaa, int maxTextureSize);

class UHybridPresentationPass
{
public:
    UHybridPresentationPass() = default;
    ~UHybridPresentationPass() noexcept;
    UHybridPresentationPass(const UHybridPresentationPass&) = delete;
    UHybridPresentationPass& operator=(const UHybridPresentationPass&) = delete;

    bool Init(std::uint64_t contextGeneration, std::string* diagnostic = nullptr);
    bool Resize(int internalWidth, int internalHeight,
                std::uint64_t contextGeneration, std::string* diagnostic = nullptr);
    bool CompositeHDR(const UHardwareGBuffer& gbuffer,
                      const FRasterLightingOutput& raster,
                      const FRayEffectOutputs& rayEffects,
                      const FRenderQuality& quality,
                      FRenderOutputView& hdrOutput,
                      std::string* diagnostic = nullptr);
    bool Present(const FRenderOutputView& hdrInput,
                 FRenderTarget& outputTarget,
                 const FRenderQuality& quality,
                 std::string* diagnostic = nullptr);
    void Shutdown() noexcept;

    bool IsReady() const { return compositeProgram_ != 0 && presentationProgram_ != 0; }
    unsigned HDRTexture() const { return hdrTexture_; }
    unsigned Framebuffer() const { return framebuffer_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    std::uint64_t ResourceRevision() const { return resourceRevision_; }

private:
    void DeleteCurrentResources() noexcept;
    void ForgetCurrentResources() noexcept;

    unsigned compositeProgram_ = 0;
    unsigned presentationProgram_ = 0;
    unsigned fullscreenVAO_ = 0;
    unsigned framebuffer_ = 0;
    unsigned hdrTexture_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::uint64_t contextGeneration_ = 0;
    std::uint64_t resourceRevision_ = 0;
};
