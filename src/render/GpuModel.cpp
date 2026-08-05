#include "render/GpuModel.h"

#include "render/Mesh.h"

GpuModel::GpuModel(const ModelData& data) {
    meshes_.reserve(data.meshes.size());
    for (const auto& meshData : data.meshes) {
        auto mesh = std::make_unique<Mesh>(meshData);
        submeshCount_ += mesh->submeshCount();
        vertexCount_ += mesh->vertexCount();
        triangleCount_ += mesh->triangleCount();
        meshes_.push_back(std::move(mesh));
    }
}

GpuModel::~GpuModel() = default;

void GpuModel::draw() const {
    for (const auto& mesh : meshes_) {
        mesh->draw();
    }
}
