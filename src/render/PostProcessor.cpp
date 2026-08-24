#include "render/PostProcessor.h"

#include <glad/gl.h>

#include "render/RenderTarget.h"
#include "render/Shader.h"

PostProcessor::PostProcessor(
    const std::filesystem::path& fullscreenVertex,
    const std::filesystem::path& extractFragment,
    const std::filesystem::path& blurFragment,
    const std::filesystem::path& compositeFragment
) : extractShader_(std::make_unique<Shader>(fullscreenVertex, extractFragment)),
    blurShader_(std::make_unique<Shader>(fullscreenVertex, blurFragment)),
    compositeShader_(std::make_unique<Shader>(fullscreenVertex, compositeFragment)) {
    glGenVertexArrays(1, &vertexArray_);
    glGenFramebuffers(2, framebuffers_);
    glGenTextures(2, textures_);
}

PostProcessor::~PostProcessor() {
    glDeleteTextures(2, textures_);
    glDeleteFramebuffers(2, framebuffers_);
    if (vertexArray_ != 0U) glDeleteVertexArrays(1, &vertexArray_);
}

std::size_t PostProcessor::estimatedBytes() const {
    if (width_ <= 0 || height_ <= 0) return 0U;
    return static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_)
        * 8U * 2U;
}

void PostProcessor::resize(int width, int height) {
    if (width == width_ && height == height_) return;
    width_ = width;
    height_ = height;
    for (int index = 0; index < 2; ++index) {
        glBindTexture(GL_TEXTURE_2D, textures_[index]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width_, height_, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffers_[index]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures_[index], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PostProcessor::drawFullscreen() const {
    glBindVertexArray(vertexArray_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void PostProcessor::process(RenderTarget& target, const PostProcessSettings& settings) {
    resize(target.width(), target.height());
    glViewport(0, 0, target.width(), target.height());
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    int bloomTextureIndex = 0;
    if (settings.bloom) {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffers_[0]);
        extractShader_->use();
        extractShader_->setInt("uScene", 0);
        extractShader_->setFloat("uThreshold", settings.bloomThreshold);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, target.hdrColorTexture());
        drawFullscreen();

        bool horizontal = true;
        for (int iteration = 0; iteration < 8; ++iteration) {
            const int destination = horizontal ? 1 : 0;
            const int source = horizontal ? 0 : 1;
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffers_[destination]);
            blurShader_->use();
            blurShader_->setInt("uImage", 0);
            blurShader_->setBool("uHorizontal", horizontal);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, textures_[source]);
            drawFullscreen();
            bloomTextureIndex = destination;
            horizontal = !horizontal;
        }
    }

    target.bindFinal();
    compositeShader_->use();
    compositeShader_->setInt("uScene", 0);
    compositeShader_->setInt("uBloom", 1);
    compositeShader_->setBool("uToneMapping", settings.toneMapping);
    compositeShader_->setBool("uBloomEnabled", settings.bloom);
    compositeShader_->setFloat("uExposure", settings.exposure);
    compositeShader_->setFloat("uBloomIntensity", settings.bloomIntensity);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, target.hdrColorTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textures_[bloomTextureIndex]);
    drawFullscreen();
    target.unbind();
}
