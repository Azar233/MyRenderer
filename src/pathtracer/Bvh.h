#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pathtracer/RayGeometry.h"

namespace pathtracer {

struct BvhBuildStats {
    std::size_t primitiveCount{0U};
    std::size_t nodeCount{0U};
    std::size_t leafCount{0U};
    std::size_t maximumDepth{0U};
};

class Bvh {
public:
    explicit Bvh(std::vector<Triangle> triangles = {}, std::size_t maximumLeafSize = 4U);

    void rebuild(std::vector<Triangle> triangles);
    bool empty() const { return triangles_.empty(); }
    const Bounds3& bounds() const { return rootBounds_; }
    const BvhBuildStats& stats() const { return stats_; }

    bool intersect(const Ray& ray, SurfaceInteraction& interaction) const;
    bool occluded(const Ray& ray) const;

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
    std::size_t maximumLeafSize_{4U};
    Bounds3 rootBounds_;
    BvhBuildStats stats_;
};

} // namespace pathtracer
