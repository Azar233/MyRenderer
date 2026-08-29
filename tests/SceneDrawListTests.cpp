#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "render/SceneDrawList.h"

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    try {
        std::vector<TransparentSortEntry> entries{
            TransparentSortEntry{0U, 0U, glm::vec3(0.0f, 0.0f, 2.0f)},
            TransparentSortEntry{1U, 0U, glm::vec3(0.0f, 0.0f, 5.0f)},
            TransparentSortEntry{0U, 1U, glm::vec3(0.0f, 0.0f, 3.0f)},
            TransparentSortEntry{2U, 0U, glm::vec3(0.0f, 0.0f, -5.0f)}
        };

        sortTransparentBackToFront(entries, glm::vec3(0.0f));

        require(entries[0].renderItemIndex == 1U, "farthest scene item should draw first");
        require(entries[1].renderItemIndex == 2U, "equal-distance entries should keep stable order");
        require(entries[2].transparentSubmeshIndex == 1U, "middle submesh should draw before near submesh");
        require(entries[3].transparentSubmeshIndex == 0U, "nearest submesh should draw last");

        const glm::mat4 view = glm::lookAt(
            glm::vec3(0.0f, 0.0f, 5.0f),
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );
        const glm::mat4 projection = glm::perspective(
            glm::radians(60.0f),
            16.0f / 9.0f,
            0.1f,
            100.0f
        );
        const ViewFrustum frustum = extractViewFrustum(projection * view);
        require(
            intersectsViewFrustum(frustum, BoundingSphere{glm::vec3(0.0f), 1.0f}),
            "sphere at the view target should be visible"
        );
        require(
            !intersectsViewFrustum(frustum, BoundingSphere{glm::vec3(50.0f, 0.0f, 0.0f), 1.0f}),
            "sphere outside the right plane should be culled"
        );
        const BoundingSphere scaled = transformBoundingSphere(
            BoundingSphere{glm::vec3(1.0f, 0.0f, 0.0f), 2.0f},
            glm::scale(
                glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f)),
                glm::vec3(2.0f, 3.0f, 4.0f)
            )
        );
        require(glm::length(scaled.center - glm::vec3(5.0f, 0.0f, 0.0f)) < 1.0e-4f,
            "bounding-sphere center should follow the model transform");
        require(std::abs(scaled.radius - 8.0f) < 1.0e-4f,
            "bounding-sphere radius should use the largest model scale");
        require(selectLodLevel(48.0f, 12.0f, 32.0f) == 0U, "large projection should use LOD0");
        require(selectLodLevel(20.0f, 12.0f, 32.0f) == 1U, "medium projection should use LOD1");
        require(selectLodLevel(6.0f, 12.0f, 32.0f) == 2U, "small projection should use LOD2");

        std::cout << "Scene draw-list tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Scene draw-list test failed: " << error.what() << '\n';
        return 1;
    }
}
