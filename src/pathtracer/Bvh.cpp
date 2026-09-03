#include "pathtracer/Bvh.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace pathtracer {

Bvh::Bvh(std::vector<Triangle> triangles, std::size_t maximumLeafSize)
    : maximumLeafSize_(std::max<std::size_t>(1U, maximumLeafSize)) {
    rebuild(std::move(triangles));
}

void Bvh::rebuild(std::vector<Triangle> triangles) {
    triangles_ = std::move(triangles);
    nodes_.clear();
    rootBounds_ = Bounds3{};
    stats_ = BvhBuildStats{};
    stats_.primitiveCount = triangles_.size();
    if (triangles_.empty()) return;

    nodes_.reserve(triangles_.size() * 2U);
    buildNode(0U, triangles_.size(), 0U);
    rootBounds_ = nodes_.front().bounds;
    stats_.nodeCount = nodes_.size();
}

std::uint32_t Bvh::buildNode(std::size_t begin, std::size_t end, std::size_t depth) {
    const std::uint32_t nodeIndex = static_cast<std::uint32_t>(nodes_.size());
    nodes_.push_back(Node{});

    Bounds3 primitiveBounds;
    Bounds3 centroidBounds;
    for (std::size_t index = begin; index < end; ++index) {
        primitiveBounds.expand(triangles_[index].bounds());
        centroidBounds.expand(triangles_[index].centroid());
    }

    const std::size_t count = end - begin;
    const int axis = centroidBounds.longestAxis();
    const bool degenerateCentroids = centroidBounds.extent()[axis] <= 1.0e-7f;
    if (count <= maximumLeafSize_ || degenerateCentroids) {
        Node& node = nodes_[nodeIndex];
        node.bounds = primitiveBounds;
        node.firstPrimitive = static_cast<std::uint32_t>(begin);
        node.primitiveCount = static_cast<std::uint32_t>(count);
        ++stats_.leafCount;
        stats_.maximumDepth = std::max(stats_.maximumDepth, depth + 1U);
        return nodeIndex;
    }

    std::stable_sort(
        triangles_.begin() + static_cast<std::ptrdiff_t>(begin),
        triangles_.begin() + static_cast<std::ptrdiff_t>(end),
        [axis](const Triangle& left, const Triangle& right) {
            const float leftCentroid = left.centroid()[axis];
            const float rightCentroid = right.centroid()[axis];
            if (leftCentroid == rightCentroid) return left.primitiveIndex < right.primitiveIndex;
            return leftCentroid < rightCentroid;
        }
    );

    const std::size_t middle = begin + count / 2U;
    const std::uint32_t leftChild = buildNode(begin, middle, depth + 1U);
    const std::uint32_t rightChild = buildNode(middle, end, depth + 1U);
    Node& node = nodes_[nodeIndex];
    node.bounds = primitiveBounds;
    node.leftChild = leftChild;
    node.rightChild = rightChild;
    return nodeIndex;
}

bool Bvh::intersect(const Ray& ray, SurfaceInteraction& interaction) const {
    if (nodes_.empty()) return false;

    float rootNear = 0.0f;
    if (!nodes_.front().bounds.intersect(ray, &rootNear, nullptr)) return false;

    struct PendingNode {
        std::uint32_t index{0U};
        float nearDistance{0.0f};
    };
    std::vector<PendingNode> stack;
    stack.reserve(stats_.maximumDepth * 2U + 1U);
    stack.push_back(PendingNode{0U, rootNear});

    bool hit = false;
    float closestDistance = ray.tMax;
    while (!stack.empty()) {
        const PendingNode pending = stack.back();
        stack.pop_back();
        if (pending.nearDistance > closestDistance) continue;

        const Node& node = nodes_[pending.index];
        if (node.leaf()) {
            const std::size_t begin = node.firstPrimitive;
            const std::size_t end = begin + node.primitiveCount;
            for (std::size_t index = begin; index < end; ++index) {
                Ray closestRay = ray;
                closestRay.tMax = closestDistance;
                SurfaceInteraction candidate;
                if (intersectTriangle(closestRay, triangles_[index], candidate)) {
                    hit = true;
                    closestDistance = candidate.t;
                    interaction = candidate;
                }
            }
            continue;
        }

        float leftNear = 0.0f;
        float rightNear = 0.0f;
        Ray closestRay = ray;
        closestRay.tMax = closestDistance;
        const bool hitsLeft = nodes_[node.leftChild].bounds.intersect(closestRay, &leftNear, nullptr);
        const bool hitsRight = nodes_[node.rightChild].bounds.intersect(closestRay, &rightNear, nullptr);
        if (hitsLeft && hitsRight) {
            if (leftNear <= rightNear) {
                stack.push_back(PendingNode{node.rightChild, rightNear});
                stack.push_back(PendingNode{node.leftChild, leftNear});
            } else {
                stack.push_back(PendingNode{node.leftChild, leftNear});
                stack.push_back(PendingNode{node.rightChild, rightNear});
            }
        } else if (hitsLeft) {
            stack.push_back(PendingNode{node.leftChild, leftNear});
        } else if (hitsRight) {
            stack.push_back(PendingNode{node.rightChild, rightNear});
        }
    }
    return hit;
}

bool Bvh::occluded(const Ray& ray) const {
    SurfaceInteraction interaction;
    return intersect(ray, interaction);
}

} // namespace pathtracer
