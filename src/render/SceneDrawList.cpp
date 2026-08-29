#include "render/SceneDrawList.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

void sortTransparentBackToFront(
    std::vector<TransparentSortEntry>& entries,
    const glm::vec3& cameraPosition
) {
    std::stable_sort(entries.begin(), entries.end(), [&](const auto& left, const auto& right) {
        const glm::vec3 leftDelta = left.worldCenter - cameraPosition;
        const glm::vec3 rightDelta = right.worldCenter - cameraPosition;
        return glm::dot(leftDelta, leftDelta) > glm::dot(rightDelta, rightDelta);
    });
}

ViewFrustum extractViewFrustum(const glm::mat4& viewProjection) {
    const glm::vec4 row0(
        viewProjection[0][0], viewProjection[1][0],
        viewProjection[2][0], viewProjection[3][0]
    );
    const glm::vec4 row1(
        viewProjection[0][1], viewProjection[1][1],
        viewProjection[2][1], viewProjection[3][1]
    );
    const glm::vec4 row2(
        viewProjection[0][2], viewProjection[1][2],
        viewProjection[2][2], viewProjection[3][2]
    );
    const glm::vec4 row3(
        viewProjection[0][3], viewProjection[1][3],
        viewProjection[2][3], viewProjection[3][3]
    );
    ViewFrustum frustum{{
        row3 + row0,
        row3 - row0,
        row3 + row1,
        row3 - row1,
        row3 + row2,
        row3 - row2
    }};
    for (glm::vec4& plane : frustum.planes) {
        const float length = glm::length(glm::vec3(plane));
        if (length > 1.0e-6f) plane /= length;
    }
    return frustum;
}

BoundingSphere transformBoundingSphere(
    const BoundingSphere& localBounds,
    const glm::mat4& modelMatrix
) {
    const glm::vec3 worldCenter = glm::vec3(
        modelMatrix * glm::vec4(localBounds.center, 1.0f)
    );
    const float maximumScale = std::max({
        glm::length(glm::vec3(modelMatrix[0])),
        glm::length(glm::vec3(modelMatrix[1])),
        glm::length(glm::vec3(modelMatrix[2]))
    });
    return BoundingSphere{worldCenter, localBounds.radius * maximumScale};
}

bool intersectsViewFrustum(const ViewFrustum& frustum, const BoundingSphere& bounds) {
    for (const glm::vec4& plane : frustum.planes) {
        const float signedDistance = glm::dot(glm::vec3(plane), bounds.center) + plane.w;
        if (signedDistance < -bounds.radius) return false;
    }
    return true;
}

float projectedSphereRadiusPixels(
    const BoundingSphere& bounds,
    const glm::vec3& cameraPosition,
    float verticalFieldOfViewRadians,
    int viewportHeight
) {
    const float distance = std::max(glm::length(bounds.center - cameraPosition), 1.0e-4f);
    const float focalLengthPixels = static_cast<float>(std::max(viewportHeight, 1))
        / (2.0f * std::tan(std::max(verticalFieldOfViewRadians, 1.0e-4f) * 0.5f));
    return bounds.radius * focalLengthPixels / distance;
}

std::size_t selectLodLevel(
    float projectedRadiusPixels,
    float mediumThresholdPixels,
    float highThresholdPixels
) {
    if (projectedRadiusPixels >= highThresholdPixels) return 0U;
    if (projectedRadiusPixels >= mediumThresholdPixels) return 1U;
    return 2U;
}
