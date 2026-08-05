#pragma once

#include <cstddef>
#include <vector>

#include "asset/ModelData.h"

class Mesh {
public:
    explicit Mesh(const MeshData& data);
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void draw() const;
    std::size_t vertexCount() const { return vertexCount_; }
    std::size_t triangleCount() const { return indexCount_ / 3; }
    std::size_t submeshCount() const { return submeshes_.size(); }

private:
    unsigned int vao_{0};
    unsigned int vbo_{0};
    unsigned int ebo_{0};
    std::size_t vertexCount_{0};
    std::size_t indexCount_{0};
    std::vector<SubmeshData> submeshes_;
};
