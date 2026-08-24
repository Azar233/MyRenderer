#include "render/SpectralBeamRenderer.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

#include <glad/gl.h>

#include "optics/SpectralBeamMesh.h"
#include "render/Shader.h"

namespace {

const char* groupLabel(SpectralBeamGroup group) {
    switch (group) {
    case SpectralBeamGroup::Incident: return "Incident Beam";
    case SpectralBeamGroup::Internal: return "Internal Beam";
    case SpectralBeamGroup::Exit: return "Exit Beam";
    default: return "Spectral Beam";
    }
}

} // namespace

SpectralBeamRenderer::SpectralBeamRenderer(
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
        static_cast<GLsizei>(sizeof(SpectralBeamVertex)),
        reinterpret_cast<void*>(offsetof(SpectralBeamVertex, position))
    );
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(SpectralBeamVertex)),
        reinterpret_cast<void*>(offsetof(SpectralBeamVertex, linearColor))
    );
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2,
        1,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(SpectralBeamVertex)),
        reinterpret_cast<void*>(offsetof(SpectralBeamVertex, edgeCoordinate))
    );
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

SpectralBeamRenderer::~SpectralBeamRenderer() {
    if (vbo_ != 0U) {
        glDeleteBuffers(1, &vbo_);
    }
    if (vao_ != 0U) {
        glDeleteVertexArrays(1, &vao_);
    }
}

std::size_t SpectralBeamRenderer::draw(
    const SpectralBeamData& spectrum,
    const glm::vec3& cameraPosition,
    const glm::mat4& view,
    const glm::mat4& projection,
    float outputLength,
    float width,
    float intensity,
    float edgeSoftness
) const {
    const SpectralBeamMeshData mesh = buildSpectralBeamMesh(
        spectrum,
        cameraPosition,
        std::max(outputLength, 0.01f),
        std::max(width, 0.001f)
    );
    if (mesh.vertices.empty()) {
        return 0U;
    }

    shader_->use();
    shader_->setMat4("uView", view);
    shader_->setMat4("uProjection", projection);
    shader_->setFloat("uIntensity", std::max(intensity, 0.0f));
    shader_->setFloat("uEdgeSoftness", std::clamp(edgeSoftness, 0.01f, 1.0f));
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(SpectralBeamVertex)),
        mesh.vertices.data(),
        GL_DYNAMIC_DRAW
    );
    glBindVertexArray(vao_);

    const bool hasDebugGroups = GLAD_GL_KHR_debug != 0
        && glPushDebugGroup != nullptr
        && glPopDebugGroup != nullptr;
    for (const SpectralBeamBatch& batch : mesh.batches) {
        const char* label = groupLabel(batch.group);
        if (hasDebugGroups) {
            glPushDebugGroup(
                GL_DEBUG_SOURCE_APPLICATION,
                0U,
                static_cast<GLsizei>(std::strlen(label)),
                label
            );
        }
        glDrawArrays(
            GL_TRIANGLES,
            static_cast<GLint>(batch.firstVertex),
            static_cast<GLsizei>(batch.vertexCount)
        );
        if (hasDebugGroups) {
            glPopDebugGroup();
        }
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return mesh.batches.size();
}
