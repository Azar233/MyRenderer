#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>

#include <glm/mat4x4.hpp>

#include "optics/PrismOptics.h"

class Shader;

class OpticalPathDebugRenderer {
public:
    OpticalPathDebugRenderer(
        const std::filesystem::path& vertexShaderPath,
        const std::filesystem::path& fragmentShaderPath
    );
    ~OpticalPathDebugRenderer();

    OpticalPathDebugRenderer(const OpticalPathDebugRenderer&) = delete;
    OpticalPathDebugRenderer& operator=(const OpticalPathDebugRenderer&) = delete;

    std::size_t draw(
        const SpectralBeamData& spectrum,
        const glm::mat4& view,
        const glm::mat4& projection,
        float outputLength
    ) const;

private:
    std::unique_ptr<Shader> shader_;
    unsigned int vao_{0};
    unsigned int vbo_{0};
};
