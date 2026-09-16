#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>

#include "pathtracer/Bvh.h"
#include "pathtracer/SceneSnapshot.h"

namespace pathtracer {

struct InstancedBvhBuildStats {
    std::size_t blasCount{0U};
    std::size_t instanceCount{0U};
    std::size_t uniquePrimitiveCount{0U};
    std::size_t expandedPrimitiveCount{0U};
    std::size_t blasNodeCount{0U};
    std::size_t tlasNodeCount{0U};
    std::size_t maximumBlasDepth{0U};
    std::size_t maximumTlasDepth{0U};
    std::size_t estimatedBytes{0U};
    double buildMilliseconds{0.0};
};

class InstancedBvh {
public:
    explicit InstancedBvh(const SceneSnapshot& snapshot,
                          BvhSplitStrategy splitStrategy = BvhSplitStrategy::BinnedSah);

    bool empty() const { return instances_.empty(); }
    const InstancedBvhBuildStats& stats() const { return stats_; }
    bool intersect(const Ray& ray, SurfaceInteraction& interaction,
                   BvhTraversalStats* traversalStats = nullptr) const;
    bool occluded(const Ray& ray, BvhTraversalStats* traversalStats = nullptr) const;

private:
    struct Blas {
        std::uint32_t assetIndex{0U};
        std::uint32_t meshIndex{0U};
        Bvh bvh;
    };
    struct Instance {
        std::uint32_t snapshotIndex{0U};
        std::uint32_t blasIndex{0U};
        std::uint32_t primitiveOffset{0U};
        Bounds3 bounds;
        glm::mat4 objectToWorld{1.0f};
        glm::mat4 worldToObject{1.0f};
        glm::mat3 normalToWorld{1.0f};
        glm::vec3 tint{1.0f};
        float handedness{1.0f};
    };
    struct Node {
        Bounds3 bounds;
        std::uint32_t firstInstance{0U};
        std::uint32_t instanceCount{0U};
        std::uint32_t leftChild{0U};
        std::uint32_t rightChild{0U};
        bool leaf() const { return instanceCount > 0U; }
    };

    std::uint32_t buildNode(std::size_t begin, std::size_t end, std::size_t depth);
    bool intersectInstance(const Instance& instance, const Ray& ray,
                           SurfaceInteraction& interaction,
                           BvhTraversalStats* traversalStats) const;

    std::vector<Blas> blases_;
    std::vector<Instance> instances_;
    std::vector<Node> nodes_;
    InstancedBvhBuildStats stats_;
};

} // namespace pathtracer
