#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

        std::cout << "Scene draw-list tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Scene draw-list test failed: " << error.what() << '\n';
        return 1;
    }
}
