#include "render/SsaoRenderer.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include <glad/gl.h>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include "render/GBuffer.h"
#include "render/Shader.h"

namespace {

void configureOcclusionTexture(unsigned int texture, int width, int height) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, width, height, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

float radicalInverse(unsigned int bits) {
    bits = (bits << 16U) | (bits >> 16U);
    bits = ((bits & 0x55555555U) << 1U) | ((bits & 0xAAAAAAAAU) >> 1U);
    bits = ((bits & 0x33333333U) << 2U) | ((bits & 0xCCCCCCCCU) >> 2U);
    bits = ((bits & 0x0F0F0F0FU) << 4U) | ((bits & 0xF0F0F0F0U) >> 4U);
    bits = ((bits & 0x00FF00FFU) << 8U) | ((bits & 0xFF00FF00U) >> 8U);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

} // namespace

SsaoRenderer::SsaoRenderer(const std::filesystem::path& shaderDirectory)
    : ssaoShader_(std::make_unique<Shader>(
        shaderDirectory / "fullscreen.vert",
        shaderDirectory / "ssao.frag"
    )),
      blurShader_(std::make_unique<Shader>(
        shaderDirectory / "fullscreen.vert",
        shaderDirectory / "ssao_blur.frag"
    )) {
    glGenVertexArrays(1, &vertexArray_);
    glGenFramebuffers(1, &rawFramebuffer_);
    glGenFramebuffers(1, &blurFramebuffer_);
    glGenTextures(1, &rawTexture_);
    glGenTextures(1, &blurredTexture_);

    for (std::size_t index = 0; index < kernel_.size(); ++index) {
        const float u = (static_cast<float>(index) + 0.5f)
            / static_cast<float>(kernel_.size());
        const float phi = 6.28318530718f * radicalInverse(static_cast<unsigned int>(index + 1U));
        const float z = std::max(0.08f, u);
        const float radial = std::sqrt(std::max(1.0f - z * z, 0.0f));
        const float scale = 0.1f + 0.9f * u * u;
        kernel_[index] = glm::normalize(glm::vec3(
            std::cos(phi) * radial,
            std::sin(phi) * radial,
            z
        )) * scale;
    }
}

SsaoRenderer::~SsaoRenderer() {
    glDeleteTextures(1, &blurredTexture_);
    glDeleteTextures(1, &rawTexture_);
    glDeleteFramebuffers(1, &blurFramebuffer_);
    glDeleteFramebuffers(1, &rawFramebuffer_);
    if (vertexArray_ != 0U) glDeleteVertexArrays(1, &vertexArray_);
}

void SsaoRenderer::resize(int width, int height) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (width == width_ && height == height_) return;
    width_ = width;
    height_ = height;

    configureOcclusionTexture(rawTexture_, width_, height_);
    glBindFramebuffer(GL_FRAMEBUFFER, rawFramebuffer_);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rawTexture_, 0
    );
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Failed to create SSAO framebuffer");
    }

    configureOcclusionTexture(blurredTexture_, width_, height_);
    glBindFramebuffer(GL_FRAMEBUFFER, blurFramebuffer_);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, blurredTexture_, 0
    );
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Failed to create SSAO blur framebuffer");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void SsaoRenderer::drawFullscreen() const {
    glBindVertexArray(vertexArray_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void SsaoRenderer::render(
    const GBuffer& gBuffer,
    const glm::mat4& view,
    const glm::mat4& projection,
    float radius,
    float bias,
    float strength,
    int width,
    int height
) {
    resize(width, height);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    glBindFramebuffer(GL_FRAMEBUFFER, rawFramebuffer_);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ssaoShader_->use();
    ssaoShader_->setInt("uNormal", 0);
    ssaoShader_->setInt("uDepth", 1);
    ssaoShader_->setMat4("uView", view);
    ssaoShader_->setMat4("uProjection", projection);
    ssaoShader_->setMat4("uInverseProjection", glm::inverse(projection));
    ssaoShader_->setFloat("uRadius", std::max(radius, 0.01f));
    ssaoShader_->setFloat("uBias", std::max(bias, 0.0f));
    ssaoShader_->setFloat("uStrength", std::max(strength, 0.0f));
    for (std::size_t index = 0; index < kernel_.size(); ++index) {
        const std::string uniform = "uKernel[" + std::to_string(index) + "]";
        ssaoShader_->setVec3(uniform.c_str(), kernel_[index]);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gBuffer.normalTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gBuffer.depthTexture());
    drawFullscreen();

    glBindFramebuffer(GL_FRAMEBUFFER, blurFramebuffer_);
    blurShader_->use();
    blurShader_->setInt("uOcclusion", 0);
    blurShader_->setInt("uDepth", 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rawTexture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gBuffer.depthTexture());
    drawFullscreen();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SsaoRenderer::bindTexture(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, blurredTexture_);
}

std::size_t SsaoRenderer::estimatedBytes() const {
    if (width_ <= 0 || height_ <= 0) return 0U;
    return static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4U;
}
