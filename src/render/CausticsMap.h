#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>

#include <glm/vec3.hpp>

class Shader;

class CausticsMap {
public:
    CausticsMap(const std::filesystem::path& shaderDirectory, int resolution = 1024);
    ~CausticsMap();

    CausticsMap(const CausticsMap&) = delete;
    CausticsMap& operator=(const CausticsMap&) = delete;

    void bindRawForWriting() const;
    void drawProjector(
        float strength,
        float scale,
        const glm::vec3& direction,
        float sharpness,
        float animationPhase
    ) const;
    void filter(float sharpness) const;
    void bindTexture(unsigned int unit) const;
    Shader& lightSpaceShader() const;

    int resolution() const { return resolution_; }
    std::size_t estimatedBytes() const {
        return static_cast<std::size_t>(resolution_)
            * static_cast<std::size_t>(resolution_) * 8U * 2U;
    }

private:
    void drawFullscreen() const;

    std::unique_ptr<Shader> projectorShader_;
    std::unique_ptr<Shader> lightSpaceShader_;
    std::unique_ptr<Shader> filterShader_;
    unsigned int framebuffers_[2]{};
    unsigned int textures_[2]{};
    unsigned int vertexArray_{0};
    int resolution_{1024};
};
