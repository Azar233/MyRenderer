#include "render/GBuffer.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <utility>

#include <glad/gl.h>

namespace {

void requireComplete(const char* label) {
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error(std::string("Failed to create ") + label + " framebuffer");
    }
}

void configureTexture(unsigned int texture) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

} // namespace

GBuffer::~GBuffer() {
    destroy();
}

void GBuffer::resize(int width, int height, int samples) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    int maximumSamples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &maximumSamples);
    samples = std::clamp(samples, 1, std::max(maximumSamples, 1));
    if (framebuffer_ != 0U && width == width_ && height == height_ && samples == samples_) return;

    destroy();
    width_ = width;
    height_ = height;
    samples_ = samples;

    glGenFramebuffers(1, &framebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);

    glGenTextures(1, &albedoTexture_);
    configureTexture(albedoTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, albedoTexture_, 0);

    glGenTextures(1, &normalTexture_);
    configureTexture(normalTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width_, height_, 0, GL_RGBA, GL_FLOAT, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, normalTexture_, 0);

    glGenTextures(1, &materialTexture_);
    configureTexture(materialTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, width_, height_, 0, GL_RG, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, materialTexture_, 0);

    glGenTextures(1, &motionTexture_);
    configureTexture(motionTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width_, height_, 0, GL_RGBA, GL_FLOAT, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, motionTexture_, 0);

    glGenTextures(1, &depthTexture_);
    configureTexture(depthTexture_);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_DEPTH24_STENCIL8,
        width_,
        height_,
        0,
        GL_DEPTH_STENCIL,
        GL_UNSIGNED_INT_24_8,
        nullptr
    );
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_DEPTH_STENCIL_ATTACHMENT,
        GL_TEXTURE_2D,
        depthTexture_,
        0
    );
    const std::array<unsigned int, 4> drawBuffers{
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2,
        GL_COLOR_ATTACHMENT3
    };
    glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
    requireComplete("resolved G-buffer");

    if (samples_ > 1) {
        glGenFramebuffers(1, &multisampleFramebuffer_);
        glBindFramebuffer(GL_FRAMEBUFFER, multisampleFramebuffer_);

        glGenRenderbuffers(1, &multisampleAlbedo_);
        glBindRenderbuffer(GL_RENDERBUFFER, multisampleAlbedo_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA8, width_, height_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, multisampleAlbedo_);

        glGenRenderbuffers(1, &multisampleNormal_);
        glBindRenderbuffer(GL_RENDERBUFFER, multisampleNormal_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA16F, width_, height_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_RENDERBUFFER, multisampleNormal_);

        glGenRenderbuffers(1, &multisampleMaterial_);
        glBindRenderbuffer(GL_RENDERBUFFER, multisampleMaterial_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RG8, width_, height_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_RENDERBUFFER, multisampleMaterial_);

        glGenRenderbuffers(1, &multisampleMotion_);
        glBindRenderbuffer(GL_RENDERBUFFER, multisampleMotion_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA16F, width_, height_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_RENDERBUFFER, multisampleMotion_);

        glGenRenderbuffers(1, &multisampleDepthStencil_);
        glBindRenderbuffer(GL_RENDERBUFFER, multisampleDepthStencil_);
        glRenderbufferStorageMultisample(
            GL_RENDERBUFFER,
            samples_,
            GL_DEPTH24_STENCIL8,
            width_,
            height_
        );
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER,
            GL_DEPTH_STENCIL_ATTACHMENT,
            GL_RENDERBUFFER,
            multisampleDepthStencil_
        );
        glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
        requireComplete("multisample G-buffer");
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GBuffer::bindForGeometry() const {
    glBindFramebuffer(
        GL_FRAMEBUFFER,
        samples_ > 1 ? multisampleFramebuffer_ : framebuffer_
    );
    const std::array<unsigned int, 4> drawBuffers{
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2,
        GL_COLOR_ATTACHMENT3
    };
    glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
}

void GBuffer::resolve() const {
    if (samples_ <= 1) return;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, multisampleFramebuffer_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer_);
    for (unsigned int attachment = 0; attachment < 4U; ++attachment) {
        glReadBuffer(GL_COLOR_ATTACHMENT0 + attachment);
        glDrawBuffer(GL_COLOR_ATTACHMENT0 + attachment);
        glBlitFramebuffer(
            0, 0, width_, height_,
            0, 0, width_, height_,
            GL_COLOR_BUFFER_BIT,
            GL_NEAREST
        );
    }
    glBlitFramebuffer(
        0, 0, width_, height_,
        0, 0, width_, height_,
        GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT,
        GL_NEAREST
    );
}

void GBuffer::bindTextures(
    unsigned int albedoUnit,
    unsigned int normalUnit,
    unsigned int materialUnit,
    unsigned int depthUnit
) const {
    const std::array<std::pair<unsigned int, unsigned int>, 4> bindings{{
        {albedoUnit, albedoTexture_},
        {normalUnit, normalTexture_},
        {materialUnit, materialTexture_},
        {depthUnit, depthTexture_}
    }};
    for (const auto& [unit, texture] : bindings) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, texture);
    }
}

std::size_t GBuffer::estimatedBytes() const {
    if (width_ <= 0 || height_ <= 0) return 0U;
    const std::size_t pixels = static_cast<std::size_t>(width_)
        * static_cast<std::size_t>(height_);
    const std::size_t resolvedBytes = pixels * (4U + 8U + 2U + 8U + 4U);
    const std::size_t multisampleBytes = samples_ > 1
        ? resolvedBytes * static_cast<std::size_t>(samples_)
        : 0U;
    return resolvedBytes + multisampleBytes;
}

void GBuffer::destroy() {
    if (multisampleDepthStencil_ != 0U) glDeleteRenderbuffers(1, &multisampleDepthStencil_);
    if (multisampleMotion_ != 0U) glDeleteRenderbuffers(1, &multisampleMotion_);
    if (multisampleMaterial_ != 0U) glDeleteRenderbuffers(1, &multisampleMaterial_);
    if (multisampleNormal_ != 0U) glDeleteRenderbuffers(1, &multisampleNormal_);
    if (multisampleAlbedo_ != 0U) glDeleteRenderbuffers(1, &multisampleAlbedo_);
    if (depthTexture_ != 0U) glDeleteTextures(1, &depthTexture_);
    if (motionTexture_ != 0U) glDeleteTextures(1, &motionTexture_);
    if (materialTexture_ != 0U) glDeleteTextures(1, &materialTexture_);
    if (normalTexture_ != 0U) glDeleteTextures(1, &normalTexture_);
    if (albedoTexture_ != 0U) glDeleteTextures(1, &albedoTexture_);
    if (multisampleFramebuffer_ != 0U) glDeleteFramebuffers(1, &multisampleFramebuffer_);
    if (framebuffer_ != 0U) glDeleteFramebuffers(1, &framebuffer_);
    framebuffer_ = 0U;
    multisampleFramebuffer_ = 0U;
    albedoTexture_ = 0U;
    normalTexture_ = 0U;
    materialTexture_ = 0U;
    motionTexture_ = 0U;
    depthTexture_ = 0U;
    multisampleAlbedo_ = 0U;
    multisampleNormal_ = 0U;
    multisampleMaterial_ = 0U;
    multisampleMotion_ = 0U;
    multisampleDepthStencil_ = 0U;
    width_ = 0;
    height_ = 0;
    samples_ = 1;
}
