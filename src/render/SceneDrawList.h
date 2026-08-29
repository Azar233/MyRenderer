#pragma once

#include <cstddef>
#include <array>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct TransparentSortEntry {
    std::size_t renderItemIndex{0};
    std::size_t transparentSubmeshIndex{0};
    glm::vec3 worldCenter{0.0f};
};

void sortTransparentBackToFront(
    std::vector<TransparentSortEntry>& entries,
    const glm::vec3& cameraPosition
);

struct BoundingSphere {
    glm::vec3 center{0.0f};
    float radius{0.0f};
};

struct ViewFrustum {
    std::array<glm::vec4, 6> planes{};
};

ViewFrustum extractViewFrustum(const glm::mat4& viewProjection);
BoundingSphere transformBoundingSphere(
    const BoundingSphere& localBounds,
    const glm::mat4& modelMatrix
);
bool intersectsViewFrustum(const ViewFrustum& frustum, const BoundingSphere& bounds);
float projectedSphereRadiusPixels(
    const BoundingSphere& bounds,
    const glm::vec3& cameraPosition,
    float verticalFieldOfViewRadians,
    int viewportHeight
);
std::size_t selectLodLevel(
    float projectedRadiusPixels,
    float mediumThresholdPixels,
    float highThresholdPixels
);
