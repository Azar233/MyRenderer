#include "pathtracer/InstancedBvh.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace pathtracer {
namespace {

float hitTieTolerance(float distance) {
    return 1.0e-5f * std::max(1.0f, std::abs(distance));
}

glm::vec3 normalized(const glm::vec3& value) {
    const float lengthSquared = glm::dot(value, value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12f) return glm::vec3(0.0f);
    return value / std::sqrt(lengthSquared);
}

std::vector<Triangle> localTriangles(const MeshData& mesh, std::uint32_t assetIndex,
                                     std::uint32_t meshIndex) {
    std::vector<std::int32_t> materialByIndex(mesh.indices.size(), -1);
    for (const SubmeshData& submesh : mesh.submeshes) {
        const std::size_t begin = std::min<std::size_t>(submesh.firstIndex, mesh.indices.size());
        const std::size_t end = std::min<std::size_t>(
            static_cast<std::size_t>(submesh.firstIndex) + submesh.indexCount,
            mesh.indices.size());
        std::fill(materialByIndex.begin() + static_cast<std::ptrdiff_t>(begin),
                  materialByIndex.begin() + static_cast<std::ptrdiff_t>(end),
                  submesh.materialIndex);
    }

    std::vector<Triangle> triangles;
    triangles.reserve(mesh.indices.size() / 3U);
    for (std::size_t first = 0; first + 2U < mesh.indices.size(); first += 3U) {
        const std::uint32_t indices[3]{mesh.indices[first], mesh.indices[first + 1U],
                                       mesh.indices[first + 2U]};
        if (indices[0] >= mesh.vertices.size() || indices[1] >= mesh.vertices.size()
            || indices[2] >= mesh.vertices.size()) continue;
        Triangle triangle;
        for (std::size_t corner = 0; corner < 3U; ++corner) {
            const Vertex& vertex = mesh.vertices[indices[corner]];
            triangle.positions[corner] = vertex.position;
            triangle.normals[corner] = vertex.normal;
            triangle.texCoords[corner] = vertex.texCoord0;
            triangle.tangents[corner] = vertex.tangent;
        }
        const glm::vec3 cross = glm::cross(triangle.positions[1] - triangle.positions[0],
                                           triangle.positions[2] - triangle.positions[0]);
        if (!std::isfinite(glm::dot(cross, cross)) || glm::dot(cross, cross) <= 1.0e-12f) continue;
        triangle.primitiveIndex = static_cast<std::uint32_t>(triangles.size());
        triangle.assetIndex = assetIndex;
        triangle.meshIndex = meshIndex;
        triangle.materialIndex = materialByIndex[first];
        triangles.push_back(triangle);
    }
    return triangles;
}

Bounds3 transformedBounds(const Bounds3& local, const glm::mat4& transform) {
    Bounds3 world;
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 point{
            (corner & 1) ? local.maximum.x : local.minimum.x,
            (corner & 2) ? local.maximum.y : local.minimum.y,
            (corner & 4) ? local.maximum.z : local.minimum.z
        };
        world.expand(glm::vec3(transform * glm::vec4(point, 1.0f)));
    }
    return world;
}

} // namespace

InstancedBvh::InstancedBvh(const SceneSnapshot& snapshot, BvhSplitStrategy splitStrategy) {
    const auto start = std::chrono::steady_clock::now();
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> blasByMesh;
    std::uint32_t primitiveOffset = 0U;
    for (std::size_t snapshotIndex = 0; snapshotIndex < snapshot.instances().size(); ++snapshotIndex) {
        const SceneSnapshotInstance& source = snapshot.instances()[snapshotIndex];
        if (!source.transformInvertible || source.assetIndex >= snapshot.assets().size()) continue;
        const auto& model = snapshot.assets()[source.assetIndex].model;
        if (!model || source.meshIndex >= model->meshes.size()) continue;
        const auto key = std::make_pair(source.assetIndex, source.meshIndex);
        auto found = blasByMesh.find(key);
        std::uint32_t blasIndex = 0U;
        if (found == blasByMesh.end()) {
            blasIndex = static_cast<std::uint32_t>(blases_.size());
            Bvh bvh(localTriangles(model->meshes[source.meshIndex], source.assetIndex,
                                   source.meshIndex), 4U, splitStrategy, true);
            if (bvh.empty()) continue;
            stats_.uniquePrimitiveCount += bvh.stats().primitiveCount;
            stats_.blasNodeCount += bvh.stats().nodeCount;
            stats_.maximumBlasDepth = std::max(stats_.maximumBlasDepth, bvh.stats().maximumDepth);
            blases_.push_back(Blas{source.assetIndex, source.meshIndex, std::move(bvh)});
            blasByMesh.emplace(key, blasIndex);
        } else {
            blasIndex = found->second;
        }

        const Blas& blas = blases_[blasIndex];
        Instance instance;
        instance.snapshotIndex = static_cast<std::uint32_t>(snapshotIndex);
        instance.blasIndex = blasIndex;
        instance.primitiveOffset = primitiveOffset;
        instance.objectToWorld = source.objectToWorld;
        instance.worldToObject = glm::inverse(source.objectToWorld);
        instance.normalToWorld = source.normalToWorld;
        instance.tint = source.tint;
        instance.handedness = glm::determinant(glm::mat3(source.objectToWorld)) < 0.0f ? -1.0f : 1.0f;
        instance.bounds = transformedBounds(blas.bvh.bounds(), source.objectToWorld);
        if (!instance.bounds.valid()) continue;
        primitiveOffset += static_cast<std::uint32_t>(blas.bvh.stats().primitiveCount);
        instances_.push_back(instance);
    }
    stats_.blasCount = blases_.size();
    stats_.instanceCount = instances_.size();
    stats_.expandedPrimitiveCount = primitiveOffset;
    if (!instances_.empty()) {
        nodes_.reserve(instances_.size() * 2U);
        buildNode(0U, instances_.size(), 0U);
    }
    stats_.tlasNodeCount = nodes_.size();
    for (const Blas& blas : blases_) stats_.estimatedBytes += blas.bvh.estimatedBytes();
    stats_.estimatedBytes += instances_.size() * sizeof(Instance) + nodes_.size() * sizeof(Node);
    stats_.buildMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

std::uint32_t InstancedBvh::buildNode(std::size_t begin, std::size_t end, std::size_t depth) {
    const std::uint32_t index = static_cast<std::uint32_t>(nodes_.size());
    nodes_.push_back(Node{});
    Bounds3 bounds;
    Bounds3 centroids;
    for (std::size_t i = begin; i < end; ++i) {
        bounds.expand(instances_[i].bounds);
        centroids.expand(instances_[i].bounds.centroid());
    }
    const std::size_t count = end - begin;
    // A single-instance TLAS leaf avoids paying for several BLAS root tests in
    // the densely packed stress scene. Instance records are much coarser than
    // triangles, so the extra TLAS nodes are a favorable trade here.
    if (count <= 1U || centroids.extent()[centroids.longestAxis()] <= 1.0e-7f) {
        Node& node = nodes_[index];
        node.bounds = bounds;
        node.firstInstance = static_cast<std::uint32_t>(begin);
        node.instanceCount = static_cast<std::uint32_t>(count);
        stats_.maximumTlasDepth = std::max(stats_.maximumTlasDepth, depth + 1U);
        return index;
    }
    const int axis = centroids.longestAxis();
    std::stable_sort(instances_.begin() + static_cast<std::ptrdiff_t>(begin),
                     instances_.begin() + static_cast<std::ptrdiff_t>(end),
                     [axis](const Instance& left, const Instance& right) {
        const float a = left.bounds.centroid()[axis];
        const float b = right.bounds.centroid()[axis];
        return a == b ? left.snapshotIndex < right.snapshotIndex : a < b;
    });
    const std::size_t middle = begin + count / 2U;
    const std::uint32_t left = buildNode(begin, middle, depth + 1U);
    const std::uint32_t right = buildNode(middle, end, depth + 1U);
    Node& node = nodes_[index];
    node.bounds = bounds;
    node.leftChild = left;
    node.rightChild = right;
    return index;
}

bool InstancedBvh::intersectInstance(const Instance& instance, const Ray& ray,
                                     SurfaceInteraction& interaction,
                                     BvhTraversalStats* traversalStats) const {
    const Ray localRay{
        glm::vec3(instance.worldToObject * glm::vec4(ray.origin, 1.0f)),
        glm::vec3(instance.worldToObject * glm::vec4(ray.direction, 0.0f)),
        ray.tMin,
        ray.tMax
    };
    SurfaceInteraction local;
    if (!blases_[instance.blasIndex].bvh.intersect(localRay, local, traversalStats)) return false;

    const Triangle* triangle = blases_[instance.blasIndex].bvh.primitive(local.primitiveIndex);
    if (!triangle) return false;
    const glm::vec3 rawLocalGeometric = normalized(glm::cross(
        triangle->positions[1] - triangle->positions[0],
        triangle->positions[2] - triangle->positions[0]));
    glm::vec3 rawWorldGeometric = normalized(instance.normalToWorld * rawLocalGeometric)
        * instance.handedness;
    if (glm::dot(rawWorldGeometric, rawWorldGeometric) == 0.0f) return false;
    const bool frontFace = glm::dot(ray.direction, rawWorldGeometric) < 0.0f;
    glm::vec3 rawWorldShading(0.0f);
    glm::vec4 worldTangent(0.0f);
    for (std::size_t corner = 0U; corner < 3U; ++corner) {
        const float weight = local.barycentrics[static_cast<glm::vec3::length_type>(corner)];
        rawWorldShading += weight * normalized(instance.normalToWorld * triangle->normals[corner]);
        worldTangent += weight * glm::vec4(
            normalized(glm::mat3(instance.objectToWorld) * glm::vec3(triangle->tangents[corner])),
            triangle->tangents[corner].w == 0.0f
                ? 0.0f : triangle->tangents[corner].w * instance.handedness);
    }
    rawWorldShading = normalized(rawWorldShading);
    if (glm::dot(rawWorldShading, rawWorldShading) == 0.0f) rawWorldShading = rawWorldGeometric;
    if (glm::dot(rawWorldShading, rawWorldGeometric) < 0.0f) rawWorldShading = -rawWorldShading;

    interaction = local;
    interaction.position = ray.at(local.t);
    interaction.geometricNormal = frontFace ? rawWorldGeometric : -rawWorldGeometric;
    interaction.shadingNormal = frontFace ? rawWorldShading : -rawWorldShading;
    interaction.tangent = worldTangent;
    interaction.tint = instance.tint;
    interaction.primitiveIndex = instance.primitiveOffset + local.primitiveIndex;
    interaction.instanceIndex = instance.snapshotIndex;
    interaction.frontFace = frontFace;
    return true;
}

bool InstancedBvh::intersect(const Ray& ray, SurfaceInteraction& interaction,
                             BvhTraversalStats* traversalStats) const {
    if (nodes_.empty()) return false;
    float rootNear = 0.0f;
    if (traversalStats) ++traversalStats->boundsTests;
    if (!nodes_.front().bounds.intersect(ray, &rootNear, nullptr)) return false;
    struct Pending { std::uint32_t index; float nearDistance; };
    std::vector<Pending> stack{{0U, rootNear}};
    bool hit = false;
    float closest = ray.tMax;
    while (!stack.empty()) {
        const Pending pending = stack.back();
        stack.pop_back();
        if (hit && pending.nearDistance > closest + hitTieTolerance(closest)) continue;
        const Node& node = nodes_[pending.index];
        if (node.leaf()) {
            for (std::size_t i = node.firstInstance; i < node.firstInstance + node.instanceCount; ++i) {
                if (traversalStats) ++traversalStats->instanceTests;
                Ray candidateRay = ray;
                candidateRay.tMax = hit ? std::min(ray.tMax, closest + hitTieTolerance(closest)) : closest;
                SurfaceInteraction candidate;
                if (!intersectInstance(instances_[i], candidateRay, candidate, traversalStats)) continue;
                const float tolerance = hit ? hitTieTolerance(closest) : 0.0f;
                const bool choose = !hit || candidate.t < closest - tolerance
                    || (std::abs(candidate.t - closest) <= tolerance
                        && candidate.primitiveIndex < interaction.primitiveIndex);
                if (choose) interaction = candidate;
                hit = true;
                closest = std::min(closest, candidate.t);
            }
            continue;
        }
        Ray candidateRay = ray;
        candidateRay.tMax = hit ? std::min(ray.tMax, closest + hitTieTolerance(closest)) : closest;
        float leftNear = 0.0f, rightNear = 0.0f;
        if (traversalStats) traversalStats->boundsTests += 2U;
        const bool left = nodes_[node.leftChild].bounds.intersect(candidateRay, &leftNear, nullptr);
        const bool right = nodes_[node.rightChild].bounds.intersect(candidateRay, &rightNear, nullptr);
        if (left && right) {
            if (leftNear <= rightNear) {
                stack.push_back({node.rightChild, rightNear});
                stack.push_back({node.leftChild, leftNear});
            } else {
                stack.push_back({node.leftChild, leftNear});
                stack.push_back({node.rightChild, rightNear});
            }
        } else if (left) stack.push_back({node.leftChild, leftNear});
        else if (right) stack.push_back({node.rightChild, rightNear});
    }
    return hit;
}

bool InstancedBvh::occluded(const Ray& ray, BvhTraversalStats* traversalStats) const {
    SurfaceInteraction interaction;
    return intersect(ray, interaction, traversalStats);
}

} // namespace pathtracer
