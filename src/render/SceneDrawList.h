#pragma once

#include <cstddef>
#include <vector>

#include <glm/vec3.hpp>

struct TransparentSortEntry {
    std::size_t renderItemIndex{0};
    std::size_t transparentSubmeshIndex{0};
    glm::vec3 worldCenter{0.0f};
};

void sortTransparentBackToFront(
    std::vector<TransparentSortEntry>& entries,
    const glm::vec3& cameraPosition
);
