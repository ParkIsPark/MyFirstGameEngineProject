#pragma once

#include <cstdint>

enum class ERenderTargetKind
{
    DefaultFramebuffer,
    TextureViewport,
};

struct FRenderTargetAttachments
{
    unsigned framebuffer = 0;
    unsigned colorTexture = 0;
    unsigned depthAttachment = 0;
};

struct FRenderTargetBindingState
{
    int framebuffer = 0;
    int viewport[4] = {0, 0, 0, 0};
};

class IRenderTargetGLAdapter
{
public:
    virtual ~IRenderTargetGLAdapter() = default;
    virtual std::uint64_t ActiveContextGeneration() const noexcept = 0;
    virtual bool AllocateTextureViewport(int width, int height,
                                         FRenderTargetAttachments& out) = 0;
    virtual void DeleteTextureViewport(
        const FRenderTargetAttachments& attachments) noexcept = 0;
    virtual FRenderTargetBindingState CaptureBindingState() = 0;
    virtual void BindTarget(unsigned framebuffer, int width, int height) = 0;
    virtual void RestoreBindingState(
        const FRenderTargetBindingState& state) noexcept = 0;
};

void SetActiveRenderTargetContextGeneration(std::uint64_t generation) noexcept;

// Move-only render destination. A default target is metadata-only. A texture
// viewport owns its FBO/color/depth attachments and exposes only the color
// texture identity needed by ImGui; attachment formats stay private.
class FRenderTarget
{
public:
    FRenderTarget() = default;
    ~FRenderTarget();
    FRenderTarget(const FRenderTarget&) = delete;
    FRenderTarget& operator=(const FRenderTarget&) = delete;
    FRenderTarget(FRenderTarget&& other) noexcept;
    FRenderTarget& operator=(FRenderTarget&& other) noexcept;

    static FRenderTarget DefaultFramebuffer(int width,
                                            int height,
                                            std::uint64_t contextGeneration);
    static FRenderTarget TextureViewport(int width,
                                         int height,
                                         std::uint64_t contextGeneration);
    static FRenderTarget TextureViewport(int width,
                                         int height,
                                         std::uint64_t contextGeneration,
                                         IRenderTargetGLAdapter& adapter);

    bool Resize(int width, int height, std::uint64_t contextGeneration);
    bool IsValid() const;
    bool IsValidForContext(std::uint64_t contextGeneration) const;

    ERenderTargetKind Kind() const { return kind_; }
    unsigned Identity() const { return attachments_.framebuffer; }
    unsigned ColorTexture() const { return attachments_.colorTexture; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    std::uint64_t ContextGeneration() const { return contextGeneration_; }

    // UWorldRenderer pairs Begin/End around every executor call. End restores
    // the caller's framebuffer and viewport, including failure paths.
    bool Begin();
    void End() noexcept;
    void Release() noexcept;

private:
    FRenderTarget(ERenderTargetKind kind,
                  int width,
                  int height,
                  std::uint64_t contextGeneration,
                  IRenderTargetGLAdapter& adapter);
    bool AllocateTextureAttachments();
    void MoveFrom(FRenderTarget&& other) noexcept;

    ERenderTargetKind kind_ = ERenderTargetKind::DefaultFramebuffer;
    int width_ = 0;
    int height_ = 0;
    std::uint64_t contextGeneration_ = 0;
    FRenderTargetAttachments attachments_;
    FRenderTargetBindingState priorBinding_;
    IRenderTargetGLAdapter* adapter_ = nullptr;
    bool bound_ = false;
};
