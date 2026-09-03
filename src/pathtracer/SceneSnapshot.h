#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "asset/ModelData.h"
#include "pathtracer/RayGeometry.h"

namespace pathtracer {

struct SnapshotCamera {
    glm::vec3 position{0.0f};
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    float verticalFieldOfViewRadians{0.785398163f};
    float aspectRatio{16.0f / 9.0f};
};

struct SnapshotDirectionalLight {
    glm::vec3 direction{-0.45f, -0.8f, -0.35f};
    glm::vec3 radiance{1.0f};
};

enum class SnapshotLocalLightType {
    Point,
    Spot
};

struct SnapshotLocalLight {
    SnapshotLocalLightType type{SnapshotLocalLightType::Point};
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    glm::vec3 radiance{1.0f};
    float radius{3.0f};
    float outerConeCosine{0.82f};
};

struct SnapshotEnvironment {
    glm::vec3 backgroundColor{0.0f};
    float intensity{1.0f};
    std::string sourceName;
};

struct SceneSnapshotLighting {
    SnapshotDirectionalLight directional;
    std::vector<SnapshotLocalLight> localLights;
    SnapshotEnvironment environment;
};

struct SceneSnapshotAsset {
    std::shared_ptr<const ModelData> model;
};

struct SceneSnapshotInstance {
    std::uint64_t entityId{0U};
    std::string entityName;
    std::uint32_t assetIndex{0U};
    std::uint32_t meshIndex{0U};
    glm::mat4 objectToWorld{1.0f};
    glm::mat3 normalToWorld{1.0f};
    glm::vec3 tint{1.0f};
    bool castsShadow{true};
    bool transformInvertible{true};
    bool skinned{false};
};

class SceneSnapshot {
public:
    const SnapshotCamera& camera() const { return camera_; }
    const SceneSnapshotLighting& lighting() const { return lighting_; }
    const std::vector<SceneSnapshotAsset>& assets() const { return assets_; }
    const std::vector<SceneSnapshotInstance>& instances() const { return instances_; }

private:
    friend class SceneSnapshotBuilder;

    SnapshotCamera camera_;
    SceneSnapshotLighting lighting_;
    std::vector<SceneSnapshotAsset> assets_;
    std::vector<SceneSnapshotInstance> instances_;
};

class SceneSnapshotBuilder {
public:
    SceneSnapshotBuilder(SnapshotCamera camera, SceneSnapshotLighting lighting = {});

    void addModel(
        std::shared_ptr<const ModelData> model,
        std::uint64_t entityId,
        std::string entityName,
        const glm::mat4& entityToWorld,
        const glm::vec3& tint = glm::vec3(1.0f),
        bool castsShadow = true
    );
    SceneSnapshot finish();

private:
    std::uint32_t assetIndex(const std::shared_ptr<const ModelData>& model);
    void addMeshInstance(
        std::uint32_t asset,
        std::uint32_t mesh,
        std::uint64_t entityId,
        const std::string& entityName,
        const glm::mat4& objectToWorld,
        const glm::vec3& tint,
        bool castsShadow
    );

    SceneSnapshot snapshot_;
    std::unordered_map<const ModelData*, std::uint32_t> assetIndices_;
    bool finished_{false};
};

std::vector<Triangle> buildWorldTriangles(const SceneSnapshot& snapshot);

} // namespace pathtracer
