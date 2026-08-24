#include "render/CausticsMap.h"

#include <stdexcept>

#include <glad/gl.h>

#include "render/Shader.h"

CausticsMap::CausticsMap(const std::filesystem::path& shaderDirectory, int resolution)
    : projectorShader_(std::make_unique<Shader>(
          shaderDirectory / "fullscreen.vert",
          shaderDirectory / "caustics_projector.frag"
      )),
      lightSpaceShader_(std::make_unique<Shader>(
          shaderDirectory / "caustics_lightspace.vert",
          shaderDirectory / "caustics_lightspace.geom",
          shaderDirectory / "caustics_lightspace.frag"
      )),
      filterShader_(std::make_unique<Shader>(
          shaderDirectory / "fullscreen.vert",
          shaderDirectory / "caustics_filter.frag"
      )),
      resolution_(resolution) {
    glGenVertexArrays(1, &vertexArray_);
    glGenFramebuffers(2, framebuffers_);
    glGenTextures(2, textures_);
    for (int index = 0; index < 2; ++index) {
        glBindTexture(GL_TEXTURE_2D, textures_[index]);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA16F, resolution_, resolution_, 0,
            GL_RGBA, GL_FLOAT, nullptr
        );
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const float border[] = {0.0f, 0.0f, 0.0f, 0.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffers_[index]);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures_[index], 0
        );
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("Failed to create caustics framebuffer");
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

CausticsMap::~CausticsMap() {
    glDeleteTextures(2, textures_);
    glDeleteFramebuffers(2, framebuffers_);
    if (vertexArray_ != 0U) glDeleteVertexArrays(1, &vertexArray_);
}

void CausticsMap::bindRawForWriting() const {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffers_[0]);
}

void CausticsMap::drawFullscreen() const {
    glBindVertexArray(vertexArray_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void CausticsMap::drawProjector(
    float strength,
    float scale,
    const glm::vec3& direction,
    float sharpness,
    float animationPhase
) const {
    projectorShader_->use();
    projectorShader_->setFloat("uStrength", strength);
    projectorShader_->setFloat("uScale", scale);
    projectorShader_->setVec3("uDirection", direction);
    projectorShader_->setFloat("uSharpness", sharpness);
    projectorShader_->setFloat("uAnimationPhase", animationPhase);
    drawFullscreen();
}

void CausticsMap::filter(float sharpness) const {
    glDisable(GL_BLEND);
    filterShader_->use();
    filterShader_->setInt("uImage", 0);
    filterShader_->setFloat("uSharpness", sharpness);
    glActiveTexture(GL_TEXTURE0);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffers_[1]);
    glBindTexture(GL_TEXTURE_2D, textures_[0]);
    filterShader_->setBool("uHorizontal", true);
    drawFullscreen();

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffers_[0]);
    glBindTexture(GL_TEXTURE_2D, textures_[1]);
    filterShader_->setBool("uHorizontal", false);
    drawFullscreen();
}

void CausticsMap::bindTexture(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, textures_[0]);
}

Shader& CausticsMap::lightSpaceShader() const {
    return *lightSpaceShader_;
}
