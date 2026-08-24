#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

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
    void drawPrismPath(
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& source,
        const glm::vec3& entry,
        const glm::vec3& exit,
        const glm::vec3& outputEnd,
        bool totalInternalReflection
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
