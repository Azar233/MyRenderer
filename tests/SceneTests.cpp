#include <cmath>
#include <iostream>
#include <stdexcept>

#include "scene/Scene.h"

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool close(float left, float right) {
    return std::abs(left - right) < 1.0e-5f;
}

} // namespace

int main() {
    try {
        Scene scene;
        const auto root = scene.createEntity("Root");
        auto* rootEntity = scene.find(root);
        require(rootEntity != nullptr, "root entity should exist");
        rootEntity->transform.translation = glm::vec3(2.0f, 0.0f, 0.0f);

        const auto child = scene.createEntity("Child");
        auto* childEntity = scene.find(child);
        require(childEntity != nullptr, "child entity should exist");
        childEntity->transform.translation = glm::vec3(0.0f, 3.0f, 0.0f);
        require(scene.setParent(child, root), "valid parent should be accepted");
        require(!scene.setParent(root, child), "hierarchy cycle should be rejected");

        for (int index = 0; index < 8; ++index) {
            scene.createEntity("Shared instance " + std::to_string(index));
        }
        require(scene.size() == 10U, "scene should preserve ten independent entities");
        scene.updateWorldTransforms();
        childEntity = scene.find(child);
        require(childEntity != nullptr, "child should remain valid after updates");
        require(close(childEntity->worldTransform[3].x, 2.0f), "parent X transform should propagate");
        require(close(childEntity->worldTransform[3].y, 3.0f), "child Y transform should propagate");

        scene.beginFrame();
        childEntity->transform.translation.y = 4.0f;
        scene.updateWorldTransforms();
        require(childEntity->motionHistoryValid, "beginFrame should establish motion history");
        require(close(childEntity->previousWorldTransform[3].y, 3.0f), "previous transform should be retained");
        require(close(childEntity->worldTransform[3].y, 4.0f), "current transform should update");

        const auto duplicate = scene.duplicateEntity(child);
        require(duplicate != invalidSceneEntityId, "entity duplication should succeed");
        require(scene.find(duplicate)->parent == invalidSceneEntityId, "duplicate should be a root entity");
        require(scene.destroyEntity(root), "entity deletion should succeed");
        require(scene.find(child)->parent == invalidSceneEntityId, "children should be reparented to root");

        std::cout << "Scene tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Scene test failed: " << error.what() << '\n';
        return 1;
    }
}
