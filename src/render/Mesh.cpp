#include "render/Mesh.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include <glad/gl.h>
#include <glm/common.hpp>

Mesh::Mesh(const MeshData& data)
    : vertexCount_(data.vertices.size()), indexCount_(data.indices.size()), submeshes_(data.submeshes) {
    if (submeshes_.empty() && indexCount_ > 0U) {
        submeshes_.push_back(SubmeshData{
            data.name,
            0U,
            static_cast<std::uint32_t>(indexCount_),
            -1
        });
    }

    submeshCenters_.reserve(submeshes_.size());
    for (const SubmeshData& submesh : submeshes_) {
        glm::vec3 boundsMin(std::numeric_limits<float>::max());
        glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
        bool hasVertex = false;
        const std::size_t begin = submesh.firstIndex;
        const std::size_t end = std::min(
            begin + static_cast<std::size_t>(submesh.indexCount),
            data.indices.size()
        );
        for (std::size_t index = begin; index < end; ++index) {
            const std::size_t vertexIndex = data.indices[index];
            if (vertexIndex >= data.vertices.size()) {
                continue;
            }
            const glm::vec3& position = data.vertices[vertexIndex].position;
            boundsMin = glm::min(boundsMin, position);
            boundsMax = glm::max(boundsMax, position);
            hasVertex = true;
        }
        submeshCenters_.push_back(hasVertex ? 0.5f * (boundsMin + boundsMax) : glm::vec3(0.0f));
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(data.vertices.size() * sizeof(Vertex)),
        data.vertices.data(),
        GL_STATIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(data.indices.size() * sizeof(std::uint32_t)),
        data.indices.data(),
        GL_STATIC_DRAW
    );

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(Vertex)),
        reinterpret_cast<void*>(offsetof(Vertex, position))
    );
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(Vertex)),
        reinterpret_cast<void*>(offsetof(Vertex, normal))
    );
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2,
        2,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(Vertex)),
        reinterpret_cast<void*>(offsetof(Vertex, texCoord0))
    );
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(
        3,
        4,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(Vertex)),
        reinterpret_cast<void*>(offsetof(Vertex, tangent))
    );

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

Mesh::~Mesh() {
    if (ebo_ != 0U) {
        glDeleteBuffers(1, &ebo_);
    }
    if (vbo_ != 0U) {
        glDeleteBuffers(1, &vbo_);
    }
    if (vao_ != 0U) {
        glDeleteVertexArrays(1, &vao_);
    }
}

void Mesh::draw(const MaterialBinder& bindMaterial) const {
    glBindVertexArray(vao_);
    for (const auto& submesh : submeshes_) {
        bindMaterial(submesh.materialIndex);
        const auto byteOffset = static_cast<std::uintptr_t>(submesh.firstIndex) * sizeof(std::uint32_t);
        glDrawElements(
            GL_TRIANGLES,
            static_cast<GLsizei>(submesh.indexCount),
            GL_UNSIGNED_INT,
            reinterpret_cast<const void*>(byteOffset)
        );
    }
    glBindVertexArray(0);
}

void Mesh::drawSubmesh(std::size_t index) const {
    if (index >= submeshes_.size()) {
        throw std::out_of_range("Mesh submesh index is out of range");
    }
    const SubmeshData& submesh = submeshes_[index];
    const auto byteOffset = static_cast<std::uintptr_t>(submesh.firstIndex) * sizeof(std::uint32_t);
    glBindVertexArray(vao_);
    glDrawElements(
        GL_TRIANGLES,
        static_cast<GLsizei>(submesh.indexCount),
        GL_UNSIGNED_INT,
        reinterpret_cast<const void*>(byteOffset)
    );
    glBindVertexArray(0);
}

std::int32_t Mesh::submeshMaterialIndex(std::size_t index) const {
    if (index >= submeshes_.size()) {
        throw std::out_of_range("Mesh submesh index is out of range");
    }
    return submeshes_[index].materialIndex;
}

const glm::vec3& Mesh::submeshCenter(std::size_t index) const {
    if (index >= submeshCenters_.size()) {
        throw std::out_of_range("Mesh submesh index is out of range");
    }
    return submeshCenters_[index];
}
