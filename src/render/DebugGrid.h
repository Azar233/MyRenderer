#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "optics/PrismOptics.h"

class Shader;

class DebugGrid {
public:
    DebugGrid(
        const std::filesystem::path& vertexShaderPath,
        const std::filesystem::path& fragmentShaderPath
    );
    ~DebugGrid();

    DebugGrid(const DebugGrid&) = delete;
    DebugGrid& operator=(const DebugGrid&) = delete;

    void draw(
        const glm::mat4& view,
        const glm::mat4& projection,
        bool showGrid,
        bool showAxes
    ) const;
    void drawPrismSpectrum(
        const glm::mat4& view,
        const glm::mat4& projection,
        const SpectralBeamData& spectrum,
        float outputLength
    ) const;

private:
    std::unique_ptr<Shader> shader_;
    unsigned int vao_{0};
    unsigned int vbo_{0};
    std::size_t gridVertexCount_{0};
    std::size_t axesFirstVertex_{0};
    std::size_t axesVertexCount_{0};
    unsigned int prismPathVao_{0};
    unsigned int prismPathVbo_{0};
};
