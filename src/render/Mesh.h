#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include <glm/vec3.hpp>

#include "asset/ModelData.h"

class Mesh {
public:
    explicit Mesh(const MeshData& data);
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    using MaterialBinder = std::function<void(std::int32_t)>;

    void draw(const MaterialBinder& bindMaterial) const;
    void drawSubmesh(std::size_t index) const;
    std::int32_t submeshMaterialIndex(std::size_t index) const;
    const glm::vec3& submeshCenter(std::size_t index) const;
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
    std::vector<glm::vec3> submeshCenters_;
};
