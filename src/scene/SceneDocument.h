#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "optics/PrismDemo.h"
#include "render/Camera.h"
#include "render/Renderer.h"
#include "scene/Scene.h"

inline constexpr const char* myRendererSceneExtension = ".myscene";
inline constexpr const char* builtinGroundResource = "builtin:ground-plane";
inline constexpr const char* builtinGlassBackdropResource = "builtin:glass-checkerboard";

struct SceneDocumentEntity {
    SceneEntityId id{invalidSceneEntityId};
    std::string name{"Entity"};
    SceneEntityId parent{invalidSceneEntityId};
    std::string modelResource;
    SceneTransform transform;
    glm::vec3 tint{1.0f};
    bool visible{true};
    bool castsShadow{true};
    bool instanceCandidate{false};
};

struct ScenePlaybackSettings {
    bool animationEnabled{false};
    bool animationPlaying{true};
    float animationTimeSeconds{0.0f};
    float animationSpeed{1.0f};
    std::size_t animationClipIndex{0U};
    bool prismEnabled{false};
    bool prismCameraLocked{false};
    PrismOpticalPreset prismPreset{PrismOpticalPreset::CrownGlass};
    PrismDemoParameters prismParameters{};
};

struct SceneDocument {
    static constexpr int currentVersion = 1;

    CameraOrbitState camera;
    RendererSettings renderer;
    ScenePlaybackSettings playback;
    std::vector<SceneDocumentEntity> entities;
};

bool saveSceneDocument(
    const std::filesystem::path& path,
    const SceneDocument& document,
    std::string& error
);

bool loadSceneDocument(
    const std::filesystem::path& path,
    SceneDocument& document,
    std::string& error
);

std::string makeSceneRelativeResource(
    const std::string& resource,
    const std::filesystem::path& scenePath
);

std::filesystem::path resolveSceneResource(
    const std::string& resource,
    const std::filesystem::path& scenePath
);
