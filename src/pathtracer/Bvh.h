#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pathtracer/RayGeometry.h"

namespace pathtracer {

enum class BvhSplitStrategy {
    Median,
    BinnedSah
};

struct BvhBuildStats {
    std::size_t primitiveCount{0U};
    std::size_t nodeCount{0U};
    std::size_t leafCount{0U};
    std::size_t maximumDepth{0U};
    std::size_t medianSplitCount{0U};
    std::size_t sahSplitCount{0U};
    double buildMilliseconds{0.0};
};

struct BvhTraversalStats {
    std::uint64_t boundsTests{0U};
    std::uint64_t triangleTests{0U};
    std::uint64_t instanceTests{0U};

    BvhTraversalStats& operator+=(const BvhTraversalStats& other) {
        boundsTests += other.boundsTests;
        triangleTests += other.triangleTests;
        instanceTests += other.instanceTests;
        return *this;
    }
};

class Bvh {
public:
    explicit Bvh(std::vector<Triangle> triangles = {}, std::size_t maximumLeafSize = 4U,
                 BvhSplitStrategy splitStrategy = BvhSplitStrategy::Median,
                 bool buildPrimitiveLookup = false);

    void rebuild(std::vector<Triangle> triangles);
    bool empty() const { return triangles_.empty(); }
    const Bounds3& bounds() const { return rootBounds_; }
    const BvhBuildStats& stats() const { return stats_; }
    std::size_t estimatedBytes() const;
    const Triangle* primitive(std::uint32_t primitiveIndex) const;
    BvhSplitStrategy splitStrategy() const { return splitStrategy_; }

    bool intersect(const Ray& ray, SurfaceInteraction& interaction,
                   BvhTraversalStats* traversalStats = nullptr) const;
    bool occluded(const Ray& ray, BvhTraversalStats* traversalStats = nullptr) const;

private:
    struct Node {
        Bounds3 bounds;
        std::uint32_t firstPrimitive{0U};
        std::uint32_t primitiveCount{0U};
        std::uint32_t leftChild{0U};
        std::uint32_t rightChild{0U};

        bool leaf() const { return primitiveCount > 0U; }
    };

    std::uint32_t buildNode(std::size_t begin, std::size_t end, std::size_t depth);

    std::vector<Triangle> triangles_;
    std::vector<Node> nodes_;
    std::vector<std::size_t> primitiveSlots_;
    std::size_t maximumLeafSize_{4U};
    BvhSplitStrategy splitStrategy_{BvhSplitStrategy::Median};
    bool buildPrimitiveLookup_{false};
    Bounds3 rootBounds_;
    BvhBuildStats stats_;
};

} // namespace pathtracer
