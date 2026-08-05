#include "render/Mesh.h"

#include <cstddef>
#include <cstdint>

#include <glad/gl.h>

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

void Mesh::draw() const {
    glBindVertexArray(vao_);
    for (const auto& submesh : submeshes_) {
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
