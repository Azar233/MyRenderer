#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>

#include <glm/mat4x4.hpp>

class Shader;

class EnvironmentMap {
public:
    EnvironmentMap(
        const std::filesystem::path& vertexShaderPath,
        const std::filesystem::path& fragmentShaderPath
    );
    ~EnvironmentMap();

    EnvironmentMap(const EnvironmentMap&) = delete;
    EnvironmentMap& operator=(const EnvironmentMap&) = delete;

    void bind(unsigned int unit) const;
    void draw(const glm::mat4& inverseViewProjection, const glm::vec3& cameraPosition, float intensity) const;
    int maximumMipLevel() const { return maximumMipLevel_; }
    std::size_t estimatedBytes() const;

private:
    std::unique_ptr<Shader> shader_;
    unsigned int texture_{0};
    unsigned int vertexArray_{0};
    int maximumMipLevel_{0};
    int faceSize_{64};
};
