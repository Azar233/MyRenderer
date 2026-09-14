#include "render/DebugGrid.h"

#include <cstddef>
#include <vector>

#include <glad/gl.h>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "render/Shader.h"

namespace {

struct DebugVertex {
    glm::vec3 position{0.0f};
    glm::vec4 color{1.0f};
};

void appendLine(
    std::vector<DebugVertex>& vertices,
    const glm::vec3& from,
    const glm::vec3& to,
    const glm::vec4& color
) {
    vertices.push_back(DebugVertex{from, color});
    vertices.push_back(DebugVertex{to, color});
}

void appendPositiveAxis(
    std::vector<DebugVertex>& vertices,
    const glm::vec3& origin,
    const glm::vec3& direction,
    const glm::vec3& arrowDirectionA,
    const glm::vec3& arrowDirectionB,
    const glm::vec4& color
) {
    constexpr float length = 2.25f;
    constexpr float arrowLength = 0.30f;
    constexpr float arrowWidth = 0.14f;
    const glm::vec3 endpoint = origin + direction * length;
    appendLine(vertices, origin, endpoint, color);
    appendLine(
        vertices,
        endpoint,
        endpoint - direction * arrowLength + arrowDirectionA * arrowWidth,
        color
    );
    appendLine(
        vertices,
        endpoint,
        endpoint - direction * arrowLength - arrowDirectionA * arrowWidth,
        color
    );
    appendLine(
        vertices,
        endpoint,
        endpoint - direction * arrowLength + arrowDirectionB * arrowWidth,
        color
    );
    appendLine(
        vertices,
        endpoint,
        endpoint - direction * arrowLength - arrowDirectionB * arrowWidth,
        color
    );
}

} // namespace

DebugGrid::DebugGrid(
    const std::filesystem::path& vertexShaderPath,
    const std::filesystem::path& fragmentShaderPath
) : shader_(std::make_unique<Shader>(vertexShaderPath, fragmentShaderPath)) {
    infiniteShader_ = std::make_unique<Shader>(vertexShaderPath.parent_path() / "fullscreen.vert",
        fragmentShaderPath.parent_path() / "infinite_grid.frag");
    std::vector<DebugVertex> vertices;
    constexpr float gridY = 0.0f;
    axesFirstVertex_ = vertices.size();
    constexpr float negativeLength = 2.25f;
    const glm::vec3 axesOrigin(0.0f, gridY, 0.0f);
    appendLine(vertices, axesOrigin, axesOrigin + glm::vec3(-negativeLength, 0.0f, 0.0f), {1.00f, 0.08f, 0.14f, 0.65f});
    appendLine(vertices, axesOrigin, axesOrigin + glm::vec3(0.0f, -negativeLength, 0.0f), {0.10f, 1.00f, 0.22f, 0.65f});
    appendLine(vertices, axesOrigin, axesOrigin + glm::vec3(0.0f, 0.0f, -negativeLength), {0.08f, 0.34f, 1.00f, 0.65f});
    appendPositiveAxis(vertices, axesOrigin, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.00f, 0.08f, 0.14f, 1.0f});
    appendPositiveAxis(vertices, axesOrigin, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.10f, 1.00f, 0.22f, 1.0f});
    appendPositiveAxis(vertices, axesOrigin, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.08f, 0.34f, 1.00f, 1.0f});
    axesVertexCount_ = vertices.size() - axesFirstVertex_;

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(DebugVertex)),
        vertices.data(),
        GL_STATIC_DRAW
    );
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(DebugVertex)),
        reinterpret_cast<void*>(offsetof(DebugVertex, position))
    );
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        4,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(DebugVertex)),
        reinterpret_cast<void*>(offsetof(DebugVertex, color))
    );
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

}

DebugGrid::~DebugGrid() {
    if (vbo_ != 0U) {
        glDeleteBuffers(1, &vbo_);
    }
    if (vao_ != 0U) {
        glDeleteVertexArrays(1, &vao_);
    }
}

void DebugGrid::draw(
    const glm::mat4& view,
    const glm::mat4& projection,
    bool showGrid,
    bool showAxes
) const {
    if (!showGrid && !showAxes) {
        return;
    }

    glBindVertexArray(vao_);
    if (showGrid) {
        infiniteShader_->use();
        infiniteShader_->setMat4("uViewProjection", projection * view);
        infiniteShader_->setMat4("uInverseViewProjection", glm::inverse(projection * view));
        infiniteShader_->setVec3("uCamera", glm::vec3(glm::inverse(view)[3]));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    shader_->use();
    shader_->setMat4("uView", view);
    shader_->setMat4("uProjection", projection);
    if (showAxes) {
        glLineWidth(3.0f);
        glDrawArrays(
            GL_LINES,
            static_cast<GLint>(axesFirstVertex_),
            static_cast<GLsizei>(axesVertexCount_)
        );
        glLineWidth(1.0f);
    }
    glBindVertexArray(0);
}
