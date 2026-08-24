#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "optics/PrismOptics.h"

class Shader;

class SpectralBeamRenderer {
public:
    SpectralBeamRenderer(
        const std::filesystem::path& vertexShaderPath,
        const std::filesystem::path& fragmentShaderPath
    );
    ~SpectralBeamRenderer();

    SpectralBeamRenderer(const SpectralBeamRenderer&) = delete;
    SpectralBeamRenderer& operator=(const SpectralBeamRenderer&) = delete;

    std::size_t draw(
        const SpectralBeamData& spectrum,
        const glm::vec3& cameraPosition,
        const glm::mat4& view,
        const glm::mat4& projection,
        float outputLength,
        float width,
        float intensity,
        float edgeSoftness
    ) const;

private:
    std::unique_ptr<Shader> shader_;
    unsigned int vao_{0};
    unsigned int vbo_{0};
};
