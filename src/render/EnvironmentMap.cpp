#include "render/EnvironmentMap.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glad/gl.h>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include "render/Shader.h"

namespace {

glm::vec3 faceDirection(int face, float u, float v) {
    switch (face) {
    case 0: return glm::normalize(glm::vec3(1.0f, -v, -u));
    case 1: return glm::normalize(glm::vec3(-1.0f, -v, u));
    case 2: return glm::normalize(glm::vec3(u, 1.0f, v));
    case 3: return glm::normalize(glm::vec3(u, -1.0f, -v));
    case 4: return glm::normalize(glm::vec3(u, -v, 1.0f));
    default: return glm::normalize(glm::vec3(-u, -v, -1.0f));
    }
}

glm::vec3 skyRadiance(const glm::vec3& direction) {
    const float horizon = std::clamp(direction.y * 0.5f + 0.5f, 0.0f, 1.0f);
    const glm::vec3 ground(0.035f, 0.028f, 0.025f);
    const glm::vec3 horizonColor(0.42f, 0.50f, 0.62f);
    const glm::vec3 zenith(0.055f, 0.13f, 0.32f);
    glm::vec3 color = direction.y < 0.0f
        ? ground * (0.6f + 0.4f * horizon)
        : horizonColor * (1.0f - horizon) + zenith * horizon;
    const glm::vec3 sunDirection = glm::normalize(glm::vec3(0.42f, 0.72f, 0.32f));
    const float sun = std::pow(std::max(glm::dot(direction, sunDirection), 0.0f), 512.0f);
    return color + glm::vec3(9.0f, 6.5f, 3.5f) * sun;
}

} // namespace

EnvironmentMap::EnvironmentMap(
    const std::filesystem::path& vertexShaderPath,
    const std::filesystem::path& fragmentShaderPath
) : shader_(std::make_unique<Shader>(vertexShaderPath, fragmentShaderPath)) {
    const int size = faceSize_;
    maximumMipLevel_ = static_cast<int>(std::log2(size));
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture_);
    std::vector<float> pixels(static_cast<std::size_t>(size * size * 3));
    for (int face = 0; face < 6; ++face) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const float u = (2.0f * (static_cast<float>(x) + 0.5f) / size) - 1.0f;
                const float v = (2.0f * (static_cast<float>(y) + 0.5f) / size) - 1.0f;
                const glm::vec3 color = skyRadiance(faceDirection(face, u, v));
                const std::size_t offset = static_cast<std::size_t>((y * size + x) * 3);
                pixels[offset] = color.r;
                pixels[offset + 1U] = color.g;
                pixels[offset + 2U] = color.b;
            }
        }
        glTexImage2D(
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
            0,
            GL_RGB16F,
            size,
            size,
            0,
            GL_RGB,
            GL_FLOAT,
            pixels.data()
        );
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glGenVertexArrays(1, &vertexArray_);
}

std::size_t EnvironmentMap::estimatedBytes() const {
    std::size_t pixels = 0U;
    int size = faceSize_;
    for (int level = 0; level <= maximumMipLevel_; ++level) {
        pixels += static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 6U;
        size = std::max(size / 2, 1);
    }
    return pixels * 6U; // RGB16F
}

EnvironmentMap::~EnvironmentMap() {
    if (vertexArray_ != 0U) glDeleteVertexArrays(1, &vertexArray_);
    if (texture_ != 0U) glDeleteTextures(1, &texture_);
}

void EnvironmentMap::bind(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture_);
}

void EnvironmentMap::draw(
    const glm::mat4& inverseViewProjection,
    const glm::vec3& cameraPosition,
    float intensity
) const {
    shader_->use();
    shader_->setMat4("uInverseViewProjection", inverseViewProjection);
    shader_->setVec3("uCameraPosition", cameraPosition);
    shader_->setFloat("uEnvironmentIntensity", intensity);
    shader_->setInt("uEnvironmentMap", 0);
    bind(0U);
    glBindVertexArray(vertexArray_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}
