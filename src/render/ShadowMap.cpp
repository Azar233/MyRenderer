#include "render/ShadowMap.h"

#include <stdexcept>

#include <glad/gl.h>

ShadowMap::ShadowMap(int resolution) : resolution_(resolution) {
    glGenFramebuffers(1, &framebuffer_);
    glGenTextures(1, &depthTexture_);
    glBindTexture(GL_TEXTURE_2D, depthTexture_);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, resolution_, resolution_, 0,
        GL_DEPTH_COMPONENT, GL_FLOAT, nullptr
    );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Failed to create shadow framebuffer");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

ShadowMap::~ShadowMap() {
    if (depthTexture_ != 0U) glDeleteTextures(1, &depthTexture_);
    if (framebuffer_ != 0U) glDeleteFramebuffers(1, &framebuffer_);
}

void ShadowMap::bindForWriting() const {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
}

void ShadowMap::bindTexture(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, depthTexture_);
}
