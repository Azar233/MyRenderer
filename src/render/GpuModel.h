#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "asset/ModelData.h"

class Mesh;

class GpuModel {
public:
    explicit GpuModel(const ModelData& data);
    ~GpuModel();

    GpuModel(const GpuModel&) = delete;
    GpuModel& operator=(const GpuModel&) = delete;

    void draw() const;

    std::size_t meshCount() const { return meshes_.size(); }
    std::size_t submeshCount() const { return submeshCount_; }
    std::size_t vertexCount() const { return vertexCount_; }
    std::size_t triangleCount() const { return triangleCount_; }

private:
    std::vector<std::unique_ptr<Mesh>> meshes_;
    std::size_t submeshCount_{0};
    std::size_t vertexCount_{0};
    std::size_t triangleCount_{0};
};
