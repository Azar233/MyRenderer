#pragma once

#include <cstddef>

#include "render/Vertex.h"

class Mesh {
public:
    explicit Mesh(const MeshData& data);
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void draw() const;
    std::size_t vertexCount() const { return vertexCount_; }
    std::size_t triangleCount() const { return indexCount_ / 3; }

private:
    unsigned int vao_{0};
    unsigned int vbo_{0};
    unsigned int ebo_{0};
    std::size_t vertexCount_{0};
    std::size_t indexCount_{0};
};
