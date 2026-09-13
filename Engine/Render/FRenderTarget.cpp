#include "FRenderTarget.h"

#include <GL/glew.h>

#include <utility>

namespace
{
class FOpenGLRenderTargetAdapter final : public IRenderTargetGLAdapter
{
public:
    std::uint64_t ActiveContextGeneration() const noexcept override
    {
        return activeGeneration_;
    }

    void SetActiveContextGeneration(std::uint64_t generation) noexcept
    {
        activeGeneration_ = generation;
    }

    bool AllocateTextureViewport(int width, int height,
                                 FRenderTargetAttachments& out) override
    {
        GLint previousFramebuffer = 0;
        GLint previousTexture = 0;
        GLint previousRenderbuffer = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &previousRenderbuffer);

        FRenderTargetAttachments created;
        glGenFramebuffers(1, &created.framebuffer);
        glGenTextures(1, &created.colorTexture);
        glGenRenderbuffers(1, &created.depthAttachment);
        if (created.framebuffer && created.colorTexture && created.depthAttachment)
        {
            glBindTexture(GL_TEXTURE_2D, created.colorTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0,
                         GL_RGB, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            glBindRenderbuffer(GL_RENDERBUFFER, created.depthAttachment);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

            glBindFramebuffer(GL_FRAMEBUFFER, created.framebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, created.colorTexture, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                      GL_RENDERBUFFER, created.depthAttachment);
        }
        const bool complete = created.framebuffer && created.colorTexture
            && created.depthAttachment
            && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned>(previousFramebuffer));
        glBindTexture(GL_TEXTURE_2D, static_cast<unsigned>(previousTexture));
        glBindRenderbuffer(GL_RENDERBUFFER, static_cast<unsigned>(previousRenderbuffer));
        if (!complete)
        {
            DeleteTextureViewport(created);
            return false;
        }
        out = created;
        return true;
    }

    void DeleteTextureViewport(
        const FRenderTargetAttachments& attachments) noexcept override
    {
        if (attachments.depthAttachment)
            glDeleteRenderbuffers(1, &attachments.depthAttachment);
        if (attachments.colorTexture)
            glDeleteTextures(1, &attachments.colorTexture);
        if (attachments.framebuffer)
            glDeleteFramebuffers(1, &attachments.framebuffer);
    }

    FRenderTargetBindingState CaptureBindingState() override
    {
        FRenderTargetBindingState state;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &state.framebuffer);
        glGetIntegerv(GL_VIEWPORT, state.viewport);
        return state;
    }

    void BindTarget(unsigned framebuffer, int width, int height) override
    {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, width, height);
    }

    void RestoreBindingState(const FRenderTargetBindingState& state) noexcept override
    {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned>(state.framebuffer));
        glViewport(state.viewport[0], state.viewport[1],
                   state.viewport[2], state.viewport[3]);
    }

private:
    std::uint64_t activeGeneration_ = 0;
};

FOpenGLRenderTargetAdapter& DefaultAdapter()
{
    static FOpenGLRenderTargetAdapter adapter;
    return adapter;
}
} // namespace

void SetActiveRenderTargetContextGeneration(std::uint64_t generation) noexcept
{
    DefaultAdapter().SetActiveContextGeneration(generation);
}

std::uint64_t ActiveRenderTargetContextGeneration() noexcept
{
    return DefaultAdapter().ActiveContextGeneration();
}

FRenderTarget::FRenderTarget(ERenderTargetKind kind,
                             int width,
                             int height,
                             std::uint64_t contextGeneration,
                             IRenderTargetGLAdapter& adapter)
    : kind_(kind), width_(width), height_(height),
      contextGeneration_(contextGeneration), adapter_(&adapter)
{
}

FRenderTarget::~FRenderTarget()
{
    Release();
}

FRenderTarget::FRenderTarget(FRenderTarget&& other) noexcept
{
    MoveFrom(std::move(other));
}

FRenderTarget& FRenderTarget::operator=(FRenderTarget&& other) noexcept
{
    if (this != &other)
    {
        Release();
        MoveFrom(std::move(other));
    }
    return *this;
}

FRenderTarget FRenderTarget::DefaultFramebuffer(int width,
                                                int height,
                                                std::uint64_t contextGeneration)
{
    return FRenderTarget(ERenderTargetKind::DefaultFramebuffer,
                         width, height, contextGeneration, DefaultAdapter());
}

FRenderTarget FRenderTarget::TextureViewport(int width,
                                             int height,
                                             std::uint64_t contextGeneration)
{
    return TextureViewport(width, height, contextGeneration, DefaultAdapter());
}

FRenderTarget FRenderTarget::TextureViewport(int width,
                                             int height,
                                             std::uint64_t contextGeneration,
                                             IRenderTargetGLAdapter& adapter)
{
    FRenderTarget result(ERenderTargetKind::TextureViewport,
                         width, height, contextGeneration, adapter);
    if (!result.AllocateTextureAttachments())
    {
        result.width_ = 0;
        result.height_ = 0;
    }
    return result;
}

bool FRenderTarget::Resize(int width, int height, std::uint64_t contextGeneration)
{
    if (width <= 0 || height <= 0 || contextGeneration == 0) return false;
    if (width == width_ && height == height_ && contextGeneration == contextGeneration_)
        return IsValid();

    if (kind_ == ERenderTargetKind::DefaultFramebuffer)
    {
        width_ = width;
        height_ = height;
        contextGeneration_ = contextGeneration;
        return true;
    }

    Release();
    width_ = width;
    height_ = height;
    contextGeneration_ = contextGeneration;
    if (!AllocateTextureAttachments())
    {
        width_ = 0;
        height_ = 0;
        return false;
    }
    return true;
}

bool FRenderTarget::IsValid() const
{
    if (width_ <= 0 || height_ <= 0 || contextGeneration_ == 0) return false;
    if (kind_ == ERenderTargetKind::DefaultFramebuffer) return true;
    return attachments_.framebuffer != 0 && attachments_.colorTexture != 0
        && attachments_.depthAttachment != 0;
}

bool FRenderTarget::IsValidForContext(std::uint64_t contextGeneration) const
{
    return IsValid() && contextGeneration != 0 && contextGeneration_ == contextGeneration;
}

bool FRenderTarget::Begin()
{
    if (!adapter_ || !IsValid() || bound_
        || adapter_->ActiveContextGeneration() != contextGeneration_)
        return false;
    priorBinding_ = adapter_->CaptureBindingState();
    adapter_->BindTarget(kind_ == ERenderTargetKind::TextureViewport
        ? attachments_.framebuffer : 0, width_, height_);
    bound_ = true;
    return true;
}

void FRenderTarget::End() noexcept
{
    if (!bound_) return;
    if (adapter_ && adapter_->ActiveContextGeneration() == contextGeneration_)
        adapter_->RestoreBindingState(priorBinding_);
    bound_ = false;
}

void FRenderTarget::Release() noexcept
{
    End();
    if (adapter_ && adapter_->ActiveContextGeneration() == contextGeneration_
        && (attachments_.framebuffer || attachments_.colorTexture
            || attachments_.depthAttachment))
        adapter_->DeleteTextureViewport(attachments_);
    attachments_ = {};
    width_ = 0;
    height_ = 0;
    contextGeneration_ = 0;
}

bool FRenderTarget::AllocateTextureAttachments()
{
    if (kind_ != ERenderTargetKind::TextureViewport || !adapter_
        || width_ <= 0 || height_ <= 0 || contextGeneration_ == 0
        || adapter_->ActiveContextGeneration() != contextGeneration_)
        return false;
    return adapter_->AllocateTextureViewport(width_, height_, attachments_);
}

void FRenderTarget::MoveFrom(FRenderTarget&& other) noexcept
{
    kind_ = other.kind_;
    width_ = other.width_;
    height_ = other.height_;
    contextGeneration_ = other.contextGeneration_;
    attachments_ = other.attachments_;
    priorBinding_ = other.priorBinding_;
    adapter_ = other.adapter_;
    bound_ = other.bound_;
    other.width_ = 0;
    other.height_ = 0;
    other.contextGeneration_ = 0;
    other.attachments_ = {};
    other.adapter_ = nullptr;
    other.bound_ = false;
}
