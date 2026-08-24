#include "render/OpticalPathDebugRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <glad/gl.h>
#include <glm/common.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "render/Shader.h"

namespace {

struct DebugVertex {
    glm::vec3 position{0.0f};
    glm::vec4 color{1.0f};
};

glm::vec3 toWorld(const glm::vec2& point, float z) {
    return glm::vec3(point, z);
}

void appendLine(
    std::vector<DebugVertex>& vertices,
    const glm::vec3& from,
    const glm::vec3& to,
    const glm::vec4& color
) {
    vertices.push_back(DebugVertex{from, color});
    vertices.push_back(DebugVertex{to, color});
}

glm::vec4 sampleColor(const PrismSpectralSample& sample) {
    const glm::vec3 rgb = glm::clamp(sample.linearRgb, glm::vec3(0.0f), glm::vec3(1.0f));
    const float alpha = 0.35f + 0.65f * glm::clamp(sample.transmittance, 0.0f, 1.0f);
    return glm::vec4(rgb, alpha);
}

} // namespace

OpticalPathDebugRenderer::OpticalPathDebugRenderer(
    const std::filesystem::path& vertexShaderPath,
    const std::filesystem::path& fragmentShaderPath
) : shader_(std::make_unique<Shader>(vertexShaderPath, fragmentShaderPath)) {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
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

OpticalPathDebugRenderer::~OpticalPathDebugRenderer() {
    if (vbo_ != 0U) glDeleteBuffers(1, &vbo_);
    if (vao_ != 0U) glDeleteVertexArrays(1, &vao_);
}

std::size_t OpticalPathDebugRenderer::draw(
    const SpectralBeamData& spectrum,
    const glm::mat4& view,
    const glm::mat4& projection,
    float outputLength
) const {
    const PrismSpectralSample* centerSample = nullptr;
    std::vector<DebugVertex> lines;
    std::vector<DebugVertex> points;
    for (const PrismSpectralSample& sample : spectrum.samples) {
        if (!sample.path.valid) continue;
        if (centerSample == nullptr
            || std::abs(sample.wavelengthNanometers - 550.0f)
                < std::abs(centerSample->wavelengthNanometers - 550.0f)) {
            centerSample = &sample;
        }

        const float z = 0.018f + 0.0008f * static_cast<float>(points.size());
        const glm::vec4 color = sample.path.totalInternalReflection
            ? glm::vec4(1.0f, 0.42f, 0.05f, 0.96f)
            : sampleColor(sample);
        const glm::vec3 entry = toWorld(sample.path.entryPoint, z);
        const glm::vec3 exit = toWorld(sample.path.exitPoint, z);
        appendLine(lines, entry, exit, color);
        appendLine(
            lines,
            exit,
            exit + toWorld(sample.path.exitDirection, 0.0f) * std::max(outputLength, 0.1f),
            color
        );
        points.push_back(DebugVertex{entry, color});
        points.push_back(DebugVertex{exit, color});
    }

    if (centerSample == nullptr) return 0U;

    constexpr float guideZ = 0.045f;
    const PrismOpticalPath& center = centerSample->path;
    appendLine(
        lines,
        toWorld(spectrum.incidentRay.origin, guideZ),
        toWorld(center.entryPoint, guideZ),
        glm::vec4(1.0f, 1.0f, 1.0f, 0.98f)
    );
    constexpr float normalLength = 0.28f;
    appendLine(
        lines,
        toWorld(center.entryPoint, guideZ),
        toWorld(center.entryPoint + center.entryNormal * normalLength, guideZ),
        glm::vec4(1.0f, 0.88f, 0.16f, 1.0f)
    );
    appendLine(
        lines,
        toWorld(center.exitPoint, guideZ),
        toWorld(center.exitPoint + center.exitNormal * normalLength, guideZ),
        glm::vec4(1.0f, 0.25f, 0.82f, 1.0f)
    );

    const std::size_t lineVertexCount = lines.size();
    lines.insert(lines.end(), points.begin(), points.end());
    shader_->use();
    shader_->setMat4("uView", view);
    shader_->setMat4("uProjection", projection);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(lines.size() * sizeof(DebugVertex)),
        lines.data(),
        GL_DYNAMIC_DRAW
    );
    glBindVertexArray(vao_);
    glLineWidth(2.0f);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineVertexCount));
    glPointSize(6.0f);
    glDrawArrays(
        GL_POINTS,
        static_cast<GLint>(lineVertexCount),
        static_cast<GLsizei>(points.size())
    );
    glPointSize(1.0f);
    glLineWidth(1.0f);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return 2U;
}
