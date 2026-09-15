#pragma once

#include <cstddef>
#include <cstdint>

enum class EHardwareGBufferSemantic : unsigned
{
    PositionCoverage = 0,
    GeometricNormal = 1,
    ShadingNormalModel = 2,
    AlbedoShininess = 3,
    SpecularMirror = 4,
    Identity = 5,
    PrecomputedLighting = 6,
    Emissive = 7,
    Count = 8,
};
// OpenGL 3.3 physical storage for the backend-neutral logical G-buffer.
// Geometry binding captures and restores framebuffer/viewport state as a pair.
class UHardwareGBuffer
{
public:
    UHardwareGBuffer() = default;
    ~UHardwareGBuffer() noexcept;
    UHardwareGBuffer(const UHardwareGBuffer&) = delete;
    UHardwareGBuffer& operator=(const UHardwareGBuffer&) = delete;

    bool Resize(int width, int height, std::uint64_t contextGeneration);
    bool BindForGeometry();
    void EndGeometry() noexcept;
    void Clear();
    void Release() noexcept;

    void BindTexture(EHardwareGBufferSemantic semantic, unsigned textureUnit) const;
    void BindDepthTexture(unsigned textureUnit) const;

    bool IsComplete() const { return complete_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    std::uint64_t ContextGeneration() const { return contextGeneration_; }
    std::uint64_t ResourceRevision() const { return resourceRevision_; }
    std::uint64_t ResourceAllocations() const { return resourceAllocations_; }
    std::uint64_t ReleasedResources() const { return releasedResources_; }
    std::uint64_t ResourceIdentity() const;
    std::size_t OwnedTextureCount() const;
    unsigned Framebuffer() const { return framebuffer_; }
    unsigned Texture(EHardwareGBufferSemantic semantic) const;
    unsigned DepthTexture() const { return depthTexture_; }
    unsigned ColorAttachment(EHardwareGBufferSemantic semantic) const;

private:
    static constexpr std::size_t ColorTextureCount =
        static_cast<std::size_t>(EHardwareGBufferSemantic::Count);

    void DeleteCurrentResources() noexcept;
    void ForgetCurrentResources() noexcept;

    unsigned framebuffer_ = 0;
    unsigned colorTextures_[ColorTextureCount] = {};
    unsigned depthTexture_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::uint64_t contextGeneration_ = 0;
    std::uint64_t resourceRevision_ = 0;
    std::uint64_t resourceAllocations_ = 0;
    std::uint64_t releasedResources_ = 0;
    int priorDrawFramebuffer_ = 0;
    int priorReadFramebuffer_ = 0;
    int priorViewport_[4] = {};
    bool complete_ = false;
    bool boundForGeometry_ = false;
};
