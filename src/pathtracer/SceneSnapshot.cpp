#include "pathtracer/SceneSnapshot.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace pathtracer {
namespace {

bool finiteVector(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

glm::vec3 transformNormal(const glm::mat3& matrix, const glm::vec3& normal) {
    const glm::vec3 transformed = matrix * normal;
    const float lengthSquared = glm::dot(transformed, transformed);
    if (!finiteVector(transformed) || lengthSquared <= 1.0e-12f) return glm::vec3(0.0f);
    return transformed * (1.0f / std::sqrt(lengthSquared));
}

bool nodeContainsMeshes(const ModelNodeData& node) {
    if (!node.meshIndices.empty()) return true;
    return std::any_of(node.children.begin(), node.children.end(), nodeContainsMeshes);
}

} // namespace

SceneSnapshotBuilder::SceneSnapshotBuilder(
    SnapshotCamera camera,
    SceneSnapshotLighting lighting
) {
    snapshot_.camera_ = std::move(camera);
    snapshot_.lighting_ = std::move(lighting);
}

std::uint32_t SceneSnapshotBuilder::assetIndex(
    const std::shared_ptr<const ModelData>& model
) {
    const auto existing = assetIndices_.find(model.get());
    if (existing != assetIndices_.end()) return existing->second;
    const auto index = static_cast<std::uint32_t>(snapshot_.assets_.size());
    snapshot_.assets_.push_back(SceneSnapshotAsset{model});
    assetIndices_.emplace(model.get(), index);
    return index;
}

void SceneSnapshotBuilder::addMeshInstance(
    std::uint32_t asset,
    std::uint32_t mesh,
    std::uint64_t entityId,
    const std::string& entityName,
    const glm::mat4& objectToWorld,
    const glm::vec3& tint,
    bool castsShadow
) {
    const ModelData& model = *snapshot_.assets_[asset].model;
    if (mesh >= model.meshes.size()) return;

    const float determinant = glm::determinant(glm::mat3(objectToWorld));
    const bool invertible = std::isfinite(determinant) && std::abs(determinant) > 1.0e-10f;
    SceneSnapshotInstance instance;
    instance.entityId = entityId;
    instance.entityName = entityName;
    instance.assetIndex = asset;
    instance.meshIndex = mesh;
    instance.objectToWorld = objectToWorld;
    instance.normalToWorld = invertible
        ? glm::inverseTranspose(glm::mat3(objectToWorld))
        : glm::mat3(1.0f);
    instance.tint = tint;
    instance.castsShadow = castsShadow;
    instance.transformInvertible = invertible;
    instance.skinned = !model.meshes[mesh].skinJoints.empty();
    snapshot_.instances_.push_back(std::move(instance));
}

void SceneSnapshotBuilder::addModel(
    std::shared_ptr<const ModelData> model,
    std::uint64_t entityId,
    std::string entityName,
    const glm::mat4& entityToWorld,
    const glm::vec3& tint,
    bool castsShadow
) {
    if (finished_) throw std::logic_error("Cannot add models after finishing a SceneSnapshot");
    if (model == nullptr || model->meshes.empty()) return;

    const std::uint32_t asset = assetIndex(model);
    if (!nodeContainsMeshes(model->rootNode)) {
        for (std::size_t meshIndex = 0; meshIndex < model->meshes.size(); ++meshIndex) {
            addMeshInstance(
                asset,
                static_cast<std::uint32_t>(meshIndex),
                entityId,
                entityName,
                entityToWorld,
                tint,
                castsShadow
            );
        }
        return;
    }

    std::function<void(const ModelNodeData&, const glm::mat4&)> appendNode;
    appendNode = [&](const ModelNodeData& node, const glm::mat4& parentTransform) {
        const glm::mat4 nodeToModel = parentTransform * node.localTransform;
        for (const std::uint32_t meshIndex : node.meshIndices) {
            if (meshIndex >= model->meshes.size()) continue;
            const glm::mat4 effectiveNodeTransform = model->meshes[meshIndex].vertexTransformBaked
                ? glm::mat4(1.0f)
                : nodeToModel;
            addMeshInstance(
                asset,
                meshIndex,
                entityId,
                entityName,
                entityToWorld * effectiveNodeTransform,
                tint,
                castsShadow
            );
        }
        for (const ModelNodeData& child : node.children) appendNode(child, nodeToModel);
    };
    appendNode(model->rootNode, glm::mat4(1.0f));
}

SceneSnapshot SceneSnapshotBuilder::finish() {
    if (finished_) throw std::logic_error("SceneSnapshotBuilder can only finish once");
    finished_ = true;
    return std::move(snapshot_);
}

std::vector<Triangle> buildWorldTriangles(const SceneSnapshot& snapshot) {
    std::size_t triangleCapacity = 0U;
    for (const SceneSnapshotInstance& instance : snapshot.instances()) {
        if (instance.assetIndex >= snapshot.assets().size()) continue;
        const auto& model = snapshot.assets()[instance.assetIndex].model;
        if (model == nullptr || instance.meshIndex >= model->meshes.size()) continue;
        triangleCapacity += model->meshes[instance.meshIndex].indices.size() / 3U;
    }

    std::vector<Triangle> triangles;
    triangles.reserve(triangleCapacity);
    for (std::size_t instanceIndex = 0; instanceIndex < snapshot.instances().size(); ++instanceIndex) {
        const SceneSnapshotInstance& instance = snapshot.instances()[instanceIndex];
        if (!instance.transformInvertible || instance.assetIndex >= snapshot.assets().size()) continue;
        const auto& model = snapshot.assets()[instance.assetIndex].model;
        if (model == nullptr || instance.meshIndex >= model->meshes.size()) continue;
        const MeshData& mesh = model->meshes[instance.meshIndex];

        std::vector<std::int32_t> materialByIndex(mesh.indices.size(), -1);
        for (const SubmeshData& submesh : mesh.submeshes) {
            const std::size_t begin = std::min<std::size_t>(submesh.firstIndex, mesh.indices.size());
            const std::size_t end = std::min<std::size_t>(
                static_cast<std::size_t>(submesh.firstIndex) + submesh.indexCount,
                mesh.indices.size()
            );
            std::fill(
                materialByIndex.begin() + static_cast<std::ptrdiff_t>(begin),
                materialByIndex.begin() + static_cast<std::ptrdiff_t>(end),
                submesh.materialIndex
            );
        }

        for (std::size_t firstIndex = 0; firstIndex + 2U < mesh.indices.size(); firstIndex += 3U) {
            const std::uint32_t indices[3]{
                mesh.indices[firstIndex],
                mesh.indices[firstIndex + 1U],
                mesh.indices[firstIndex + 2U]
            };
            if (indices[0] >= mesh.vertices.size()
                || indices[1] >= mesh.vertices.size()
                || indices[2] >= mesh.vertices.size()) {
                continue;
            }

            Triangle triangle;
            bool finitePositions = true;
            for (std::size_t corner = 0; corner < 3U; ++corner) {
                const Vertex& vertex = mesh.vertices[indices[corner]];
                triangle.positions[corner] = glm::vec3(
                    instance.objectToWorld * glm::vec4(vertex.position, 1.0f)
                );
                triangle.normals[corner] = transformNormal(instance.normalToWorld, vertex.normal);
                triangle.texCoords[corner] = vertex.texCoord0;
                finitePositions = finitePositions && finiteVector(triangle.positions[corner]);
            }
            const glm::vec3 geometricNormal = glm::cross(
                triangle.positions[1] - triangle.positions[0],
                triangle.positions[2] - triangle.positions[0]
            );
            if (!finitePositions || glm::dot(geometricNormal, geometricNormal) <= 1.0e-12f) continue;

            triangle.tint = instance.tint;
            triangle.primitiveIndex = static_cast<std::uint32_t>(triangles.size());
            triangle.instanceIndex = static_cast<std::uint32_t>(instanceIndex);
            triangle.meshIndex = instance.meshIndex;
            triangle.materialIndex = materialByIndex[firstIndex];
            triangle.assetIndex = instance.assetIndex;
            triangles.push_back(std::move(triangle));
        }
    }
    return triangles;
}

} // namespace pathtracer
