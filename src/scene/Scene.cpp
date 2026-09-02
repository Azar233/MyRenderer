#include "scene/Scene.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>

glm::mat4 SceneTransform::matrix() const {
    glm::mat4 result = glm::translate(glm::mat4(1.0f), translation);
    result = glm::rotate(result, glm::radians(rotationDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
    result = glm::rotate(result, glm::radians(rotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
    result = glm::rotate(result, glm::radians(rotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
    result = glm::scale(result, scale);
    return result * assetTransform;
}

SceneEntityId Scene::createEntity(std::string name, const GpuModel* model) {
    const SceneEntityId id = nextId_++;
    SceneEntity entity;
    entity.id = id;
    entity.name = std::move(name);
    entity.model = model;
    entities_.push_back(std::move(entity));
    return id;
}

SceneEntityId Scene::duplicateEntity(SceneEntityId source) {
    const SceneEntity* original = find(source);
    if (original == nullptr) return invalidSceneEntityId;
    SceneEntity copy = *original;
    copy.id = nextId_++;
    copy.name += " Copy";
    copy.parent = invalidSceneEntityId;
    copy.motionHistoryValid = false;
    copy.worldTransformInitialized = false;
    copy.previousWorldTransform = copy.worldTransform;
    entities_.push_back(std::move(copy));
    return entities_.back().id;
}

bool Scene::destroyEntity(SceneEntityId id) {
    const auto entity = std::find_if(
        entities_.begin(), entities_.end(),
        [id](const SceneEntity& candidate) { return candidate.id == id; }
    );
    if (entity == entities_.end()) return false;
    for (SceneEntity& candidate : entities_) {
        if (candidate.parent == id) candidate.parent = invalidSceneEntityId;
    }
    entities_.erase(entity);
    return true;
}

bool Scene::setParent(SceneEntityId child, SceneEntityId parent) {
    SceneEntity* childEntity = find(child);
    if (childEntity == nullptr || child == parent) return false;
    if (parent != invalidSceneEntityId && find(parent) == nullptr) return false;
    if (parent != invalidSceneEntityId && isDescendant(child, parent)) return false;
    childEntity->parent = parent;
    childEntity->motionHistoryValid = false;
    return true;
}

void Scene::clear() {
    entities_.clear();
}

SceneEntity* Scene::find(SceneEntityId id) {
    const auto entity = std::find_if(
        entities_.begin(), entities_.end(),
        [id](const SceneEntity& candidate) { return candidate.id == id; }
    );
    return entity == entities_.end() ? nullptr : &*entity;
}

const SceneEntity* Scene::find(SceneEntityId id) const {
    const auto entity = std::find_if(
        entities_.begin(), entities_.end(),
        [id](const SceneEntity& candidate) { return candidate.id == id; }
    );
    return entity == entities_.end() ? nullptr : &*entity;
}

void Scene::beginFrame() {
    for (SceneEntity& entity : entities_) {
        entity.motionHistoryValid = entity.worldTransformInitialized;
        if (entity.worldTransformInitialized) {
            entity.previousWorldTransform = entity.worldTransform;
        }
    }
}

void Scene::updateWorldTransforms() {
    std::vector<SceneEntityId> resolving;
    for (SceneEntity& entity : entities_) {
        entity.worldTransform = resolveWorldTransform(entity.id, resolving);
        entity.worldTransformInitialized = true;
    }
}

std::vector<RenderItem> Scene::buildRenderItems() const {
    std::vector<RenderItem> items;
    items.reserve(entities_.size());
    for (const SceneEntity& entity : entities_) {
        if (entity.model == nullptr) continue;
        items.push_back(RenderItem{
            entity.model,
            entity.worldTransform,
            entity.tint,
            entity.visible && entity.enabledByPreset,
            entity.castsShadow,
            entity.instanceCandidate,
            entity.id,
            entity.previousWorldTransform,
            entity.motionHistoryValid
        });
    }
    return items;
}

bool Scene::isDescendant(SceneEntityId ancestor, SceneEntityId candidate) const {
    SceneEntityId current = candidate;
    for (std::size_t depth = 0; depth <= entities_.size(); ++depth) {
        if (current == ancestor) return true;
        const SceneEntity* entity = find(current);
        if (entity == nullptr || entity->parent == invalidSceneEntityId) return false;
        current = entity->parent;
    }
    return true;
}

glm::mat4 Scene::resolveWorldTransform(
    SceneEntityId id,
    std::vector<SceneEntityId>& resolving
) {
    SceneEntity* entity = find(id);
    if (entity == nullptr) return glm::mat4(1.0f);
    if (std::find(resolving.begin(), resolving.end(), id) != resolving.end()) {
        throw std::runtime_error("Scene transform hierarchy contains a cycle");
    }
    resolving.push_back(id);
    glm::mat4 result = entity->transform.matrix();
    if (entity->parent != invalidSceneEntityId) {
        result = resolveWorldTransform(entity->parent, resolving) * result;
    }
    resolving.pop_back();
    return result;
}
