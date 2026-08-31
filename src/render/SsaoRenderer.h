#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class GBuffer;
class Shader;

class SsaoRenderer {
public:
    explicit SsaoRenderer(const std::filesystem::path& shaderDirectory);
    ~SsaoRenderer();

    SsaoRenderer(const SsaoRenderer&) = delete;
    SsaoRenderer& operator=(const SsaoRenderer&) = delete;

    void render(
        const GBuffer& gBuffer,
        const glm::mat4& view,
        const glm::mat4& projection,
        float radius,
        float bias,
        float strength,
        int width,
        int height
    );

    void bindTexture(unsigned int unit) const;
    unsigned int texture() const { return blurredTexture_; }
    std::size_t estimatedBytes() const;

private:
    void resize(int width, int height);
    void drawFullscreen() const;

    std::unique_ptr<Shader> ssaoShader_;
    std::unique_ptr<Shader> blurShader_;
    unsigned int vertexArray_{0};
    unsigned int rawFramebuffer_{0};
    unsigned int blurFramebuffer_{0};
    unsigned int rawTexture_{0};
    unsigned int blurredTexture_{0};
    std::array<glm::vec3, 16> kernel_{};
    int width_{0};
    int height_{0};
};
