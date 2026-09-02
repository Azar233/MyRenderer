#include "render/PostProcessor.h"

#include <algorithm>

#include <glad/gl.h>

#include "render/RenderTarget.h"
#include "render/Shader.h"

namespace {

void configureTexture(
    unsigned int texture,
    int internalFormat,
    int format,
    int type,
    int width,
    int height,
    int filtering
) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filtering);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtering);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

} // namespace

PostProcessor::PostProcessor(
    const std::filesystem::path& fullscreenVertex,
    const std::filesystem::path& extractFragment,
    const std::filesystem::path& blurFragment,
    const std::filesystem::path& compositeFragment,
    const std::filesystem::path& temporalFragment
) : extractShader_(std::make_unique<Shader>(fullscreenVertex, extractFragment)),
    blurShader_(std::make_unique<Shader>(fullscreenVertex, blurFragment)),
    compositeShader_(std::make_unique<Shader>(fullscreenVertex, compositeFragment)),
    temporalShader_(std::make_unique<Shader>(fullscreenVertex, temporalFragment)) {
    glGenVertexArrays(1, &vertexArray_);
    glGenFramebuffers(2, framebuffers_);
    glGenTextures(2, textures_);
    glGenFramebuffers(2, temporalFramebuffers_);
    glGenTextures(2, historyColorTextures_);
    glGenTextures(2, historyDepthTextures_);
    glGenTextures(2, motionTextures_);
}

PostProcessor::~PostProcessor() {
    glDeleteTextures(2, motionTextures_);
    glDeleteTextures(2, historyDepthTextures_);
    glDeleteTextures(2, historyColorTextures_);
    glDeleteFramebuffers(2, temporalFramebuffers_);
    glDeleteTextures(2, textures_);
    glDeleteFramebuffers(2, framebuffers_);
    if (vertexArray_ != 0U) glDeleteVertexArrays(1, &vertexArray_);
}

std::size_t PostProcessor::estimatedBytes() const {
    if (width_ <= 0 || height_ <= 0) return 0U;
    return static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_)
        * (8U * 2U + (8U + 4U + 4U) * 2U);
}

void PostProcessor::resize(int width, int height) {
    if (width == width_ && height == height_) return;
    width_ = width;
    height_ = height;
    historyValid_ = false;
    for (int index = 0; index < 2; ++index) {
        glBindTexture(GL_TEXTURE_2D, textures_[index]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width_, height_, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffers_[index]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures_[index], 0);

        configureTexture(
            historyColorTextures_[index], GL_RGBA16F, GL_RGBA, GL_FLOAT,
            width_, height_, GL_LINEAR
        );
        configureTexture(
            historyDepthTextures_[index], GL_R32F, GL_RED, GL_FLOAT,
            width_, height_, GL_NEAREST
        );
        configureTexture(
            motionTextures_[index], GL_RG16F, GL_RG, GL_FLOAT,
            width_, height_, GL_LINEAR
        );
        glBindFramebuffer(GL_FRAMEBUFFER, temporalFramebuffers_[index]);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
            historyColorTextures_[index], 0
        );
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
            historyDepthTextures_[index], 0
        );
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D,
            motionTextures_[index], 0
        );
        constexpr unsigned int attachments[]{
            GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2
        };
        glDrawBuffers(3, attachments);
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

    unsigned int sceneTexture = target.hdrColorTexture();
    unsigned int motionTexture = 0U;
    if (settings.temporalAa) {
        if (settings.resetTemporalHistory) historyValid_ = false;
        const int destination = 1 - historyIndex_;
        glBindFramebuffer(GL_FRAMEBUFFER, temporalFramebuffers_[destination]);
        temporalShader_->use();
        temporalShader_->setInt("uCurrentColor", 0);
        temporalShader_->setInt("uCurrentDepth", 1);
        temporalShader_->setInt("uHistoryColor", 2);
        temporalShader_->setInt("uHistoryDepth", 3);
        temporalShader_->setInt("uObjectMotion", 4);
        temporalShader_->setBool("uObjectMotionAvailable", settings.objectMotionTexture != 0U);
        temporalShader_->setBool("uHistoryValid", historyValid_);
        temporalShader_->setFloat(
            "uHistoryWeight",
            std::clamp(settings.temporalHistoryWeight, 0.0f, 0.98f)
        );
        temporalShader_->setMat4(
            "uInverseCurrentViewProjection",
            settings.inverseCurrentViewProjection
        );
        temporalShader_->setMat4("uPreviousViewProjection", settings.previousViewProjection);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, target.hdrColorTexture());
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, settings.depthTexture);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, historyColorTextures_[historyIndex_]);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, historyDepthTextures_[historyIndex_]);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, settings.objectMotionTexture);
        drawFullscreen();
        historyIndex_ = destination;
        historyValid_ = true;
        sceneTexture = historyColorTextures_[historyIndex_];
        motionTexture = motionTextures_[historyIndex_];
    } else {
        historyValid_ = false;
    }

    int bloomTextureIndex = 0;
    if (settings.bloom) {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffers_[0]);
        extractShader_->use();
        extractShader_->setInt("uScene", 0);
        extractShader_->setFloat("uThreshold", settings.bloomThreshold);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneTexture);
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
    compositeShader_->setInt("uMotion", 2);
    compositeShader_->setInt("uTemporalDebugView", settings.temporalDebugView);
    compositeShader_->setBool("uToneMapping", settings.toneMapping);
    compositeShader_->setBool("uBloomEnabled", settings.bloom);
    compositeShader_->setBool("uEncodeSrgb", settings.encodeSrgb);
    compositeShader_->setFloat("uExposure", settings.exposure);
    compositeShader_->setFloat("uBloomIntensity", settings.bloomIntensity);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textures_[bloomTextureIndex]);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, motionTexture);
    drawFullscreen();
    target.unbind();
}
