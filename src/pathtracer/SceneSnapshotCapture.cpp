#include "pathtracer/SceneSnapshotCapture.h"

#include <algorithm>
#include <utility>

#include <glm/trigonometric.hpp>
#include <glm/geometric.hpp>

#include "render/Camera.h"
#include "render/GpuModel.h"
#include "render/Renderer.h"
#include "scene/Scene.h"

namespace pathtracer {

SceneSnapshot captureSceneSnapshot(
    const Scene& scene,
    const Camera& camera,
    float aspectRatio,
    SceneSnapshotLighting lighting
) {
    SnapshotCamera snapshotCamera;
    snapshotCamera.position = camera.position();
    snapshotCamera.view = camera.viewMatrix();
    snapshotCamera.aspectRatio = std::max(aspectRatio, 0.01f);
    snapshotCamera.projection = camera.projectionMatrix(snapshotCamera.aspectRatio);
    snapshotCamera.verticalFieldOfViewRadians = glm::radians(camera.fieldOfView());

    SceneSnapshotBuilder builder(snapshotCamera, std::move(lighting));
    for (const SceneEntity& entity : scene.entities()) {
        if (entity.model == nullptr || !entity.visible || !entity.enabledByPreset) continue;
        builder.addModel(
            entity.model->sourceData(),
            entity.id,
            entity.name,
            entity.worldTransform,
            entity.tint,
            entity.castsShadow
        );
    }
    return builder.finish();
}

SceneSnapshotLighting captureSceneLighting(const RendererSettings& settings) {
    SceneSnapshotLighting lighting;
    const float directionLengthSquared = glm::dot(settings.lightDirection, settings.lightDirection);
    lighting.directional.direction = directionLengthSquared > 1.0e-12f
        ? glm::normalize(settings.lightDirection)
        : glm::vec3(0.0f, -1.0f, 0.0f);
    lighting.directional.radiance = glm::vec3(std::max(settings.diffuseStrength, 0.0f));
    lighting.environment.backgroundColor = settings.backgroundColor;
    lighting.environment.intensity = std::max(settings.environmentIntensity, 0.0f);
    lighting.environment.sourceName = settings.iblEnabled
        ? "Active renderer environment"
        : "Background color";
    lighting.localLights.reserve(settings.localLights.size());
    for (const LocalLight& source : settings.localLights) {
        SnapshotLocalLight light;
        light.type = source.type == LocalLightType::Spot
            ? SnapshotLocalLightType::Spot
            : SnapshotLocalLightType::Point;
        light.position = source.position;
        const float localDirectionLengthSquared = glm::dot(source.direction, source.direction);
        light.direction = localDirectionLengthSquared > 1.0e-12f
            ? glm::normalize(source.direction)
            : glm::vec3(0.0f, -1.0f, 0.0f);
        light.radiance = source.color * std::max(source.intensity, 0.0f);
        light.radius = std::max(source.radius, 0.0f);
        light.outerConeCosine = source.outerConeCosine;
        lighting.localLights.push_back(light);
    }
    return lighting;
}

SceneSnapshot captureSceneSnapshot(
    const Scene& scene,
    const Camera& camera,
    float aspectRatio,
    const RendererSettings& settings
) {
    return captureSceneSnapshot(
        scene,
        camera,
        aspectRatio,
        captureSceneLighting(settings)
    );
}

} // namespace pathtracer
