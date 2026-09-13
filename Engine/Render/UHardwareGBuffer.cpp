#include "UHardwareGBuffer.h"
#include "FRenderTarget.h"

#include <GL/glew.h>

#include <algorithm>

namespace
{
constexpr GLenum ColorAttachments[] = {
    GL_COLOR_ATTACHMENT0,
    GL_COLOR_ATTACHMENT1,
    GL_COLOR_ATTACHMENT2,
    GL_COLOR_ATTACHMENT3,
    GL_COLOR_ATTACHMENT4,
    GL_COLOR_ATTACHMENT5,
    GL_COLOR_ATTACHMENT6,
    GL_COLOR_ATTACHMENT7,
};

constexpr GLint InternalFormats[] = {
    GL_RGBA32F, // world position + coverage
    GL_RGBA32F, // geometric normal
    GL_RGBA32F, // shading normal + shading model
    GL_RGBA32F, // resolved albedo + shininess
    GL_RGBA32F, // specular color + mirror factor
    GL_RG32UI,  // object + material identity
    GL_RGBA16F, // flat/Gouraud precomputed lighting
    GL_RGBA16F, // emissive color
};

constexpr GLenum ExternalFormats[] = {
    GL_RGBA, GL_RGBA, GL_RGBA, GL_RGBA, GL_RGBA, GL_RG_INTEGER,
    GL_RGBA, GL_RGBA,
};

constexpr GLenum ExternalTypes[] = {
    GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_UNSIGNED_INT,
    GL_FLOAT, GL_FLOAT,
};
}

UHardwareGBuffer::~UHardwareGBuffer() noexcept
{
    Release();
}

bool UHardwareGBuffer::Resize(int width, int height,
                              std::uint64_t contextGeneration)
{
    if (boundForGeometry_ || width <= 0 || height <= 0 || contextGeneration == 0 ||
        contextGeneration != ActiveRenderTargetContextGeneration())
        return false;
    if (complete_ && width == width_ && height == height_ &&
        contextGeneration == contextGeneration_)
        return true;

    GLint maxColorAttachments = 0;
    GLint maxDrawBuffers = 0;
    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxColorAttachments);
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    if (maxColorAttachments < static_cast<GLint>(ColorTextureCount) ||
        maxDrawBuffers < static_cast<GLint>(ColorTextureCount) ||
        width > maxTextureSize || height > maxTextureSize)
        return false;

    if (framebuffer_)
    {
        if (contextGeneration_ == contextGeneration) DeleteCurrentResources();
        else ForgetCurrentResources(); // old names belong to the destroyed context
    }

    GLint previousDrawFramebuffer = 0;
    GLint previousReadFramebuffer = 0;
    GLint previousActiveTexture = 0;
    GLint previousTexture = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);

    glGenFramebuffers(1, &framebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glGenTextures(static_cast<GLsizei>(ColorTextureCount), colorTextures_);
    for (std::size_t i = 0; i < ColorTextureCount; ++i)
    {
        glBindTexture(GL_TEXTURE_2D, colorTextures_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, InternalFormats[i], width, height, 0,
                     ExternalFormats[i], ExternalTypes[i], nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, ColorAttachments[i], GL_TEXTURE_2D,
                               colorTextures_[i], 0);
    }

    glGenTextures(1, &depthTexture_);
    glBindTexture(GL_TEXTURE_2D, depthTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0,
                 GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           depthTexture_, 0);
    glDrawBuffers(static_cast<GLsizei>(ColorTextureCount), ColorAttachments);

    complete_ = framebuffer_ != 0 &&
        std::all_of(std::begin(colorTextures_), std::end(colorTextures_),
                    [](unsigned texture) { return texture != 0; }) &&
        depthTexture_ != 0 &&
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    glBindTexture(GL_TEXTURE_2D, static_cast<unsigned>(previousTexture));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<unsigned>(previousDrawFramebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<unsigned>(previousReadFramebuffer));

    if (!complete_)
    {
        DeleteCurrentResources();
        return false;
    }

    width_ = width;
    height_ = height;
    contextGeneration_ = contextGeneration;
    ++resourceRevision_;
    return true;
}

bool UHardwareGBuffer::BindForGeometry()
{
    if (!complete_ || boundForGeometry_) return false;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &priorDrawFramebuffer_);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &priorReadFramebuffer_);
    glGetIntegerv(GL_VIEWPORT, priorViewport_);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, width_, height_);
    glDrawBuffers(static_cast<GLsizei>(ColorTextureCount), ColorAttachments);
    boundForGeometry_ = true;
    return true;
}

void UHardwareGBuffer::EndGeometry() noexcept
{
    if (!boundForGeometry_) return;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<unsigned>(priorDrawFramebuffer_));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<unsigned>(priorReadFramebuffer_));
    glViewport(priorViewport_[0], priorViewport_[1], priorViewport_[2], priorViewport_[3]);
    boundForGeometry_ = false;
}

void UHardwareGBuffer::Clear()
{
    const bool wasBound = boundForGeometry_;
    if (!wasBound && !BindForGeometry()) return;

    const GLfloat positionCoverage[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const GLfloat normal[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    const GLfloat shadingNormalModel[4] = {0.0f, 0.0f, 1.0f, 2.0f};
    const GLfloat albedoShininess[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    const GLfloat specularMirror[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const GLuint identity[4] = {0u, 0u, 0u, 0u};
    const GLfloat black[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const GLfloat depth = 1.0f;
    glClearBufferfv(GL_COLOR, 0, positionCoverage);
    glClearBufferfv(GL_COLOR, 1, normal);
    glClearBufferfv(GL_COLOR, 2, shadingNormalModel);
    glClearBufferfv(GL_COLOR, 3, albedoShininess);
    glClearBufferfv(GL_COLOR, 4, specularMirror);
    glClearBufferuiv(GL_COLOR, 5, identity);
    glClearBufferfv(GL_COLOR, 6, black);
    glClearBufferfv(GL_COLOR, 7, black);
    glClearBufferfv(GL_DEPTH, 0, &depth);

    if (!wasBound) EndGeometry();
}

void UHardwareGBuffer::Release() noexcept
{
    EndGeometry();
    if (contextGeneration_ != 0 &&
        contextGeneration_ == ActiveRenderTargetContextGeneration())
        DeleteCurrentResources();
    else
        ForgetCurrentResources();
}

void UHardwareGBuffer::BindTexture(EHardwareGBufferSemantic semantic,
                                    unsigned textureUnit) const
{
    glActiveTexture(GL_TEXTURE0 + textureUnit);
    glBindTexture(GL_TEXTURE_2D, Texture(semantic));
}

void UHardwareGBuffer::BindDepthTexture(unsigned textureUnit) const
{
    glActiveTexture(GL_TEXTURE0 + textureUnit);
    glBindTexture(GL_TEXTURE_2D, depthTexture_);
}

std::size_t UHardwareGBuffer::OwnedTextureCount() const
{
    std::size_t count = depthTexture_ ? 1u : 0u;
    for (unsigned texture : colorTextures_)
        if (texture) ++count;
    return count;
}

unsigned UHardwareGBuffer::Texture(EHardwareGBufferSemantic semantic) const
{
    const std::size_t index = static_cast<std::size_t>(semantic);
    return index < ColorTextureCount ? colorTextures_[index] : 0;
}

unsigned UHardwareGBuffer::ColorAttachment(EHardwareGBufferSemantic semantic) const
{
    const std::size_t index = static_cast<std::size_t>(semantic);
    return index < ColorTextureCount ? ColorAttachments[index] : GL_NONE;
}

void UHardwareGBuffer::DeleteCurrentResources() noexcept
{
    if (depthTexture_) glDeleteTextures(1, &depthTexture_);
    glDeleteTextures(static_cast<GLsizei>(ColorTextureCount), colorTextures_);
    if (framebuffer_) glDeleteFramebuffers(1, &framebuffer_);
    ForgetCurrentResources();
}

void UHardwareGBuffer::ForgetCurrentResources() noexcept
{
    framebuffer_ = 0;
    std::fill(std::begin(colorTextures_), std::end(colorTextures_), 0u);
    depthTexture_ = 0;
    width_ = 0;
    height_ = 0;
    contextGeneration_ = 0;
    complete_ = false;
    boundForGeometry_ = false;
}
