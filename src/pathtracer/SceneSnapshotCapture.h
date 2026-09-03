#pragma once

#include "pathtracer/SceneSnapshot.h"

class Camera;
class Scene;
struct RendererSettings;

namespace pathtracer {

SceneSnapshot captureSceneSnapshot(
    const Scene& scene,
    const Camera& camera,
    float aspectRatio,
    SceneSnapshotLighting lighting = {}
);

SceneSnapshotLighting captureSceneLighting(const RendererSettings& settings);

SceneSnapshot captureSceneSnapshot(
    const Scene& scene,
    const Camera& camera,
    float aspectRatio,
    const RendererSettings& settings
);

} // namespace pathtracer
