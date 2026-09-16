#include "pathtracer/Bvh.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <utility>

namespace pathtracer {
namespace {

constexpr std::size_t sahBinCount = 16U;

float hitTieTolerance(float distance) {
    return 1.0e-5f * std::max(1.0f, std::abs(distance));
}

float surfaceArea(const Bounds3& bounds) {
    const glm::vec3 extent = bounds.extent();
    return 2.0f * (extent.x * extent.y + extent.y * extent.z + extent.z * extent.x);
}

std::size_t binFor(float centroid, float minimum, float extent) {
    const float normalized = std::clamp((centroid - minimum) / extent, 0.0f, 0.99999994f);
    return std::min<std::size_t>(static_cast<std::size_t>(normalized * sahBinCount), sahBinCount - 1U);
}

} // namespace

Bvh::Bvh(std::vector<Triangle> triangles, std::size_t maximumLeafSize,
         BvhSplitStrategy splitStrategy, bool buildPrimitiveLookup)
    : maximumLeafSize_(std::max<std::size_t>(1U, maximumLeafSize)),
      splitStrategy_(splitStrategy), buildPrimitiveLookup_(buildPrimitiveLookup) {
    rebuild(std::move(triangles));
}

std::size_t Bvh::estimatedBytes() const {
    return triangles_.size() * sizeof(Triangle) + nodes_.size() * sizeof(Node)
        + primitiveSlots_.size() * sizeof(std::size_t);
}

const Triangle* Bvh::primitive(std::uint32_t primitiveIndex) const {
    if (primitiveIndex >= primitiveSlots_.size()) return nullptr;
    const std::size_t slot = primitiveSlots_[primitiveIndex];
    return slot < triangles_.size() && triangles_[slot].primitiveIndex == primitiveIndex
        ? &triangles_[slot] : nullptr;
}

void Bvh::rebuild(std::vector<Triangle> triangles) {
    const auto start = std::chrono::steady_clock::now();
    triangles_ = std::move(triangles);
    nodes_.clear();
    primitiveSlots_.clear();
    rootBounds_ = Bounds3{};
    stats_ = BvhBuildStats{};
    stats_.primitiveCount = triangles_.size();
    if (triangles_.empty()) {
        stats_.buildMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start
        ).count();
        return;
    }

    nodes_.reserve(triangles_.size() * 2U);
    buildNode(0U, triangles_.size(), 0U);
    const auto maximumPrimitive = std::max_element(
        triangles_.begin(), triangles_.end(),
        [](const Triangle& left, const Triangle& right) {
            return left.primitiveIndex < right.primitiveIndex;
        });
    if (buildPrimitiveLookup_ && maximumPrimitive != triangles_.end()
        && maximumPrimitive->primitiveIndex < triangles_.size() * 4U) {
        primitiveSlots_.resize(static_cast<std::size_t>(maximumPrimitive->primitiveIndex) + 1U,
                               std::numeric_limits<std::size_t>::max());
        for (std::size_t slot = 0U; slot < triangles_.size(); ++slot)
            primitiveSlots_[triangles_[slot].primitiveIndex] = slot;
    }
    rootBounds_ = nodes_.front().bounds;
    stats_.nodeCount = nodes_.size();
    stats_.buildMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start
    ).count();
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
    if (count <= maximumLeafSize_) {
        Node& node = nodes_[nodeIndex];
        node.bounds = primitiveBounds;
        node.firstPrimitive = static_cast<std::uint32_t>(begin);
        node.primitiveCount = static_cast<std::uint32_t>(count);
        ++stats_.leafCount;
        stats_.maximumDepth = std::max(stats_.maximumDepth, depth + 1U);
        return nodeIndex;
    }

    int axis = centroidBounds.longestAxis();
    std::size_t middle = begin;
    bool usedSah = false;
    if (splitStrategy_ == BvhSplitStrategy::BinnedSah) {
        struct Bin {
            Bounds3 bounds;
            std::size_t count{0U};
        };
        float bestCost = std::numeric_limits<float>::infinity();
        int bestAxis = -1;
        std::size_t bestSplit = 0U;
        const float parentArea = surfaceArea(primitiveBounds);
        for (int candidateAxis = 0; candidateAxis < 3; ++candidateAxis) {
            const float extent = centroidBounds.extent()[candidateAxis];
            if (!(extent > 1.0e-7f)) continue;
            std::array<Bin, sahBinCount> bins;
            for (std::size_t index = begin; index < end; ++index) {
                const std::size_t bin = binFor(
                    triangles_[index].centroid()[candidateAxis],
                    centroidBounds.minimum[candidateAxis],
                    extent
                );
                ++bins[bin].count;
                bins[bin].bounds.expand(triangles_[index].bounds());
            }
            std::array<Bounds3, sahBinCount> leftBounds;
            std::array<Bounds3, sahBinCount> rightBounds;
            std::array<std::size_t, sahBinCount> leftCounts{};
            std::array<std::size_t, sahBinCount> rightCounts{};
            Bounds3 runningLeft;
            std::size_t leftCount = 0U;
            for (std::size_t bin = 0U; bin < sahBinCount; ++bin) {
                runningLeft.expand(bins[bin].bounds);
                leftCount += bins[bin].count;
                leftBounds[bin] = runningLeft;
                leftCounts[bin] = leftCount;
            }
            Bounds3 runningRight;
            std::size_t rightCount = 0U;
            for (std::size_t bin = sahBinCount; bin-- > 0U;) {
                runningRight.expand(bins[bin].bounds);
                rightCount += bins[bin].count;
                rightBounds[bin] = runningRight;
                rightCounts[bin] = rightCount;
            }
            for (std::size_t split = 0U; split + 1U < sahBinCount; ++split) {
                if (!leftCounts[split] || !rightCounts[split + 1U]) continue;
                const float cost = parentArea > 0.0f
                    ? 1.0f + (surfaceArea(leftBounds[split]) * static_cast<float>(leftCounts[split])
                              + surfaceArea(rightBounds[split + 1U])
                                  * static_cast<float>(rightCounts[split + 1U])) / parentArea
                    : std::numeric_limits<float>::infinity();
                if (cost < bestCost) {
                    bestCost = cost;
                    bestAxis = candidateAxis;
                    bestSplit = split;
                }
            }
        }
        if (bestAxis >= 0) {
            axis = bestAxis;
            const float extent = centroidBounds.extent()[axis];
            const auto partition = std::stable_partition(
                triangles_.begin() + static_cast<std::ptrdiff_t>(begin),
                triangles_.begin() + static_cast<std::ptrdiff_t>(end),
                [&](const Triangle& triangle) {
                    return binFor(triangle.centroid()[axis], centroidBounds.minimum[axis], extent)
                        <= bestSplit;
                }
            );
            middle = static_cast<std::size_t>(partition - triangles_.begin());
            usedSah = middle > begin && middle < end;
        }
    }

    if (!usedSah) {
        const bool degenerateCentroids = centroidBounds.extent()[axis] <= 1.0e-7f;
        if (degenerateCentroids) {
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
        middle = begin + count / 2U;
        ++stats_.medianSplitCount;
    } else {
        ++stats_.sahSplitCount;
    }
    const std::uint32_t leftChild = buildNode(begin, middle, depth + 1U);
    const std::uint32_t rightChild = buildNode(middle, end, depth + 1U);
    Node& node = nodes_[nodeIndex];
    node.bounds = primitiveBounds;
    node.leftChild = leftChild;
    node.rightChild = rightChild;
    return nodeIndex;
}

bool Bvh::intersect(const Ray& ray, SurfaceInteraction& interaction,
                    BvhTraversalStats* traversalStats) const {
    if (nodes_.empty()) return false;

    float rootNear = 0.0f;
    if (traversalStats) ++traversalStats->boundsTests;
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
        if (hit && pending.nearDistance > closestDistance + hitTieTolerance(closestDistance)) continue;

        const Node& node = nodes_[pending.index];
        if (node.leaf()) {
            const std::size_t begin = node.firstPrimitive;
            const std::size_t end = begin + node.primitiveCount;
            for (std::size_t index = begin; index < end; ++index) {
                if (traversalStats) ++traversalStats->triangleTests;
                Ray closestRay = ray;
                closestRay.tMax = hit
                    ? std::min(ray.tMax, closestDistance + hitTieTolerance(closestDistance))
                    : closestDistance;
                SurfaceInteraction candidate;
                if (intersectTriangle(closestRay, triangles_[index], candidate)) {
                    const float tolerance = hit ? hitTieTolerance(closestDistance) : 0.0f;
                    const bool materiallyCloser = !hit || candidate.t < closestDistance - tolerance;
                    const bool tiedAndStable = hit
                        && std::abs(candidate.t - closestDistance) <= tolerance
                        && candidate.primitiveIndex < interaction.primitiveIndex;
                    if (materiallyCloser || tiedAndStable) {
                        hit = true;
                        interaction = candidate;
                    }
                    closestDistance = std::min(closestDistance, candidate.t);
                }
            }
            continue;
        }

        float leftNear = 0.0f;
        float rightNear = 0.0f;
        Ray closestRay = ray;
        closestRay.tMax = hit
            ? std::min(ray.tMax, closestDistance + hitTieTolerance(closestDistance))
            : closestDistance;
        if (traversalStats) traversalStats->boundsTests += 2U;
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

bool Bvh::occluded(const Ray& ray, BvhTraversalStats* traversalStats) const {
    SurfaceInteraction interaction;
    return intersect(ray, interaction, traversalStats);
}

} // namespace pathtracer
