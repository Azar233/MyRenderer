#include "render/SceneDrawList.h"

#include <algorithm>

#include <glm/geometric.hpp>

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
