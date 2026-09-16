#include "app/Application.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include "app/EditorUi.h"
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "app/AppIcon.h"
#include "app/FileDialog.h"
#include "io/AssimpImporter.h"
#include "io/ModelImporter.h"
#include "io/ObjLoader.h"
#include "optics/PrismDemo.h"
#include "optics/PrismOptics.h"
#include "pathtracer/ProgressiveRenderer.h"
#include "pathtracer/ReferenceComparison.h"
#include "pathtracer/SceneSnapshotCapture.h"
#include "render/GpuModel.h"
#include "render/Shader.h"
#include "render/OpenGlDebug.h"
#include "render/Renderer.h"
#include "scene/SceneDocument.h"

namespace {

std::filesystem::path findRuntimeRoot() {
    std::vector<std::filesystem::path> candidates;
#ifdef _WIN32
    std::wstring executablePath(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr,
        executablePath.data(),
        static_cast<DWORD>(executablePath.size())
    );
    if (length > 0 && length < executablePath.size()) {
        executablePath.resize(length);
        candidates.push_back(std::filesystem::path(executablePath).parent_path());
    }
#endif
    std::error_code error;
    candidates.push_back(std::filesystem::current_path(error));
    candidates.emplace_back(MYRENDERER_SOURCE_DIR);
    for (const std::filesystem::path& candidate : candidates) {
        if (!candidate.empty()
            && std::filesystem::is_directory(candidate / "shaders", error)
            && std::filesystem::is_directory(candidate / "assets", error)) {
            return std::filesystem::absolute(candidate, error).lexically_normal();
        }
    }
    return std::filesystem::path(MYRENDERER_SOURCE_DIR);
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

const char* glString(unsigned int name) {
    const auto* value = glGetString(name);
    return value == nullptr ? "Unknown" : reinterpret_cast<const char*>(value);
}

const char* diagnosticScopeName(ModelDiagnosticScope scope) {
    switch (scope) {
    case ModelDiagnosticScope::File: return "File";
    case ModelDiagnosticScope::Node: return "Node";
    case ModelDiagnosticScope::Mesh: return "Mesh";
    case ModelDiagnosticScope::Material: return "Material";
    case ModelDiagnosticScope::Texture: return "Texture";
    }
    return "Unknown";
}

const char* diagnosticSeverityName(ModelDiagnosticSeverity severity) {
    switch (severity) {
    case ModelDiagnosticSeverity::Info: return "Info";
    case ModelDiagnosticSeverity::Warning: return "Warning";
    case ModelDiagnosticSeverity::Error: return "Error";
    }
    return "Unknown";
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(std::ceil(
        std::clamp(fraction, 0.0, 1.0) * static_cast<double>(values.size())
    ));
    return values[std::min(index > 0U ? index - 1U : 0U, values.size() - 1U)];
}

ModelData makeGroundPlaneData() {
    constexpr float halfExtent = 4.0f;
    MeshData mesh;
    mesh.name = "Ground receiver";
    mesh.vertices = {
        Vertex{glm::vec3(-halfExtent, 0.0f, -halfExtent), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.0f, 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)},
        Vertex{glm::vec3( halfExtent, 0.0f, -halfExtent), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(4.0f, 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)},
        Vertex{glm::vec3( halfExtent, 0.0f,  halfExtent), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(4.0f, 4.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)},
        Vertex{glm::vec3(-halfExtent, 0.0f,  halfExtent), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.0f, 4.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)}
    };
    mesh.indices = {0U, 2U, 1U, 0U, 3U, 2U};
    mesh.submeshes.push_back(SubmeshData{"Ground receiver", 0U, 6U, 0});
    mesh.boundsMin = glm::vec3(-halfExtent, 0.0f, -halfExtent);
    mesh.boundsMax = glm::vec3(halfExtent, 0.0f, halfExtent);

    MaterialData material;
    material.name = "Ground matte";
    material.roughnessFactor = 0.82f;
    material.metallicFactor = 0.0f;

    ModelData model;
    model.name = "Procedural ground receiver";
    model.meshes.push_back(std::move(mesh));
    model.materials.push_back(std::move(material));
    model.rootNode.name = "Ground root";
    model.rootNode.meshIndices.push_back(0U);
    model.boundsMin = glm::vec3(-halfExtent, 0.0f, -halfExtent);
    model.boundsMax = glm::vec3(halfExtent, 0.0f, halfExtent);
    return model;
}

ModelData makeGlassCheckerboardData() {
    constexpr int columns = 10;
    constexpr int rows = 7;
    constexpr float cellSize = 0.34f;
    MeshData mesh;
    mesh.name = "Glass-2C checkerboard backdrop";
    std::vector<std::uint32_t> darkIndices;
    std::vector<std::uint32_t> lightIndices;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const float left = (static_cast<float>(column) - columns * 0.5f) * cellSize;
            const float right = left + cellSize;
            const float bottom = (static_cast<float>(row) - rows * 0.5f) * cellSize;
            const float top = bottom + cellSize;
            const std::uint32_t first = static_cast<std::uint32_t>(mesh.vertices.size());
            const glm::vec3 normal(0.0f, 0.0f, 1.0f);
            const glm::vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);
            mesh.vertices.push_back(Vertex{glm::vec3(left, bottom, -1.05f), normal, glm::vec2(0.0f), tangent});
            mesh.vertices.push_back(Vertex{glm::vec3(right, bottom, -1.05f), normal, glm::vec2(1.0f, 0.0f), tangent});
            mesh.vertices.push_back(Vertex{glm::vec3(right, top, -1.05f), normal, glm::vec2(1.0f), tangent});
            mesh.vertices.push_back(Vertex{glm::vec3(left, top, -1.05f), normal, glm::vec2(0.0f, 1.0f), tangent});
            std::vector<std::uint32_t>& target = ((row + column) % 2 == 0)
                ? lightIndices
                : darkIndices;
            target.insert(target.end(), {first, first + 1U, first + 2U, first, first + 2U, first + 3U});
        }
    }
    mesh.indices = darkIndices;
    mesh.indices.insert(mesh.indices.end(), lightIndices.begin(), lightIndices.end());
    mesh.submeshes.push_back(SubmeshData{
        "Dark checks",
        0U,
        static_cast<std::uint32_t>(darkIndices.size()),
        0
    });
    mesh.submeshes.push_back(SubmeshData{
        "Light checks",
        static_cast<std::uint32_t>(darkIndices.size()),
        static_cast<std::uint32_t>(lightIndices.size()),
        1
    });
    mesh.boundsMin = glm::vec3(-columns * cellSize * 0.5f, -rows * cellSize * 0.5f, -1.05f);
    mesh.boundsMax = glm::vec3(columns * cellSize * 0.5f, rows * cellSize * 0.5f, -1.05f);

    MaterialData dark;
    dark.name = "Checker charcoal";
    dark.baseColorFactor = glm::vec4(0.035f, 0.045f, 0.055f, 1.0f);
    dark.roughnessFactor = 0.78f;
    MaterialData light;
    light.name = "Checker ivory";
    light.baseColorFactor = glm::vec4(0.82f, 0.78f, 0.66f, 1.0f);
    light.roughnessFactor = 0.72f;

    ModelData model;
    model.name = "Procedural Glass-2C checkerboard";
    model.meshes.push_back(std::move(mesh));
    model.materials.push_back(std::move(dark));
    model.materials.push_back(std::move(light));
    model.rootNode.name = "Checkerboard root";
    model.rootNode.meshIndices.push_back(0U);
    model.boundsMin = model.meshes.front().boundsMin;
    model.boundsMax = model.meshes.front().boundsMax;
    return model;
}

} // namespace

Application::Application()
    : sourceRoot_(findRuntimeRoot()) {
}

Application::~Application() {
    shutdown();
}

int Application::run(const std::filesystem::path& initialModel) {
    benchmarkMode_ = std::getenv("MYRENDERER_BENCHMARK_FRAMES") != nullptr
        || std::getenv("MYRENDERER_BENCHMARK_OUTPUT") != nullptr;
    if (benchmarkMode_) {
        vsync_ = false;
        if (const char* value = std::getenv("MYRENDERER_BENCHMARK_FRAMES")) {
            benchmarkMeasurementFrames_ = std::max(std::atoi(value), 30);
        }
        if (const char* value = std::getenv("MYRENDERER_BENCHMARK_WARMUP")) {
            benchmarkWarmupFrames_ = std::max(std::atoi(value), 4);
        }
        benchmarkOutputPath_ = std::getenv("MYRENDERER_BENCHMARK_OUTPUT") == nullptr
            ? std::filesystem::absolute("prism-benchmark.json")
            : std::filesystem::absolute(std::getenv("MYRENDERER_BENCHMARK_OUTPUT"));
    }
    if (const char* value = std::getenv("MYRENDERER_PRISM_REEL_DIR")) {
        prismReelMode_ = true;
        vsync_ = false;
        prismReelFramesDirectory_ = std::filesystem::absolute(value).lexically_normal();
        std::filesystem::create_directories(prismReelFramesDirectory_);
        if (const char* frameCount = std::getenv("MYRENDERER_PRISM_REEL_FRAMES")) {
            prismReelFrameCount_ = std::clamp(std::atoi(frameCount), 24, 1440);
        }
    }
    if (const char* value = std::getenv("MYRENDERER_RENDER_WIDTH")) {
        renderWidthOverride_ = std::clamp(std::atoi(value), 64, 7680);
    } else if (benchmarkMode_) {
        renderWidthOverride_ = 1920;
    } else if (prismReelMode_) {
        renderWidthOverride_ = 1280;
    }
    if (const char* value = std::getenv("MYRENDERER_RENDER_HEIGHT")) {
        renderHeightOverride_ = std::clamp(std::atoi(value), 64, 4320);
    } else if (benchmarkMode_) {
        renderHeightOverride_ = 1080;
    } else if (prismReelMode_) {
        renderHeightOverride_ = 720;
    }
    initializeWindow();
    initializeGui();
    initializeRenderer();
    initializeImporters();
    discoverModels();
    if (const char* msaa = std::getenv("MYRENDERER_MSAA")) {
        rendererSettings_.msaaSamples = std::atoi(msaa) <= 1 ? 1 : 4;
    }
    if (const char* value = std::getenv("MYRENDERER_RENDER_PATH")) {
        rendererSettings_.renderPath = std::atoi(value) == 0
            ? RenderPath::Forward
            : RenderPath::Deferred;
    }
    if (const char* value = std::getenv("MYRENDERER_GBUFFER_DEBUG")) {
        rendererSettings_.gBufferDebugView = static_cast<GBufferDebugView>(
            std::clamp(std::atoi(value), 0, 5)
        );
    }
    if (const char* value = std::getenv("MYRENDERER_SSAO")) {
        rendererSettings_.ssaoEnabled = std::atoi(value) != 0;
    }
    if (const char* value = std::getenv("MYRENDERER_SSAO_RADIUS")) {
        rendererSettings_.ssaoRadius = std::clamp(std::strtof(value, nullptr), 0.05f, 2.0f);
    }
    if (const char* value = std::getenv("MYRENDERER_SSAO_BIAS")) {
        rendererSettings_.ssaoBias = std::clamp(std::strtof(value, nullptr), 0.0f, 0.15f);
    }
    if (const char* value = std::getenv("MYRENDERER_SSAO_STRENGTH")) {
        rendererSettings_.ssaoStrength = std::clamp(std::strtof(value, nullptr), 0.1f, 3.0f);
    }
    if (const char* value = std::getenv("MYRENDERER_TAA")) {
        rendererSettings_.temporalAaEnabled = std::atoi(value) != 0;
    }
    if (const char* value = std::getenv("MYRENDERER_TAA_HISTORY_WEIGHT")) {
        rendererSettings_.temporalHistoryWeight = std::clamp(
            std::strtof(value, nullptr), 0.0f, 0.98f
        );
    }
    if (const char* value = std::getenv("MYRENDERER_TAA_DEBUG")) {
        rendererSettings_.temporalDebugView = std::clamp(std::atoi(value), 0, 2);
    }
    if (const char* value = std::getenv("MYRENDERER_TAA_MOTION_DEMO")) {
        temporalMotionDemoEnabled_ = std::atoi(value) != 0;
    }
    if (const char* value = std::getenv("MYRENDERER_OBJECT_MOTION_DEMO")) {
        objectMotionDemoEnabled_ = std::atoi(value) != 0;
    }
    if (const char* value = std::getenv("MYRENDERER_SCENE_FOUNDATION_DEMO")) {
        sceneFoundationDemoEnabled_ = std::atoi(value) != 0;
    }
    if (const char* value = std::getenv("MYRENDERER_ANIMATION_DEMO")) {
        animationDemoEnabled_ = std::atoi(value) != 0;
        animationEnabled_ = animationDemoEnabled_;
    }
    if (const char* value = std::getenv("MYRENDERER_ANIMATION")) {
        animationEnabled_ = std::atoi(value) != 0;
    }
    if (const char* value = std::getenv("MYRENDERER_ANIMATION_TIME")) {
        animationTimeSeconds_ = std::max(std::strtof(value, nullptr), 0.0f);
        animationTimeFixed_ = true;
        animationPlaying_ = false;
    }
    if (const char* value = std::getenv("MYRENDERER_ANIMATION_FRAME_STEP")) {
        animationFrameStep_ = std::max(std::strtof(value, nullptr), 0.0f);
        animationPlaying_ = animationFrameStep_ > 0.0f;
        animationTimeFixed_ = false;
    }
    if (const char* value = std::getenv("MYRENDERER_SKIN_DEBUG")) {
        rendererSettings_.skinningDebugView = std::clamp(std::atoi(value), 0, 2);
    }
    if (const char* value = std::getenv("MYRENDERER_PBR")) rendererSettings_.pbrEnabled = std::atoi(value) != 0;
    if (const char* value = std::getenv("MYRENDERER_IBL")) rendererSettings_.iblEnabled = std::atoi(value) != 0;
    if (const char* value = std::getenv("MYRENDERER_SHADOWS")) rendererSettings_.shadowsEnabled = std::atoi(value) != 0;
    if (const char* value = std::getenv("MYRENDERER_BLOOM")) rendererSettings_.bloom = std::atoi(value) != 0;
    if (const char* value = std::getenv("MYRENDERER_GRID")) rendererSettings_.showGrid = std::atoi(value) != 0;
    if (const char* value = std::getenv("MYRENDERER_AXES")) rendererSettings_.showAxes = std::atoi(value) != 0;
    if (const char* value = std::getenv("MYRENDERER_GROUND")) showGroundPlane_ = std::atoi(value) != 0;
    if (const char* value = std::getenv("MYRENDERER_TRANSMISSION")) rendererSettings_.transmissionEnabled = std::atoi(value) != 0;
    if (const char* value = std::getenv("MYRENDERER_REFRACTION_SCALE")) {
        rendererSettings_.refractionScale = std::clamp(std::strtof(value, nullptr), 0.0f, 0.8f);
    }
    if (const char* value = std::getenv("MYRENDERER_REFRACTION_STEPS")) {
        rendererSettings_.refractionSteps = std::clamp(std::atoi(value), 4, 32);
    }
    if (const char* value = std::getenv("MYRENDERER_VOLUME_THICKNESS_SCALE")) {
        rendererSettings_.volumeThicknessScale = std::clamp(std::strtof(value, nullptr), 0.0f, 4.0f);
    }
    if (const char* value = std::getenv("MYRENDERER_GEOMETRIC_THICKNESS")) {
        rendererSettings_.geometricThicknessEnabled = std::atoi(value) != 0;
    }
    if (const char* value = std::getenv("MYRENDERER_TWO_INTERFACE_REFRACTION")) {
        rendererSettings_.twoInterfaceRefractionEnabled = std::atoi(value) != 0;
    }
    if (const char* value = std::getenv("MYRENDERER_DISPERSION")) {
        rendererSettings_.dispersionStrength = std::clamp(std::strtof(value, nullptr), 0.0f, 2.5f);
    }
    if (const char* value = std::getenv("MYRENDERER_DISPERSION_ENABLED")) {
        rendererSettings_.dispersionEnabled = std::atoi(value) != 0;
    }
    if (const char* value = std::getenv("MYRENDERER_IOR")) {
        rendererSettings_.indexOfRefractionOverride = std::clamp(
            std::strtof(value, nullptr), 1.0f, 3.0f
        );
    }
    if (const char* value = std::getenv("MYRENDERER_GLASS_PRESET")) {
        volumeGlassPreset_ = static_cast<VolumeGlassPreset>(
            std::clamp(std::atoi(value), 0, 3)
        );
    }
    if (const char* value = std::getenv("MYRENDERER_GLASS_DEBUG")) {
        rendererSettings_.glassDebugView = static_cast<GlassDebugView>(
            std::clamp(std::atoi(value), 0, 12)
        );
    }
    if (const char* value = std::getenv("MYRENDERER_CAUSTICS")) {
        rendererSettings_.causticsEnabled = std::atoi(value) != 0;
    }
    if (const char* value = std::getenv("MYRENDERER_CAUSTICS_MODE")) {
        rendererSettings_.causticsMode = std::atoi(value) == 0
            ? CausticsMode::Projector
            : CausticsMode::LightSpace;
    }
    if (const char* value = std::getenv("MYRENDERER_TRANSMISSION_SHADOWS")) {
        rendererSettings_.coloredTransmissionShadowsEnabled = std::atoi(value) != 0;
    }
    if (const char* value = std::getenv("MYRENDERER_GLASS3_DEMO")) {
        glassCausticsDemoEnabled_ = std::atoi(value) != 0;
        if (glassCausticsDemoEnabled_ && std::getenv("MYRENDERER_GLASS_PRESET") == nullptr) {
            volumeGlassPreset_ = VolumeGlassPreset::Crystal;
        }
    }
    if (const char* value = std::getenv("MYRENDERER_SCENE_DEMO")) showComparisonObject_ = std::atoi(value) != 0;
    if (const char* value = std::getenv("MYRENDERER_LIGHT_STRESS")) {
        lightStressDemoEnabled_ = std::atoi(value) != 0;
    }
    if (const char* value = std::getenv("MYRENDERER_INSTANCE_STRESS")) {
        instanceStressDemoEnabled_ = std::atoi(value) != 0;
    }
    if (const char* value = std::getenv("MYRENDERER_INSTANCE_OPTIMIZATION")) {
        rendererSettings_.instanceOptimizationEnabled = std::atoi(value) != 0;
    } else if (instanceStressDemoEnabled_) {
        rendererSettings_.instanceOptimizationEnabled = true;
    }
    if (const char* value = std::getenv("MYRENDERER_FRUSTUM_CULLING")) {
        rendererSettings_.frustumCullingEnabled = std::atoi(value) != 0;
    }
    if (const char* value = std::getenv("MYRENDERER_LOD")) {
        rendererSettings_.lodSelectionEnabled = std::atoi(value) != 0;
    }
    if (const char* value = std::getenv("MYRENDERER_LOCAL_LIGHT_TIER")) {
        localLightTierIndex_ = std::clamp(std::atoi(value), 0, 2);
    }
    if (const char* value = std::getenv("MYRENDERER_PRISM_DEMO")) {
        prismDemoEnabled_ = std::atoi(value) != 0;
    }
    if (prismReelMode_) prismDemoEnabled_ = true;
    if (prismDemoEnabled_) {
        activatePrismDemoPreset(false);
    } else if (instanceStressDemoEnabled_ && !glassCausticsDemoEnabled_) {
        activateInstanceStressPreset(false);
    } else if (lightStressDemoEnabled_ && !glassCausticsDemoEnabled_) {
        activateLightStressPreset(false);
    }

    const bool initialSceneRequested = !initialModel.empty()
        && lowercase(initialModel.extension().string()) == myRendererSceneExtension;
    bool initialSceneLoaded = false;
    if (initialSceneRequested) {
        initialSceneLoaded = openScene(initialModel);
    }
    std::filesystem::path modelToLoad = initialSceneRequested ? std::filesystem::path{} : initialModel;
    if (modelToLoad.empty() && !initialSceneRequested) {
        const auto defaultModel = animationDemoEnabled_
            ? sourceRoot_ / "assets" / "models" / "skinning_test.gltf"
            : (prismDemoEnabled_
            ? sourceRoot_ / "assets" / "models" / "prism_spectrum.gltf"
            : (glassCausticsDemoEnabled_
                ? sourceRoot_ / "assets" / "models" / "glass_volume_sphere.gltf"
                : (instanceStressDemoEnabled_
                    ? sourceRoot_ / "assets" / "models" / "sphere.obj"
                    : sourceRoot_ / "assets" / "models" / "cube.obj")));
        modelToLoad = std::filesystem::exists(defaultModel)
            ? defaultModel
            : (availableModels_.empty() ? std::filesystem::path{} : availableModels_.front());
    }
    if (initialSceneLoaded) {
        // openScene already populated the status and the complete scene session.
    } else if (!modelToLoad.empty()) {
        loadModel(modelToLoad);
    } else if (!initialSceneRequested) {
        statusMessage_ = "No supported model was found in assets/models";
    }

    // Apply automation overrides after scene loading so a fixed .myscene can
    // be captured in both PBR and Stylized modes without duplicating assets or
    // camera data.
    if (const char* value = std::getenv("MYRENDERER_STYLIZED")) {
        rendererSettings_.shadingMode = std::atoi(value) == 0
            ? ShadingMode::PhysicallyBased
            : ShadingMode::Stylized;
    }
    if (const char* value = std::getenv("MYRENDERER_STYLIZED_BANDS")) {
        rendererSettings_.stylizedBandCount = std::clamp(std::atoi(value), 2, 8);
    }
    if (const char* value = std::getenv("MYRENDERER_STYLIZED_OUTLINE")) {
        rendererSettings_.stylizedOutlineEnabled = std::atoi(value) != 0;
    }
    if (const char* value = std::getenv("MYRENDERER_STYLIZED_OUTLINE_WIDTH")) {
        rendererSettings_.stylizedOutlineWidth = std::clamp(
            std::strtof(value, nullptr), 0.5f, 6.0f
        );
    }
    if (const char* value = std::getenv("MYRENDERER_RENDER_PATH")) {
        rendererSettings_.renderPath = std::atoi(value) == 0
            ? RenderPath::Forward
            : RenderPath::Deferred;
    }
    if (const char* value = std::getenv("MYRENDERER_HIDE_SELECTION_OUTLINE")) {
        if (std::atoi(value) != 0) selectedSceneEntity_ = invalidSceneEntityId;
    }

    if (const char* value = std::getenv("MYRENDERER_REFERENCE_COMPARE_DIR")) {
        referenceComparisonMode_ = true;
        vsync_ = false;
        referenceComparisonDirectory_ = std::filesystem::absolute(value).lexically_normal();
        if (const char* samples = std::getenv("MYRENDERER_REFERENCE_SPP")) {
            referenceComparisonSamples_ = static_cast<std::uint32_t>(
                std::clamp(std::atoi(samples), 1, 4096)
            );
        }
        if (const char* depth = std::getenv("MYRENDERER_REFERENCE_MAX_DEPTH")) {
            referenceComparisonMaxDepth_ = static_cast<std::uint32_t>(
                std::clamp(std::atoi(depth), 1, 64)
            );
        }
        if (const char* seed = std::getenv("MYRENDERER_REFERENCE_SEED")) {
            referenceComparisonSeed_ = static_cast<std::uint32_t>(std::strtoul(seed, nullptr, 10));
        }
        if (const char* warmup = std::getenv("MYRENDERER_REFERENCE_WARMUP")) {
            referenceComparisonWarmupFrames_ = std::clamp(std::atoi(warmup), 1, 240);
        }
        // Keep display transforms shared but remove raster-only temporal and
        // screen-space decoration from the algorithm comparison.
        rendererSettings_.bloom = false;
        rendererSettings_.temporalAaEnabled = false;
        rendererSettings_.ssaoEnabled = false;
        rendererSettings_.showGrid = false;
        rendererSettings_.showAxes = false;
        rendererSettings_.gBufferDebugView = GBufferDebugView::Final;
        rendererSettings_.temporalDebugView = 0;
        selectedSceneEntity_ = invalidSceneEntityId;
    }

    if (const char* screenshotPath = std::getenv("MYRENDERER_SCREENSHOT")) {
        pendingScreenshotPath_ = std::filesystem::absolute(screenshotPath).lexically_normal();
        // Let newly uploaded materials and driver-specialized shader state settle
        // before recording an automated visual baseline.
        pendingScreenshotWarmupFrames_ = 2;
        if (const char* value = std::getenv("MYRENDERER_SCREENSHOT_WARMUP")) {
            pendingScreenshotWarmupFrames_ = std::clamp(std::atoi(value), 1, 240);
        }
    }
    if (const char* screenshotPath = std::getenv("MYRENDERER_EDITOR_SCREENSHOT")) {
        pendingEditorScreenshotPath_ = std::filesystem::absolute(screenshotPath).lexically_normal();
        pendingEditorScreenshotWarmupFrames_ = 2;
        if (const char* value = std::getenv("MYRENDERER_EDITOR_SCREENSHOT_WARMUP")) {
            pendingEditorScreenshotWarmupFrames_ = std::clamp(std::atoi(value), 1, 240);
        }
        if (const char* tab = std::getenv("MYRENDERER_EDITOR_SCREENSHOT_TAB")) {
            focusRendererTab_ = std::strcmp(tab, "renderer") == 0;
        }
    }
    const char* recoveryModelValue = std::getenv("MYRENDERER_RECOVERY_TEST");
    const std::filesystem::path recoveryModel = recoveryModelValue == nullptr
        ? std::filesystem::path{}
        : std::filesystem::path(recoveryModelValue);
    bool recoveryScheduled = recoveryModel.empty();

    int smokeTestFrames = std::getenv("MYRENDERER_SMOKE_TEST") == nullptr ? -1 : 5;
    if (const char* extra = std::getenv("MYRENDERER_APPEND_TEST")) {
        droppedModelPaths_.push_back(std::filesystem::u8path(extra));
    }
    previousFrameTime_ = glfwGetTime();
    while (!glfwWindowShouldClose(window_)) {
        const double cpuFrameStart = glfwGetTime();
        glfwPollEvents();
        if (!droppedModelPaths_.empty() && !pendingModelImport_.has_value()) {
            const std::filesystem::path dropped = std::move(droppedModelPaths_.front());
            droppedModelPaths_.pop_front();
            loadModel(dropped, true);
        }
        updateModelLoad();
        if (!recoveryScheduled && model_ != nullptr && !pendingModelImport_.has_value()) {
            recoveryScheduled = loadModel(recoveryModel);
        }
        if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }

        const double currentTime = glfwGetTime();
        const float deltaTime = static_cast<float>(std::min(currentTime - previousFrameTime_, 0.1));
        previousFrameTime_ = currentTime;
        if (autoRotate_) {
            modelRotationDegrees_.y = std::fmod(modelRotationDegrees_.y + 25.0f * deltaTime, 360.0f);
        }
        if (objectMotionDemoEnabled_ && model_ != nullptr && !pendingModelImport_.has_value()) {
            modelRotationDegrees_.y = static_cast<float>(objectMotionDemoFrame_++ * 6);
        }
        if (temporalMotionDemoEnabled_) {
            camera_.orbit(0.012f, 0.0f);
        }
        if (model_ != nullptr && model_->hasSkinning()) {
            if (animationEnabled_ && animationFrameStep_ > 0.0f) {
                animationTimeSeconds_ = static_cast<float>(animationDemoFrame_++) * animationFrameStep_;
            } else if (animationEnabled_ && animationPlaying_ && !animationTimeFixed_) {
                animationTimeSeconds_ += deltaTime * animationSpeed_;
            }
            model_->updateAnimation(
                animationEnabled_,
                animationClipIndex_,
                animationTimeSeconds_
            );
            model_->setSkinningDebugView(rendererSettings_.skinningDebugView);
        }
        if (prismReelMode_ && model_ != nullptr && !pendingModelImport_.has_value()
            && prismReelWarmupFrames_ == 0) {
            updatePrismReelFrame();
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        drawMainMenu();
        drawEditorLayout();
        if (hierarchyPanelOpen_) drawScenePanel();
        if (assetsPanelOpen_) drawAssetsPanel();
        drawViewportPanel();
        if (inspectorPanelOpen_) drawInspectorPanel();
        drawAboutPopup();
        if (showImGuiDemo_) {
            ImGui::ShowDemoWindow(&showImGuiDemo_);
        }

        for (auto it = importedModels_.begin(); it != importedModels_.end();) {
            const GpuModel* asset = it->get();
            const bool used = std::any_of(scene_.entities().begin(), scene_.entities().end(),
                [asset](const SceneEntity& entity) { return entity.model == asset; });
            if (!used) it = importedModels_.erase(it); else ++it;
        }
        ImGui::Render();
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window_, &framebufferWidth, &framebufferHeight);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, framebufferWidth, framebufferHeight);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glClearColor(0.035f, 0.04f, 0.055f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (!pendingEditorScreenshotPath_.empty() && !pendingModelImport_.has_value()
            && !scene_.entities().empty() && pendingEditorScreenshotWarmupFrames_ > 0) {
            --pendingEditorScreenshotWarmupFrames_;
        } else if (!pendingEditorScreenshotPath_.empty() && !pendingModelImport_.has_value()
            && !scene_.entities().empty()) {
            std::string screenshotError;
            if (renderer_->saveEditorScreenshot(
                    pendingEditorScreenshotPath_,
                    framebufferWidth,
                    framebufferHeight,
                    screenshotError
                )) {
                std::cout << "Saved editor screenshot: "
                          << pendingEditorScreenshotPath_.string() << '\n';
            } else {
                std::cerr << "Editor screenshot failed: " << screenshotError << '\n';
            }
            pendingEditorScreenshotPath_.clear();
        }
        glfwSwapBuffers(window_);
        const double measuredCpuTime = (glfwGetTime() - cpuFrameStart) * 1000.0;
        cpuFrameTimeMilliseconds_ = cpuFrameTimeMilliseconds_ > 0.0
            ? cpuFrameTimeMilliseconds_ * 0.9 + measuredCpuTime * 0.1
            : measuredCpuTime;
        if (benchmarkMode_ && model_ != nullptr && !pendingModelImport_.has_value()) {
            ++benchmarkRenderedFrames_;
            if (benchmarkRenderedFrames_ > benchmarkWarmupFrames_) {
                benchmarkCpuFrameTimes_.push_back(measuredCpuTime);
                if (renderer_->gpuFrameMeasurementSerial() != lastBenchmarkGpuFrameSerial_) {
                    lastBenchmarkGpuFrameSerial_ = renderer_->gpuFrameMeasurementSerial();
                    benchmarkGpuFrameTimes_.push_back(
                        renderer_->latestGpuFrameMeasurementMilliseconds()
                    );
                }
                if (renderer_->prismBeamMeasurementSerial() != lastBenchmarkBeamSerial_) {
                    lastBenchmarkBeamSerial_ = renderer_->prismBeamMeasurementSerial();
                    benchmarkBeamGpuTimes_.push_back(
                        renderer_->latestPrismBeamMeasurementMilliseconds()
                    );
                }
                if (renderer_->causticsMeasurementSerial() != lastBenchmarkCausticsSerial_) {
                    lastBenchmarkCausticsSerial_ = renderer_->causticsMeasurementSerial();
                    benchmarkCausticsGpuTimes_.push_back(
                        renderer_->latestCausticsMeasurementMilliseconds()
                    );
                }
                for (const GpuPassTiming& timing : renderer_->gpuPassTimings()) {
                    std::size_t& lastSerial = lastBenchmarkPassSerials_[timing.name];
                    if (timing.measurementSerial == lastSerial) continue;
                    lastSerial = timing.measurementSerial;
                    benchmarkPassGpuTimes_[timing.name].push_back(
                        timing.latestMilliseconds
                    );
                }
            }
            if (benchmarkRenderedFrames_
                >= benchmarkWarmupFrames_ + benchmarkMeasurementFrames_) {
                writePrismBenchmarkReport();
                glfwSetWindowShouldClose(window_, GLFW_TRUE);
            }
        }
        if (smokeTestFrames > 0 && !pendingModelImport_.has_value() && droppedModelPaths_.empty() && --smokeTestFrames == 0) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
    }

    const bool recoveryPassed = recoveryModel.empty()
        || (recoveryScheduled && lastLoadFailed_ && model_ != nullptr);
    bool appendPassed = true;
    if (std::getenv("MYRENDERER_APPEND_TEST") && !lastLoadFailed_) {
        const SceneEntity* primary = scene_.find(primaryEntity_);
        const SceneEntity* added = scene_.find(selectedSceneEntity_);
        appendPassed = primary && added && primary->id != added->id
            && primary->model != added->model && importedModels_.size() == 1;
        const auto items = scene_.buildRenderItems();
        appendPassed = appendPassed && items.size() >= 2;
        std::cout << "Append scene validation: " << (appendPassed ? "PASS" : "FAIL") << '\n';
    }
    const bool interactionsPassed = !std::getenv("MYRENDERER_EDITOR_INTERACTION_TEST") || editorInteractionRegression();
    const bool referenceComparisonPassed = !referenceComparisonMode_
        || (referenceComparisonComplete_ && !referenceComparisonFailed_);
    shutdown();
    return recoveryPassed && appendPassed && interactionsPassed && referenceComparisonPassed ? 0 : 2;
}

void Application::initializeWindow() {
    initializeMyRendererApplicationIdentity();
    glfwSetErrorCallback([](int code, const char* description) {
        std::cerr << "GLFW error " << code << ": " << description << '\n';
    });
    if (glfwInit() != GLFW_TRUE) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 0);
    if (std::getenv("MYRENDERER_SMOKE_TEST") != nullptr || benchmarkMode_ || prismReelMode_) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    window_ = glfwCreateWindow(1440, 900, "MyRenderer - OpenGL Rasterizer", nullptr, nullptr);
    if (window_ == nullptr) {
        glfwTerminate();
        throw std::runtime_error("Failed to create an OpenGL 3.3 window");
    }
    setMyRendererWindowIcon(window_);
    glfwSetWindowUserPointer(window_, this);
    glfwSetDropCallback(window_, [](GLFWwindow* window, int count, const char** paths) {
        auto* application = static_cast<Application*>(glfwGetWindowUserPointer(window));
        if (application != nullptr) {
            application->queueDroppedFiles(count, paths);
        }
    });
    // Keep enough room for two usable side panels and a complete viewport toolbar.
    glfwSetWindowSizeLimits(window_, 1100, 680, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(vsync_ ? 1 : 0);

    const int version = gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress));
    if (version == 0) {
        throw std::runtime_error("Failed to load OpenGL functions through GLAD");
    }

    initializeOpenGlDebugOutput();

    gpuDescription_ = std::string(glString(GL_RENDERER)) + " | OpenGL " + glString(GL_VERSION);
    std::cout << "GPU: " << glString(GL_RENDERER) << '\n'
              << "Vendor: " << glString(GL_VENDOR) << '\n'
              << "OpenGL: " << glString(GL_VERSION) << '\n';

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
}

void Application::initializeGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    const bool automatedRun = std::getenv("MYRENDERER_SMOKE_TEST") != nullptr
        || benchmarkMode_ || prismReelMode_;
    // Hidden regression/benchmark windows must not overwrite the interactive layout.
    io.IniFilename = automatedRun ? nullptr : "MyRenderer.editor.ini";

    EditorUi::initialize(io);
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.PopupRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(7.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.CellPadding = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 8.0f;
    style.WindowMinSize = EditorUi::minimumDockedPanelSize;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.86f, 0.87f, 0.89f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.48f, 0.50f, 0.54f, 1.0f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.078f, 0.086f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.068f, 0.071f, 0.078f, 1.0f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.094f, 0.102f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.23f, 0.24f, 0.27f, 1.0f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.125f, 0.138f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.17f, 0.18f, 0.20f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.21f, 0.23f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.055f, 0.057f, 0.063f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.09f, 0.094f, 0.103f, 1.0f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.085f, 0.088f, 0.096f, 1.0f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.055f, 0.057f, 0.063f, 1.0f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.26f, 0.29f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.34f, 0.35f, 0.38f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.42f, 0.44f, 0.48f, 1.0f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.30f, 0.62f, 1.0f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.35f, 0.63f, 0.96f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.48f, 0.73f, 1.0f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.14f, 0.145f, 0.16f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.21f, 0.23f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.11f, 0.115f, 0.128f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.14f, 0.145f, 0.16f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.21f, 0.23f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.16f, 0.34f, 0.58f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(0.23f, 0.24f, 0.27f, 1.0f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.36f, 0.59f, 0.88f, 1.0f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.30f, 0.62f, 1.0f, 1.0f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.30f, 0.62f, 1.0f, 0.18f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.30f, 0.62f, 1.0f, 0.55f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.30f, 0.62f, 1.0f, 0.85f);
    colors[ImGuiCol_Tab] = ImVec4(0.09f, 0.094f, 0.103f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.18f, 0.28f, 0.41f, 1.0f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.14f, 0.24f, 0.36f, 1.0f);
    colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.30f, 0.62f, 1.0f, 1.0f);
    colors[ImGuiCol_DockingPreview] = ImVec4(0.30f, 0.62f, 1.0f, 0.65f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.055f, 0.057f, 0.063f, 1.0f);

    if (!ImGui_ImplGlfw_InitForOpenGL(window_, true)) {
        throw std::runtime_error("Failed to initialize the ImGui GLFW backend");
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
        throw std::runtime_error("Failed to initialize the ImGui OpenGL backend");
    }
    guiInitialized_ = true;
}

void Application::initializeRenderer() {
    renderer_ = std::make_unique<Renderer>(
        sourceRoot_ / "shaders" / "basic.vert",
        sourceRoot_ / "shaders" / "basic.frag",
        sourceRoot_ / "shaders" / "debug_lines.vert",
        sourceRoot_ / "shaders" / "debug_lines.frag"
    );
    std::vector<TextureUploadWarning> warnings;
    groundModel_ = std::make_unique<GpuModel>(
        makeGroundPlaneData(),
        renderer_->textureCache(),
        warnings
    );
    glassBackdropModel_ = std::make_unique<GpuModel>(
        makeGlassCheckerboardData(),
        renderer_->textureCache(),
        warnings
    );
}

void Application::initializeImporters() {
    importers_.push_back(std::make_unique<ObjLoader>());
    importers_.push_back(std::make_unique<AssimpImporter>());
}

void Application::shutdown() {
    if (shutdownComplete_) {
        return;
    }
    shutdownComplete_ = true;

    if (pendingModelImport_.has_value()) {
        pendingModelImport_->future.wait();
        try {
            pendingModelImport_->future.get();
        } catch (...) {
        }
        pendingModelImport_.reset();
    }

    if (window_ != nullptr) {
        glfwMakeContextCurrent(window_);
        scene_.clear();
        importedModels_.clear();
        pickingShader_.reset();
        model_.reset();
        groundModel_.reset();
        glassBackdropModel_.reset();
        renderer_.reset();
    }
    if (guiInitialized_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        guiInitialized_ = false;
    }
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

void Application::drawMainMenu() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_N, false)) newEmptyScene();
        if (ImGui::IsKeyPressed(ImGuiKey_O, false)) openSceneFromDialog();
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            if (ImGui::GetIO().KeyShift) saveSceneAs(); else saveCurrentScene();
        }
    }
    if (ImGui::BeginMenu(EditorUi::label("File"))) {
        if (ImGui::MenuItem(EditorUi::label("New empty scene"), "Ctrl+N")) newEmptyScene();
        ImGui::Separator();
        if (ImGui::MenuItem(EditorUi::label("Open scene..."), "Ctrl+O", false, !pendingModelImport_.has_value())) {
            openSceneFromDialog();
        }
        const std::filesystem::path recent = recentScenePath();
        if (ImGui::MenuItem(
                EditorUi::label("Reopen last scene"),
                nullptr,
                false,
                !recent.empty() && std::filesystem::exists(recent) && !pendingModelImport_.has_value()
            )) {
            openScene(recent);
        }
        if (ImGui::BeginMenu(EditorUi::label("Open bundled scene"), !availableScenes_.empty())) {
            for (const auto& path : availableScenes_) {
                if (ImGui::MenuItem(path.stem().string().c_str())) openScene(path);
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(EditorUi::label("Save scene"), "Ctrl+S", false, !pendingModelImport_.has_value())) {
            saveCurrentScene();
        }
        if (ImGui::MenuItem(EditorUi::label("Save scene as..."), "Ctrl+Shift+S", false, !pendingModelImport_.has_value())) {
            saveSceneAs();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(EditorUi::label("Open model..."), nullptr, false, !pendingModelImport_.has_value())) {
            std::string dialogError;
            const auto selected = openModelFileDialog(dialogError);
            if (selected.has_value()) {
                loadModel(*selected, true);
            } else if (!dialogError.empty()) {
                statusMessage_ = "Open failed: " + dialogError;
            }
        }
        if (ImGui::BeginMenu(EditorUi::label("Open bundled model"))) {
            for (const auto& path : availableModels_) {
                if (ImGui::MenuItem(path.filename().string().c_str())) {
                    loadModel(path, true);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(
            "Reset scene to current model",
            nullptr,
            false,
            currentScenePath_.empty() && !currentModelPath_.empty() && !pendingModelImport_.has_value()
        )) {
            loadModel(currentModelPath_);
        }
        if (ImGui::MenuItem(EditorUi::label("Save viewport PNG"), nullptr, false, !scene_.entities().empty())) {
            pendingScreenshotPath_ = nextScreenshotPath();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(EditorUi::label("Exit"), "Esc")) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(EditorUi::label("View"))) {
        if (ImGui::MenuItem(EditorUi::label("Reset camera"), "F")) {
            camera_.reset();
        }
        if (ImGui::MenuItem("Prism spectrum preset")) {
            activatePrismDemoPreset(true);
        }
        if (ImGui::MenuItem("Volume glass preset")) {
            glassCausticsDemoEnabled_ = false;
            volumeGlassPreset_ = VolumeGlassPreset::Olive;
            loadModel(sourceRoot_ / "assets" / "models" / "glass_volume_sphere.gltf");
        }
        if (ImGui::MenuItem("Glass caustics preset")) {
            activateGlassCausticsPreset();
        }
        if (ImGui::MenuItem("Local light stress preset")) {
            activateLightStressPreset(true);
        }
        if (ImGui::MenuItem("Instance / culling / LOD stress preset")) {
            activateInstanceStressPreset(true);
        }
        ImGui::MenuItem(EditorUi::label("Wireframe"), nullptr, &rendererSettings_.wireframe);
        ImGui::MenuItem(EditorUi::label("Back-face culling"), nullptr, &rendererSettings_.cullBackFaces);
        ImGui::Separator();
        ImGui::MenuItem(EditorUi::label("Ground grid"), nullptr, &rendererSettings_.showGrid);
        ImGui::MenuItem(EditorUi::label("Ground plane"), nullptr, &showGroundPlane_);
        ImGui::MenuItem(EditorUi::label("Comparison object"), nullptr, &showComparisonObject_);
        ImGui::MenuItem(EditorUi::label("XYZ axes"), nullptr, &rendererSettings_.showAxes);
        ImGui::MenuItem(EditorUi::label("Auto rotate"), nullptr, &autoRotate_);
        ImGui::Separator();
        if (ImGui::BeginMenu(EditorUi::label("Panels"))) {
            ImGui::MenuItem(EditorUi::label("Hierarchy###Hierarchy"), nullptr, &hierarchyPanelOpen_);
            ImGui::MenuItem(EditorUi::label("Inspector###Inspector"), nullptr, &inspectorPanelOpen_);
            ImGui::MenuItem(EditorUi::label("Content Browser###Assets"), nullptr, &assetsPanelOpen_);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(EditorUi::label("Help"))) {
        ImGui::MenuItem("Dear ImGui demo", nullptr, &showImGuiDemo_);
        if (ImGui::MenuItem(EditorUi::label("About MyRenderer"))) {
            showAbout_ = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Language / 语言")) {
        if (ImGui::MenuItem("English", nullptr, !EditorUi::chinese)) EditorUi::setLanguage(false);
        if (ImGui::MenuItem("简体中文", nullptr, EditorUi::chinese, EditorUi::chineseFontAvailable)) EditorUi::setLanguage(true);
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem(EditorUi::label(EditorUi::label("Reset layout")))) resetEditorLayout_ = true;
    const std::string fps = std::to_string(static_cast<int>(ImGui::GetIO().Framerate)) + " FPS";
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - ImGui::CalcTextSize(fps.c_str()).x - 16.0f));
    ImGui::TextDisabled("%s", fps.c_str());
    ImGui::EndMainMenuBar();
}


void Application::drawInspectorPanel() {
    ImGui::SetNextWindowSizeConstraints(
        EditorUi::minimumDockedPanelSize,
        ImVec2(FLT_MAX, FLT_MAX)
    );
    if (!ImGui::Begin(EditorUi::label("Inspector###Inspector"))) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("InspectorTabs")) {
        const bool showObject = focusObjectTab_;
        focusObjectTab_ = false;
        if (ImGui::BeginTabItem(EditorUi::label("Object"), nullptr, showObject ? ImGuiTabItemFlags_SetSelected : 0)) {
            SceneEntity* selectedEntity = scene_.find(selectedSceneEntity_);
            if (EditorUi::section("Transform", true)) {
                if (!selectedEntity) {
                    ImGui::TextWrapped("%s", EditorUi::chinese ? "左键点击场景中的模型或在层级中选择对象。" : "Left-click a model in the viewport or select an object in the hierarchy.");
                } else {
                    ImGui::TextUnformatted(selectedEntity->name.c_str());
                    bool edited = EditorUi::DragFloat3(EditorUi::label("Position"), &selectedEntity->transform.translation.x, 0.01f);
                    edited |= EditorUi::DragFloat3(EditorUi::label("Rotation"), &selectedEntity->transform.rotationDegrees.x, 0.25f);
                    edited |= EditorUi::DragFloat3(EditorUi::label("Scale"), &selectedEntity->transform.scale.x, 0.01f, 0.01f, 100.0f);
                    edited |= EditorUi::ColorEdit3(EditorUi::label("Entity tint"), &selectedEntity->tint.x);
                    if (ImGui::Button(EditorUi::label("Reset transform"), ImVec2(-1.0f, 0.0f))) {
                        const auto asset = selectedEntity->transform.assetTransform;
                        selectedEntity->transform = SceneTransform{};
                        selectedEntity->transform.assetTransform = asset;
                        edited = true;
                    }
                    if (edited) {
                        editedEntities_.insert(selectedEntity->id);
                        selectedEntity->motionHistoryValid = false;
                    }
                }
            }
            if (selectedEntity && EditorUi::section("Asset statistics")) {
                const GpuModel* asset = selectedEntity->model;
                if (asset) {
                    ImGui::Text("Meshes: %zu | Triangles: %zu", asset->meshCount(), asset->triangleCount());
                    ImGui::Text("Materials: %zu | Textures: %zu", asset->materialCount(), asset->textureCount());
                }
                if (ImGui::Button(EditorUi::label("Delete"), ImVec2(-1.0f, 0.0f))) deleteSelectedEntity();
            }
            ImGui::EndTabItem();
        }

        const bool showRenderer = focusRendererTab_;
        focusRendererTab_ = false;
        if (ImGui::BeginTabItem(
                EditorUi::label("Renderer"),
                nullptr,
                showRenderer ? ImGuiTabItemFlags_SetSelected : 0
            )) {
            if (EditorUi::section("Stage", true)) {
                EditorUi::Checkbox(EditorUi::label("Ground receiver"), &showGroundPlane_);
                EditorUi::ColorEdit3(EditorUi::label("Ground color"), &groundColor_.x);
                EditorUi::DragFloat(EditorUi::label("Ground offset"), &groundOffset_, 0.01f, -3.0f, 0.0f, "%.2f");
                EditorUi::Checkbox(EditorUi::label("Comparison object"), &showComparisonObject_);
            }

            if (EditorUi::section("Material", true)) {
                EditorUi::ColorEdit3(EditorUi::label("Base color tint"), &rendererSettings_.baseColor.x);
                EditorUi::SliderFloat("Ambient", &rendererSettings_.ambientStrength, 0.0f, 1.0f);
                EditorUi::SliderFloat("Diffuse", &rendererSettings_.diffuseStrength, 0.0f, 2.0f);
                EditorUi::SliderFloat("Specular", &rendererSettings_.specularStrength, 0.0f, 2.0f);
                EditorUi::SliderFloat("Shininess", &rendererSettings_.shininess, 1.0f, 256.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
            }

            if (EditorUi::section("Shader development")) {
                EditorUi::Checkbox(EditorUi::label("Shader hot reload"), &rendererSettings_.shaderHotReloadEnabled);
                if (renderer_ != nullptr) {
                    if (renderer_->shaderReloadFailed()) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.36f, 0.30f, 1.0f));
                        ImGui::TextWrapped("%s", renderer_->shaderReloadStatus().c_str());
                        ImGui::PopStyleColor();
                    } else {
                        ImGui::TextDisabled("%s", renderer_->shaderReloadStatus().c_str());
                    }
                }
            }
            if (EditorUi::section("GPU skinning & animation")) {
                const bool hasSkinning = model_ != nullptr && model_->hasSkinning();
                ImGui::BeginDisabled(!hasSkinning);
                EditorUi::Checkbox(EditorUi::label("Enable animation"), &animationEnabled_);
                EditorUi::Checkbox(EditorUi::label("Play"), &animationPlaying_);
                if (hasSkinning && model_->animationCount() > 0U) {
                animationClipIndex_ = std::min(
                    animationClipIndex_,
                    model_->animationCount() - 1U
                );
                if (ImGui::BeginCombo(
                    "Animation clip",
                    model_->animationName(animationClipIndex_).c_str()
                )) {
                    for (std::size_t clip = 0; clip < model_->animationCount(); ++clip) {
                        const bool selected = clip == animationClipIndex_;
                        if (ImGui::Selectable(model_->animationName(clip).c_str(), selected)) {
                            animationClipIndex_ = clip;
                            animationTimeSeconds_ = 0.0f;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                const float duration = model_->animationDuration(animationClipIndex_);
                EditorUi::SliderFloat(
                    "Animation time",
                    &animationTimeSeconds_,
                    0.0f,
                    std::max(duration, 0.01f),
                    "%.3f s"
                );
                EditorUi::SliderFloat("Playback speed", &animationSpeed_, 0.0f, 3.0f, "%.2fx");
                }
                const char* skinDebugViews[] = {"Final", "Joint influence", "Dominant weight"};
                EditorUi::Combo(
                "Skinning debug",
                &rendererSettings_.skinningDebugView,
                skinDebugViews,
                3
                );
                if (hasSkinning) {
                    ImGui::TextDisabled(
                    "%zu palette joints | %zu animation clip(s)",
                    model_->jointCount(),
                    model_->animationCount()
                    );
                } else {
                    ImGui::TextDisabled("Current asset has no skin palette");
                }
                ImGui::EndDisabled();
            }
            if (EditorUi::section("PBR & environment", true)) {
                int shadingMode = static_cast<int>(rendererSettings_.shadingMode);
                const char* shadingModes[] = {"Physically based", "Stylized / toon"};
                if (EditorUi::Combo("Shading mode", &shadingMode, shadingModes, 2)) {
                    rendererSettings_.shadingMode = static_cast<ShadingMode>(shadingMode);
                }
                if (rendererSettings_.shadingMode == ShadingMode::Stylized) {
                    EditorUi::SliderInt(
                        "Lighting bands", &rendererSettings_.stylizedBandCount, 2, 8
                    );
                    EditorUi::SliderFloat(
                        "Band softness", &rendererSettings_.stylizedBandSoftness,
                        0.0f, 0.25f, "%.3f"
                    );
                    EditorUi::SliderFloat(
                        "Specular size", &rendererSettings_.stylizedSpecularSize,
                        0.02f, 0.8f, "%.2f"
                    );
                    EditorUi::SliderFloat(
                        "Specular softness", &rendererSettings_.stylizedSpecularSoftness,
                        0.0f, 0.2f, "%.3f"
                    );
                    EditorUi::SliderFloat(
                        "Rim width", &rendererSettings_.stylizedRimWidth,
                        0.02f, 0.9f, "%.2f"
                    );
                    EditorUi::SliderFloat(
                        "Rim softness", &rendererSettings_.stylizedRimSoftness,
                        0.0f, 0.3f, "%.3f"
                    );
                    EditorUi::SliderFloat(
                        "Rim intensity", &rendererSettings_.stylizedRimIntensity,
                        0.0f, 3.0f, "%.2f"
                    );
                    EditorUi::ColorEdit3(
                        "Shadow tint", &rendererSettings_.stylizedShadowTint.x
                    );
                    EditorUi::ColorEdit3(
                        "Rim color", &rendererSettings_.stylizedRimColor.x
                    );
                    EditorUi::Checkbox(
                        "Screen-space outline",
                        &rendererSettings_.stylizedOutlineEnabled
                    );
                    if (rendererSettings_.stylizedOutlineEnabled) {
                        EditorUi::SliderFloat(
                            "Outline width", &rendererSettings_.stylizedOutlineWidth,
                            0.5f, 6.0f, "%.1f px"
                        );
                        EditorUi::SliderFloat(
                            "Outline depth threshold",
                            &rendererSettings_.stylizedOutlineDepthThreshold,
                            0.001f, 0.12f, "%.3f"
                        );
                        EditorUi::SliderFloat(
                            "Outline normal threshold",
                            &rendererSettings_.stylizedOutlineNormalThreshold,
                            0.02f, 0.8f, "%.2f"
                        );
                        EditorUi::ColorEdit3(
                            "Outline color", &rendererSettings_.stylizedOutlineColor.x
                        );
                    }
                }
                int renderPath = static_cast<int>(rendererSettings_.renderPath);
                const char* renderPaths[] = {"Forward", "Deferred (hybrid)"};
                if (EditorUi::Combo("Opaque render path", &renderPath, renderPaths, 2)) {
                    rendererSettings_.renderPath = static_cast<RenderPath>(renderPath);
                }
                ImGui::BeginDisabled(rendererSettings_.renderPath != RenderPath::Deferred);
                int gBufferDebug = static_cast<int>(rendererSettings_.gBufferDebugView);
                const char* gBufferDebugViews[] = {
                "Final lighting",
                "Albedo",
                "Encoded normal",
                "Metallic / Roughness",
                "Depth",
                "SSAO"
                };
                if (EditorUi::Combo("G-buffer debug", &gBufferDebug, gBufferDebugViews, 6)) {
                    rendererSettings_.gBufferDebugView = static_cast<GBufferDebugView>(gBufferDebug);
                }
                ImGui::EndDisabled();
            }
            if (EditorUi::section("Local light stress")) {
                bool stressEnabled = lightStressDemoEnabled_;
                if (EditorUi::Checkbox("Enable stress scene", &stressEnabled)) {
                lightStressDemoEnabled_ = stressEnabled;
                if (lightStressDemoEnabled_) {
                    activateLightStressPreset(false);
                } else {
                    rendererSettings_.localLights.clear();
                    statusMessage_ = "Local light stress scene disabled";
                }
                }
                ImGui::BeginDisabled(!lightStressDemoEnabled_);
                const char* lightTiers[] = {"Low (8)", "Medium (32)", "High (64)"};
                if (EditorUi::Combo("Local light tier", &localLightTierIndex_, lightTiers, 3)) {
                    rebuildLocalLights();
                }
                const std::size_t spotCount = std::count_if(
                rendererSettings_.localLights.begin(),
                rendererSettings_.localLights.end(),
                [](const LocalLight& light) { return light.type == LocalLightType::Spot; }
                );
                ImGui::TextDisabled(
                "%zu point + %zu spot | 100 objects",
                rendererSettings_.localLights.size() - spotCount,
                spotCount
                );
                ImGui::EndDisabled();
            }
            if (EditorUi::section("Instance submission stress")) {
                bool instanceStressEnabled = instanceStressDemoEnabled_;
                if (EditorUi::Checkbox("Enable 2,500-instance scene", &instanceStressEnabled)) {
                instanceStressDemoEnabled_ = instanceStressEnabled;
                if (instanceStressDemoEnabled_) {
                    activateInstanceStressPreset(true);
                } else {
                    rendererSettings_.instanceOptimizationEnabled = false;
                    statusMessage_ = "Instance stress scene disabled";
                }
                }
                ImGui::BeginDisabled(!instanceStressDemoEnabled_);
                EditorUi::Checkbox(
                "GPU instancing / batching",
                &rendererSettings_.instanceOptimizationEnabled
                );
                ImGui::BeginDisabled(!rendererSettings_.instanceOptimizationEnabled);
                EditorUi::Checkbox("CPU frustum culling", &rendererSettings_.frustumCullingEnabled);
                EditorUi::Checkbox("Projected-size LOD", &rendererSettings_.lodSelectionEnabled);
                ImGui::EndDisabled();
                const auto& lodCounts = renderer_->lodInstanceCounts();
                ImGui::TextDisabled(
                "Submitted %zu | visible %zu | culled %zu",
                renderer_->submittedInstanceCount(),
                renderer_->visibleInstanceCount(),
                renderer_->culledInstanceCount()
                );
                ImGui::TextDisabled(
                "LOD0 / 1 / 2: %zu / %zu / %zu | prep %.3f ms",
                lodCounts[0], lodCounts[1], lodCounts[2],
                renderer_->instancePreparationMilliseconds()
                );
                ImGui::TextDisabled(
                "Submitted triangles: %zu",
                renderer_->renderedInstanceTriangleCount()
                );
                ImGui::EndDisabled();
            }
            if (EditorUi::section("Lighting & environment", true)) {
            EditorUi::Checkbox("Metallic-roughness PBR", &rendererSettings_.pbrEnabled);
            EditorUi::Checkbox("Image-based lighting", &rendererSettings_.iblEnabled);
            EditorUi::Checkbox(EditorUi::label("Skybox"), &rendererSettings_.skyboxEnabled);
            EditorUi::Checkbox(EditorUi::label("Shadow mapping"), &rendererSettings_.shadowsEnabled);
            EditorUi::Checkbox(
                "Colored transmission shadows",
                &rendererSettings_.coloredTransmissionShadowsEnabled
            );
            EditorUi::Checkbox("HDR caustics", &rendererSettings_.causticsEnabled);
            if (rendererSettings_.causticsEnabled) {
                int causticsMode = static_cast<int>(rendererSettings_.causticsMode);
                const char* causticsModes[] = {"Projector / decal", "Light-space RGB"};
                if (EditorUi::Combo("Caustics mode", &causticsMode, causticsModes, 2)) {
                    rendererSettings_.causticsMode = static_cast<CausticsMode>(causticsMode);
                }
                EditorUi::SliderFloat(
                    "Caustics strength",
                    &rendererSettings_.causticsStrength,
                    0.0f,
                    8.0f,
                    "%.2f"
                );
                EditorUi::SliderFloat(
                    "Caustics scale",
                    &rendererSettings_.causticsScale,
                    0.1f,
                    3.0f,
                    "%.2f"
                );
                EditorUi::SliderFloat3(
                    "Caustics direction",
                    &rendererSettings_.causticsDirection.x,
                    -1.5f,
                    1.5f,
                    "%.2f"
                );
                EditorUi::SliderFloat(
                    "Caustics sharpness",
                    &rendererSettings_.causticsSharpness,
                    0.0f,
                    1.0f,
                    "%.2f"
                );
                ImGui::BeginDisabled(
                    rendererSettings_.causticsMode != CausticsMode::Projector
                );
                EditorUi::Checkbox("Animate caustics", &rendererSettings_.causticsAnimated);
                ImGui::EndDisabled();
                ImGui::TextDisabled(
                    "Caustics map: 1024 x 1024 | GPU %.3f ms",
                    renderer_->hasCausticsGpuTime()
                        ? renderer_->causticsGpuTimeMilliseconds()
                        : 0.0
                );
            }
            }
            if (EditorUi::section("Glass feature toggles")) {
            EditorUi::Checkbox("Glass transmission", &rendererSettings_.transmissionEnabled);
            EditorUi::Checkbox("Dispersion", &rendererSettings_.dispersionEnabled);
            EditorUi::Checkbox("Geometric glass thickness", &rendererSettings_.geometricThicknessEnabled);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Uses a back-face depth pass for closed glass meshes; "
                    "falls back to the material thickness/texture when no exit surface is found."
                );
            }
            EditorUi::Checkbox(
                "Two-interface refraction",
                &rendererSettings_.twoInterfaceRefractionEnabled
            );
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Traces the curved exit surface and applies Snell refraction again "
                    "when light leaves the glass."
                );
            }
            EditorUi::SliderFloat(
                "Refraction scale",
                &rendererSettings_.refractionScale,
                0.0f,
                0.8f,
                "%.3f"
            );
            EditorUi::SliderInt("Refraction steps", &rendererSettings_.refractionSteps, 4, 32);
            EditorUi::SliderFloat(
                "Volume thickness scale",
                &rendererSettings_.volumeThicknessScale,
                0.0f,
                4.0f,
                "%.2f"
            );
            EditorUi::Checkbox(
                "Volume glass material override",
                &rendererSettings_.volumeGlassOverrideEnabled
            );
            if (rendererSettings_.volumeGlassOverrideEnabled) {
                int presetIndex = static_cast<int>(volumeGlassPreset_);
                const char* presetNames[] = {"Clear", "Olive", "Amber", "Crystal"};
                if (EditorUi::Combo("Glass preset", &presetIndex, presetNames, 4)) {
                    applyVolumeGlassPreset(static_cast<VolumeGlassPreset>(presetIndex));
                }
                EditorUi::SliderFloat(
                    "Glass transmission",
                    &rendererSettings_.volumeGlassTransmission,
                    0.0f,
                    1.0f,
                    "%.2f"
                );
                EditorUi::SliderFloat(
                    "Glass roughness",
                    &rendererSettings_.volumeGlassRoughness,
                    0.04f,
                    1.0f,
                    "%.2f"
                );
                EditorUi::ColorEdit3(
                    "Attenuation color",
                    &rendererSettings_.volumeGlassAttenuationColor.x
                );
                EditorUi::SliderFloat(
                    "Attenuation distance",
                    &rendererSettings_.volumeGlassAttenuationDistance,
                    0.05f,
                    8.0f,
                    "%.2f",
                    ImGuiSliderFlags_Logarithmic
                );
                if (ImGui::Button("Clear")) {
                    applyVolumeGlassPreset(VolumeGlassPreset::Clear);
                }
                ImGui::SameLine();
                if (ImGui::Button("Olive")) {
                    applyVolumeGlassPreset(VolumeGlassPreset::Olive);
                }
                ImGui::SameLine();
                if (ImGui::Button("Amber")) {
                    applyVolumeGlassPreset(VolumeGlassPreset::Amber);
                }
            }
            if (!prismDemoEnabled_) {
                ImGui::BeginDisabled(!rendererSettings_.dispersionEnabled);
                EditorUi::SliderFloat(
                    "Dispersion override",
                    &rendererSettings_.dispersionStrength,
                    0.0f,
                    2.5f,
                    "%.2f"
                );
                if (rendererSettings_.dispersionStrength > 0.0f) {
                    ImGui::TextDisabled(
                        "Abbe number: %.2f",
                        20.0f / rendererSettings_.dispersionStrength
                    );
                } else {
                    ImGui::TextDisabled("Abbe number: material-driven");
                }
                ImGui::EndDisabled();
            }
            }
            if (prismDemoEnabled_ && EditorUi::section("Prism spectrum")) {
                int presetIndex = static_cast<int>(prismOpticalPreset_);
                const char* presetNames[] = {
                    prismOpticalPresetName(PrismOpticalPreset::CrownGlass),
                    prismOpticalPresetName(PrismOpticalPreset::WaterLike),
                    prismOpticalPresetName(PrismOpticalPreset::DiamondLike),
                    prismOpticalPresetName(PrismOpticalPreset::ExaggeratedCover)
                };
                if (EditorUi::Combo("Optical preset", &presetIndex, presetNames, 4)) {
                    applyPrismOpticalPreset(static_cast<PrismOpticalPreset>(presetIndex));
                }

                bool opticsChanged = false;
                opticsChanged |= EditorUi::SliderFloat(
                    "Beam direction",
                    &prismParameters_.beamAngleDegrees,
                    -20.0f,
                    20.0f,
                    "%.2f deg"
                );
                opticsChanged |= EditorUi::SliderFloat(
                    "Central IOR",
                    &prismParameters_.centralIndexOfRefraction,
                    1.0f,
                    2.6f,
                    "%.3f"
                );
                opticsChanged |= EditorUi::SliderFloat(
                    "Dispersion",
                    &prismParameters_.dispersion,
                    0.0f,
                    2.5f,
                    "%.3f"
                );
                if (prismParameters_.dispersion > 0.0f) {
                    ImGui::TextDisabled(
                        "Abbe number: %.2f",
                        20.0f / prismParameters_.dispersion
                    );
                } else {
                    ImGui::TextDisabled("Abbe number: infinite (no dispersion)");
                }
                static constexpr std::array<int, 4> sampleTiers{7, 15, 21, 31};
                int sampleTierIndex = 0;
                for (std::size_t index = 0; index < sampleTiers.size(); ++index) {
                    if (prismParameters_.spectralSampleCount == sampleTiers[index]) {
                        sampleTierIndex = static_cast<int>(index);
                    }
                }
                const char* sampleLabels[] = {"7", "15", "21", "31"};
                if (EditorUi::Combo("Spectral samples", &sampleTierIndex, sampleLabels, 4)) {
                    prismParameters_.spectralSampleCount = sampleTiers[static_cast<std::size_t>(sampleTierIndex)];
                    opticsChanged = true;
                }
                int spectrumMode = static_cast<int>(prismParameters_.spectrumMode);
                const char* spectrumModes[] = {"Continuous", "Seven-band"};
                if (EditorUi::Combo("Spectrum mode", &spectrumMode, spectrumModes, 2)) {
                    prismParameters_.spectrumMode = static_cast<PrismSpectrumMode>(spectrumMode);
                    opticsChanged = true;
                }
                opticsChanged |= EditorUi::SliderFloat(
                    "White point",
                    &prismParameters_.whitePointKelvin,
                    2000.0f,
                    12000.0f,
                    "%.0f K"
                );
                if (opticsChanged) {
                    updatePrismDemoOptics();
                }

                EditorUi::Checkbox(
                    "Spectral beam ribbons",
                    &rendererSettings_.showPrismIncidentBeam
                );
                EditorUi::SliderFloat(
                    "Beam width",
                    &rendererSettings_.prismBeamWidth,
                    0.005f,
                    0.16f,
                    "%.3f"
                );
                EditorUi::SliderFloat(
                    "Beam intensity",
                    &rendererSettings_.prismBeamIntensity,
                    0.0f,
                    16.0f,
                    "%.2f"
                );
                EditorUi::SliderFloat(
                    "Edge softness",
                    &rendererSettings_.prismBeamEdgeSoftness,
                    0.01f,
                    1.0f,
                    "%.2f"
                );
                EditorUi::SliderFloat(
                    "Bloom contribution",
                    &rendererSettings_.prismBeamBloomContribution,
                    0.0f,
                    2.0f,
                    "%.2f"
                );
                EditorUi::Checkbox(
                    "Optical path debug",
                    &rendererSettings_.showPrismOpticalPathDebug
                );

                float minimumEnergy = 1.0f;
                float maximumEnergy = 0.0f;
                int validPathCount = 0;
                int tirPathCount = 0;
                for (const PrismSpectralSample& sample : rendererSettings_.prismSpectrum.samples) {
                    if (!sample.path.valid) {
                        continue;
                    }
                    ++validPathCount;
                    tirPathCount += sample.path.totalInternalReflection ? 1 : 0;
                    minimumEnergy = std::min(minimumEnergy, sample.transmittance);
                    maximumEnergy = std::max(maximumEnergy, sample.transmittance);
                }
                ImGui::TextDisabled(
                    "Paths: %d valid / %d TIR | energy %.3f..%.3f",
                    validPathCount,
                    tirPathCount,
                    validPathCount > 0 ? minimumEnergy : 0.0f,
                    maximumEnergy
                );
                if (ImGui::TreeNode(EditorUi::label("Optical path details"))) {
                    if (ImGui::BeginTable(
                            "PrismOpticalPathTable",
                            5,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                        )) {
                        ImGui::TableSetupColumn("nm");
                        ImGui::TableSetupColumn("IOR");
                        ImGui::TableSetupColumn("Entry T");
                        ImGui::TableSetupColumn("Exit T");
                        ImGui::TableSetupColumn("Total / state");
                        ImGui::TableHeadersRow();
                        for (const PrismSpectralSample& sample : rendererSettings_.prismSpectrum.samples) {
                            if (!sample.path.valid) continue;
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%.0f", sample.wavelengthNanometers);
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%.4f", sample.indexOfRefraction);
                            ImGui::TableSetColumnIndex(2);
                            ImGui::Text("%.3f", sample.path.entryTransmittance);
                            ImGui::TableSetColumnIndex(3);
                            ImGui::Text("%.3f", sample.path.exitTransmittance);
                            ImGui::TableSetColumnIndex(4);
                            ImGui::Text(
                                "%.3f%s",
                                sample.path.totalTransmittance,
                                sample.path.totalInternalReflection ? " / TIR" : ""
                            );
                        }
                        ImGui::EndTable();
                    }
                    ImGui::TreePop();
                }
                EditorUi::Checkbox("Lock hero camera", &prismCameraLocked_);
                EditorUi::Checkbox("Auto rotate prism", &autoRotate_);
                if (ImGui::Button("Restore prism hero shot", ImVec2(-1.0f, 0.0f))) {
                    restorePrismHeroShot();
                }
            }
            if (EditorUi::section("Render diagnostics")) {
                int glassDebugView = static_cast<int>(rendererSettings_.glassDebugView);
                const char* glassDebugViews[] = {
                "Final",
                "Reflection",
                "Refraction",
                "IOR",
                "Refracted UV",
                "Thickness",
                "Transmittance",
                "RGB dispersion",
                "Front/back thickness data",
                "Exit surface normal",
                "Object ID",
                "Caustics map",
                "Transmission shadow"
                };
                if (EditorUi::Combo("Glass debug view", &glassDebugView, glassDebugViews, 13)) {
                    rendererSettings_.glassDebugView = static_cast<GlassDebugView>(glassDebugView);
                }
                EditorUi::SliderFloat("Environment", &rendererSettings_.environmentIntensity, 0.0f, 2.0f, "%.2f");
                ImGui::TextDisabled("Shadow map: %d x %d", renderer_->shadowResolution(), renderer_->shadowResolution());
            }

            if (EditorUi::section("Post processing", true)) {
                ImGui::BeginDisabled(rendererSettings_.renderPath != RenderPath::Deferred);
                EditorUi::Checkbox("SSAO", &rendererSettings_.ssaoEnabled);
                ImGui::BeginDisabled(!rendererSettings_.ssaoEnabled);
                EditorUi::SliderFloat("SSAO radius", &rendererSettings_.ssaoRadius, 0.05f, 2.0f, "%.2f");
                EditorUi::SliderFloat("SSAO bias", &rendererSettings_.ssaoBias, 0.0f, 0.15f, "%.3f");
                EditorUi::SliderFloat("SSAO strength", &rendererSettings_.ssaoStrength, 0.1f, 3.0f, "%.2f");
                ImGui::EndDisabled();
                ImGui::EndDisabled();
                EditorUi::Checkbox("Temporal AA", &rendererSettings_.temporalAaEnabled);
                ImGui::BeginDisabled(!rendererSettings_.temporalAaEnabled);
                EditorUi::SliderFloat(
                "TAA history weight",
                &rendererSettings_.temporalHistoryWeight,
                0.0f,
                0.98f,
                "%.2f"
                );
                const char* temporalDebugViews[] = {"Final", "Motion vectors", "History weight"};
                EditorUi::Combo(
                "TAA debug",
                &rendererSettings_.temporalDebugView,
                temporalDebugViews,
                3
                );
                ImGui::EndDisabled();
                EditorUi::Checkbox("ACES tone mapping", &rendererSettings_.toneMapping);
                EditorUi::Checkbox("Bloom", &rendererSettings_.bloom);
                EditorUi::SliderFloat("Exposure", &rendererSettings_.exposure, 0.1f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
                ImGui::BeginDisabled(!rendererSettings_.bloom);
                EditorUi::SliderFloat("Bloom threshold", &rendererSettings_.bloomThreshold, 0.1f, 4.0f, "%.2f");
                EditorUi::SliderFloat("Bloom intensity", &rendererSettings_.bloomIntensity, 0.0f, 1.0f, "%.2f");
                ImGui::EndDisabled();
            }

            if (EditorUi::section("Rasterization", true)) {
            EditorUi::Checkbox(EditorUi::label("Wireframe"), &rendererSettings_.wireframe);
            EditorUi::Checkbox(EditorUi::label("Back-face culling"), &rendererSettings_.cullBackFaces);
            EditorUi::Checkbox(EditorUi::label("Normal mapping"), &rendererSettings_.normalMapping);
            EditorUi::Checkbox(EditorUi::label("Ground grid"), &rendererSettings_.showGrid);
            EditorUi::Checkbox("XYZ axes + gizmo", &rendererSettings_.showAxes);
            EditorUi::ColorEdit3(EditorUi::label("Background"), &rendererSettings_.backgroundColor.x);
            const char* msaaOptions[] = {"1x", "4x"};
            int msaaSelection = rendererSettings_.msaaSamples > 1 ? 1 : 0;
            if (EditorUi::Combo("MSAA", &msaaSelection, msaaOptions, 2)) {
                rendererSettings_.msaaSamples = msaaSelection == 0 ? 1 : 4;
            }
            ImGui::TextDisabled("Active samples: %dx", renderer_->activeMsaaSamples());
            }

            if (EditorUi::section("Directional light")) {
                EditorUi::DragFloat3(EditorUi::label("Direction"), &rendererSettings_.lightDirection.x, 0.01f, -1.0f, 1.0f, "%.2f");
            }

            if (EditorUi::section("Camera")) {
                float fieldOfView = camera_.fieldOfView();
                if (EditorUi::SliderFloat(EditorUi::label("Field of view"), &fieldOfView, 15.0f, 90.0f, "%.0f deg")) {
                    camera_.setFieldOfView(fieldOfView);
                }
                if (ImGui::Button(EditorUi::label("Frame model"), ImVec2(-1.0f, 0.0f))) {
                    camera_.reset(modelPosition_);
                }
            }

            if (EditorUi::section("Runtime")) {
            if (EditorUi::Checkbox("VSync", &vsync_)) {
                glfwSwapInterval(vsync_ ? 1 : 0);
            }
            ImGui::Text("CPU frame: %.2f ms", cpuFrameTimeMilliseconds_);
            if (renderer_->hasGpuFrameTime()) {
                ImGui::Text("GPU viewport: %.2f ms", renderer_->gpuFrameTimeMilliseconds());
            } else {
                ImGui::TextDisabled("GPU viewport: collecting...");
            }
            if (renderer_->hasPrismBeamGpuTime()) {
                ImGui::Text("GPU beam pass: %.3f ms", renderer_->prismBeamGpuTimeMilliseconds());
            }
            ImGui::Text("Draw calls: %zu", renderer_->drawCallCount());
            ImGui::Text(
                "Estimated opaque traffic: %.1f MiB/frame",
                static_cast<double>(renderer_->estimatedOpaqueTrafficBytesPerFrame())
                    / (1024.0 * 1024.0)
            );
            ImGui::Text("Active passes: %zu", renderer_->activePassNames().size());
            for (std::size_t passIndex = 0; passIndex < renderer_->activePassNames().size(); ++passIndex) {
                const auto& passName = renderer_->activePassNames()[passIndex];
                const auto timing = std::find_if(
                    renderer_->gpuPassTimings().begin(),
                    renderer_->gpuPassTimings().end(),
                    [&](const GpuPassTiming& candidate) {
                        return candidate.name == passName;
                    }
                );
                if (timing != renderer_->gpuPassTimings().end()) {
                    ImGui::BulletText("%s: %.3f ms", passName.c_str(), timing->milliseconds);
                } else {
                    ImGui::BulletText("%s: collecting...", passName.c_str());
                }
                if (passIndex < renderer_->activePassContexts().size()
                    && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    const RenderPassContext& context = renderer_->activePassContexts()[passIndex];
                    ImGui::BeginTooltip();
                    ImGui::TextDisabled("Inputs");
                    for (const std::string& input : context.inputs) ImGui::BulletText("%s", input.c_str());
                    ImGui::TextDisabled("Outputs");
                    for (const std::string& output : context.outputs) ImGui::BulletText("%s", output.c_str());
                    ImGui::EndTooltip();
                }
            }
            ImGui::Text("Triangles: %zu", model_ ? loadedTriangleCount_ : 0U);
            ImGui::Text(
                "Texture memory: %.2f MiB",
                static_cast<double>(loadedTextureMemoryBytes_) / (1024.0 * 1024.0)
            );
            ImGui::Text(
                "Render memory: %.2f MiB",
                static_cast<double>(renderer_->estimatedRenderMemoryBytes()) / (1024.0 * 1024.0)
            );
            if (lastLoadTotalMilliseconds_ > 0.0) {
                ImGui::TextDisabled(
                    "Last load: %.1f ms CPU + %.1f ms GPU = %.1f ms",
                    lastCpuImportMilliseconds_,
                    lastGpuUploadMilliseconds_,
                    lastLoadTotalMilliseconds_
                );
            }
            ImGui::TextWrapped("%s", gpuDescription_.c_str());
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void Application::drawViewportPanel() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin(EditorUi::label("Viewport###Viewport"));
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return;
    }

    ImGui::SetCursorPos(ImVec2(8.0f, 30.0f));
    if (ImGui::SmallButton(EditorUi::label("Frame"))) {
        const SceneEntity* selected = scene_.find(selectedSceneEntity_);
        camera_.reset(selected ? glm::vec3(selected->worldTransform[3]) : modelPosition_);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(EditorUi::label("Save PNG"))) {
        pendingScreenshotPath_ = nextScreenshotPath();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(EditorUi::label("Panels"))) ImGui::OpenPopup("ViewportPanels");
    if (ImGui::BeginPopup("ViewportPanels")) {
        ImGui::MenuItem(EditorUi::label("Hierarchy###Hierarchy"), nullptr, &hierarchyPanelOpen_);
        ImGui::MenuItem(EditorUi::label("Inspector###Inspector"), nullptr, &inspectorPanelOpen_);
        ImGui::MenuItem(EditorUi::label("Content Browser###Assets"), nullptr, &assetsPanelOpen_);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    EditorUi::toolbarToggle("Grid", &rendererSettings_.showGrid);
    ImGui::SameLine();
    EditorUi::toolbarToggle("Ground", &showGroundPlane_);
    ImGui::SameLine();
    EditorUi::toolbarToggle("Axes", &rendererSettings_.showAxes);
    const char* viewportHelp = EditorUi::chinese
        ? "左键选择 | Delete 删除 | 右键旋转 | 中键平移"
        : "LMB select | Delete remove | RMB orbit | MMB pan";
    if (ImGui::GetContentRegionAvail().x > ImGui::CalcTextSize(viewportHelp).x + 16.0f) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", viewportHelp);
    } else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("%s", viewportHelp);
    }
    ImGui::SetCursorPosY(58.0f);

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const int width = renderWidthOverride_ > 0
        ? renderWidthOverride_
        : std::max(static_cast<int>(available.x), 1);
    const int height = renderHeightOverride_ > 0
        ? renderHeightOverride_
        : std::max(static_cast<int>(available.y), 1);

    const glm::mat4 normalization =
        glm::scale(glm::mat4(1.0f), glm::vec3(modelNormalizationScale_))
        * glm::translate(glm::mat4(1.0f), -modelCenter_);
    std::vector<RenderItem> renderItems;
    scene_.beginFrame();
    syncSceneEntities(normalization);
    if (model_ != nullptr && instanceStressDemoEnabled_) {
        static constexpr std::array<glm::vec3, 6> instanceTints{
            glm::vec3(0.82f, 0.34f, 0.22f),
            glm::vec3(0.86f, 0.62f, 0.20f),
            glm::vec3(0.30f, 0.72f, 0.42f),
            glm::vec3(0.20f, 0.54f, 0.86f),
            glm::vec3(0.48f, 0.32f, 0.82f),
            glm::vec3(0.78f, 0.28f, 0.60f)
        };
        constexpr int instanceColumns = 50;
        constexpr int instanceRows = 50;
        for (int row = 0; row < instanceRows; ++row) {
            for (int column = 0; column < instanceColumns; ++column) {
                const int index = row * instanceColumns + column;
                const glm::vec3 position(
                    -17.15f + static_cast<float>(column) * 0.70f,
                    -0.25f + 0.08f * static_cast<float>((row + column) % 4),
                    -17.15f + static_cast<float>(row) * 0.70f
                );
                glm::mat4 instanceMatrix = glm::translate(glm::mat4(1.0f), position);
                instanceMatrix = glm::rotate(
                    instanceMatrix,
                    glm::radians(static_cast<float>((index * 29) % 360)),
                    glm::vec3(0.0f, 1.0f, 0.0f)
                );
                instanceMatrix = glm::scale(instanceMatrix, glm::vec3(0.22f));
                instanceMatrix *= normalization;
                renderItems.push_back(RenderItem{
                    model_.get(),
                    instanceMatrix,
                    instanceTints[static_cast<std::size_t>(index) % instanceTints.size()],
                    true,
                    false,
                    true
                });
            }
        }
    } else if (model_ != nullptr && lightStressDemoEnabled_) {
        static constexpr std::array<glm::vec3, 6> instanceTints{
            glm::vec3(0.95f, 0.36f, 0.24f),
            glm::vec3(0.96f, 0.70f, 0.24f),
            glm::vec3(0.42f, 0.86f, 0.48f),
            glm::vec3(0.22f, 0.68f, 0.96f),
            glm::vec3(0.52f, 0.38f, 0.94f),
            glm::vec3(0.92f, 0.32f, 0.70f)
        };
        constexpr int instanceColumns = 10;
        constexpr int instanceRows = 10;
        for (int row = 0; row < instanceRows; ++row) {
            for (int column = 0; column < instanceColumns; ++column) {
                const int index = row * instanceColumns + column;
                const glm::vec3 position(
                    -3.24f + static_cast<float>(column) * 0.72f,
                    groundOffset_ + 0.25f,
                    -3.24f + static_cast<float>(row) * 0.72f
                );
                glm::mat4 instanceMatrix = glm::translate(glm::mat4(1.0f), position);
                instanceMatrix = glm::rotate(
                    instanceMatrix,
                    glm::radians(static_cast<float>((index * 29) % 360)),
                    glm::vec3(0.0f, 1.0f, 0.0f)
                );
                instanceMatrix = glm::rotate(
                    instanceMatrix,
                    glm::radians(static_cast<float>((row + column) % 3) * 7.0f),
                    glm::vec3(1.0f, 0.0f, 0.0f)
                );
                instanceMatrix = glm::scale(instanceMatrix, glm::vec3(0.36f));
                instanceMatrix *= normalization;
                renderItems.push_back(RenderItem{
                    model_.get(),
                    instanceMatrix,
                    instanceTints[static_cast<std::size_t>(index) % instanceTints.size()],
                    true,
                    false
                });
            }
        }
    }
    if (!benchmarkMode_ && (lightStressDemoEnabled_ || instanceStressDemoEnabled_)) materializeStressEntities(renderItems);
    if (!lightStressDemoEnabled_ && !instanceStressDemoEnabled_) {
        renderItems = scene_.buildRenderItems();
    }
    if (!loadedSceneDocument_) {
        rendererSettings_.causticsReceiverPlaneY =
            modelPosition_.y + groundOffset_ * modelScale_ + 0.002f;
    }
    rendererSettings_.causticsAnimationPhase = rendererSettings_.causticsAnimated
        ? static_cast<float>(std::fmod(glfwGetTime() * 0.16, 1.0))
        : 0.0f;
    if (lightStressDemoEnabled_ || instanceStressDemoEnabled_) {
        for (const RenderItem& item : scene_.buildRenderItems()) {
            if (std::find(stressEntities_.begin(), stressEntities_.end(), item.entityId) == stressEntities_.end()
                && item.entityId != primaryEntity_ && item.entityId != comparisonEntity_ && item.model != groundModel_.get()
                && item.model != glassBackdropModel_.get()) renderItems.push_back(item);
        }
    }
    renderer_->render(renderItems, camera_, rendererSettings_, width, height);
    if (referenceComparisonMode_ && !referenceComparisonComplete_
        && !pendingModelImport_.has_value() && !scene_.entities().empty()) {
        if (referenceComparisonWarmupFrames_ > 0) {
            --referenceComparisonWarmupFrames_;
        } else {
            captureReferenceComparison(width, height);
        }
    }
    if (!benchmarkMode_ && !prismReelMode_ && !referenceComparisonMode_) {
        renderer_->drawSelectionOutline(renderItems, camera_, selectedSceneEntity_, rendererSettings_.cullBackFaces);
    }

    if (prismReelMode_ && model_ != nullptr && !pendingModelImport_.has_value()) {
        if (prismReelWarmupFrames_ > 0) {
            --prismReelWarmupFrames_;
        } else {
            char filename[32]{};
            std::snprintf(filename, sizeof(filename), "frame_%04d.png", prismReelFrameIndex_);
            std::string reelError;
            if (!renderer_->saveScreenshot(prismReelFramesDirectory_ / filename, reelError)) {
                std::cerr << "Prism reel frame failed: " << reelError << '\n';
                glfwSetWindowShouldClose(window_, GLFW_TRUE);
            } else if (++prismReelFrameIndex_ >= prismReelFrameCount_) {
                std::cout << "Saved " << prismReelFrameIndex_
                          << " Prism reel frames to " << prismReelFramesDirectory_ << '\n';
                glfwSetWindowShouldClose(window_, GLFW_TRUE);
            }
        }
    }

    if (!pendingScreenshotPath_.empty() && !scene_.entities().empty() && !pendingModelImport_.has_value()
        && pendingScreenshotWarmupFrames_ > 0) {
        --pendingScreenshotWarmupFrames_;
    } else if (!pendingScreenshotPath_.empty() && !scene_.entities().empty()
        && !pendingModelImport_.has_value()) {
        std::string screenshotError;
        if (renderer_->saveScreenshot(pendingScreenshotPath_, screenshotError)) {
            statusMessage_ = "Saved screenshot (MSAA "
                           + std::to_string(renderer_->activeMsaaSamples())
                           + "x): " + pendingScreenshotPath_.string();
            std::cout << statusMessage_ << '\n';
        } else {
            statusMessage_ = "Screenshot failed: " + screenshotError;
            std::cerr << statusMessage_ << '\n';
        }
        pendingScreenshotPath_.clear();
    }

    ImGui::Image(
        static_cast<ImTextureID>(static_cast<std::uintptr_t>(renderer_->colorTexture())),
        ImVec2(static_cast<float>(width), static_cast<float>(height)),
        ImVec2(0.0f, 1.0f),
        ImVec2(1.0f, 0.0f)
    );

    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        selectEntity(pickEntity(renderItems, width, height,
            static_cast<int>(mouse.x - min.x), height - 1 - static_cast<int>(mouse.y - min.y)));
        ImGui::SetWindowFocus();
    }
    if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput
        && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) deleteSelectedEntity();
    if (ImGui::IsItemHovered() && !(prismDemoEnabled_ && prismCameraLocked_)) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f) {
            camera_.zoom(io.MouseWheel);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            camera_.orbit(-io.MouseDelta.x * 0.007f, -io.MouseDelta.y * 0.007f);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            camera_.pan(io.MouseDelta.x, io.MouseDelta.y);
        }
    }
    if (rendererSettings_.showAxes) {
        drawOrientationGizmo();
    }
    ImGui::End();
}

void Application::captureReferenceComparison(int width, int height) {
    referenceComparisonComplete_ = true;
    try {
        std::filesystem::create_directories(referenceComparisonDirectory_);
        const std::filesystem::path rasterPath = referenceComparisonDirectory_ / "raster.png";
        std::string screenshotError;
        if (!renderer_->saveScreenshot(rasterPath, screenshotError)) {
            throw std::runtime_error("Raster capture failed: " + screenshotError);
        }

        pathtracer::RenderSettings settings;
        settings.width = static_cast<std::uint32_t>(width);
        settings.height = static_cast<std::uint32_t>(height);
        settings.samplesPerPixel = referenceComparisonSamples_;
        settings.maxDepth = referenceComparisonMaxDepth_;
        settings.seed = referenceComparisonSeed_;
        const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
        pathtracer::ProgressiveRenderer progressive(
            pathtracer::captureSceneSnapshot(scene_, camera_, aspectRatio, rendererSettings_),
            settings
        );
        std::cout << "Reference comparison: tracing " << width << 'x' << height
                  << " at " << referenceComparisonSamples_ << " SPP...\n";
        while (progressive.renderPass()) {
        }
        if (!progressive.complete()) {
            throw std::runtime_error("Path-traced comparison did not complete");
        }

        const pathtracer::RenderImage& image = progressive.image();
        const std::filesystem::path aovStem = referenceComparisonDirectory_ / "aov" / "path-traced";
        pathtracer::writeReferenceImage(image, aovStem);
        pathtracer::writeReferenceAovs(image, aovStem);
        pathtracer::ReferenceComparisonOptions options;
        options.exposure = rendererSettings_.exposure;
        options.toneMapping = rendererSettings_.toneMapping;
        options.targetSamplesPerPixel = settings.samplesPerPixel;
        options.maxDepth = settings.maxDepth;
        options.seed = settings.seed;
        options.sceneName = currentScenePath_.empty()
            ? currentModelPath_.filename().u8string()
            : currentScenePath_.filename().u8string();
        const pathtracer::ReferenceComparisonMetrics metrics = pathtracer::writeReferenceComparison(
            image, rasterPath, referenceComparisonDirectory_, options
        );
        std::cout << std::fixed << std::setprecision(6)
                  << "Reference comparison complete: MAE=" << metrics.meanAbsoluteError
                  << ", RMSE=" << metrics.rootMeanSquaredError
                  << ", changed=" << metrics.changedFraction * 100.0 << "%\n"
                  << "Artifacts: " << referenceComparisonDirectory_.string() << '\n';
        statusMessage_ = "Raster/path-traced comparison saved: "
            + referenceComparisonDirectory_.string();
    } catch (const std::exception& error) {
        referenceComparisonFailed_ = true;
        statusMessage_ = "Reference comparison failed: " + std::string(error.what());
        std::cerr << statusMessage_ << '\n';
    }
    glfwSetWindowShouldClose(window_, GLFW_TRUE);
}

void Application::drawOrientationGizmo() {
    const ImVec2 imageMin = ImGui::GetItemRectMin();
    const ImVec2 imageMax = ImGui::GetItemRectMax();
    if (imageMax.x - imageMin.x < 120.0f || imageMax.y - imageMin.y < 120.0f) {
        return;
    }

    struct AxisGuide {
        glm::vec3 direction;
        const char* label;
        ImU32 color;
        glm::vec3 cameraDirection{0.0f};
    };
    std::array<AxisGuide, 3> axes{
        AxisGuide{{1.0f, 0.0f, 0.0f}, "X", IM_COL32(255, 20, 36, 255)},
        AxisGuide{{0.0f, 1.0f, 0.0f}, "Y", IM_COL32(26, 255, 56, 255)},
        AxisGuide{{0.0f, 0.0f, 1.0f}, "Z", IM_COL32(20, 86, 255, 255)}
    };
    const glm::mat3 viewRotation(camera_.viewMatrix());
    for (AxisGuide& axis : axes) {
        axis.cameraDirection = viewRotation * axis.direction;
    }
    std::sort(axes.begin(), axes.end(), [](const AxisGuide& left, const AxisGuide& right) {
        return left.cameraDirection.z < right.cameraDirection.z;
    });

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 center(imageMin.x + 58.0f, imageMax.y - 58.0f);
    constexpr float radius = 38.0f;
    drawList->AddCircleFilled(center, 49.0f, IM_COL32(15, 16, 18, 205), 32);
    drawList->AddCircle(center, 49.0f, IM_COL32(92, 96, 104, 190), 32, 1.0f);
    for (const AxisGuide& axis : axes) {
        const ImVec2 endpoint(
            center.x + axis.cameraDirection.x * radius,
            center.y - axis.cameraDirection.y * radius
        );
        drawList->AddLine(center, endpoint, axis.color, 3.5f);
        drawList->AddCircleFilled(endpoint, 5.0f, axis.color, 12);
        const ImVec2 textSize = ImGui::CalcTextSize(axis.label);
        const float offsetX = endpoint.x >= center.x ? 7.0f : -textSize.x - 7.0f;
        const float offsetY = endpoint.y >= center.y ? 4.0f : -textSize.y - 4.0f;
        drawList->AddText({endpoint.x + offsetX, endpoint.y + offsetY}, axis.color, axis.label);
    }
    drawList->AddCircleFilled(center, 3.5f, IM_COL32(230, 235, 245, 255), 12);
}

void Application::drawDiagnostics() {
    if (modelDiagnostics_.empty()) {
        ImGui::TextDisabled("No import diagnostics.");
        return;
    }

    if (!ImGui::TreeNodeEx(EditorUi::label("Import diagnostics"), ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    static constexpr std::array<ModelDiagnosticScope, 5> scopes{
        ModelDiagnosticScope::File,
        ModelDiagnosticScope::Node,
        ModelDiagnosticScope::Mesh,
        ModelDiagnosticScope::Material,
        ModelDiagnosticScope::Texture
    };
    for (const ModelDiagnosticScope scope : scopes) {
        const std::size_t count = static_cast<std::size_t>(std::count_if(
            modelDiagnostics_.begin(),
            modelDiagnostics_.end(),
            [scope](const ModelDiagnostic& diagnostic) { return diagnostic.scope == scope; }
        ));
        if (count == 0U) {
            continue;
        }
        const std::string label = std::string(diagnosticScopeName(scope))
                                + " (" + std::to_string(count) + ")";
        if (!ImGui::TreeNode(label.c_str())) {
            continue;
        }
        for (std::size_t index = 0; index < modelDiagnostics_.size(); ++index) {
            const ModelDiagnostic& diagnostic = modelDiagnostics_[index];
            if (diagnostic.scope != scope) {
                continue;
            }
            ImGui::PushID(static_cast<int>(index));
            const ImVec4 color = diagnostic.severity == ModelDiagnosticSeverity::Error
                ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                : diagnostic.severity == ModelDiagnosticSeverity::Warning
                    ? ImVec4(1.0f, 0.75f, 0.25f, 1.0f)
                    : ImVec4(0.45f, 0.72f, 1.0f, 1.0f);
            ImGui::TextColored(
                color,
                "%s%s%s",
                diagnosticSeverityName(diagnostic.severity),
                diagnostic.context.empty() ? "" : " - ",
                diagnostic.context.c_str()
            );
            ImGui::TextWrapped("%s", diagnostic.message.c_str());
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    ImGui::TreePop();
}

void Application::drawAboutPopup() {
    if (showAbout_) {
        ImGui::OpenPopup(EditorUi::label("About MyRenderer"));
        showAbout_ = false;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(EditorUi::label("About MyRenderer"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("MyRenderer 0.1.0");
        ImGui::Separator();
        ImGui::Text("C++17 / OpenGL 3.3 / GPU rasterization");
        ImGui::TextWrapped("A compact GPU renderer with a format-independent model pipeline.");
        if (ImGui::Button(EditorUi::label("Close"), ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Application::discoverModels() {
    availableModels_.clear();
    availableScenes_.clear();
    unsupportedModelCount_ = 0;
    const auto modelDirectory = sourceRoot_ / "assets" / "models";
    if (!std::filesystem::exists(modelDirectory)) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(modelDirectory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (findImporter(entry.path()) != nullptr) {
            availableModels_.push_back(entry.path());
        } else {
            const std::string extension = lowercase(entry.path().extension().string());
            if (extension == ".dae" || extension == ".fbx" || extension == ".gltf" || extension == ".glb") {
                ++unsupportedModelCount_;
            }
        }
    }
    std::sort(availableModels_.begin(), availableModels_.end());
    const auto sceneDirectory = sourceRoot_ / "assets" / "scenes";
    if (std::filesystem::exists(sceneDirectory)) {
        for (const auto& entry : std::filesystem::directory_iterator(sceneDirectory)) {
            if (entry.is_regular_file()
                && lowercase(entry.path().extension().string()) == myRendererSceneExtension) {
                availableScenes_.push_back(entry.path());
            }
        }
        std::sort(availableScenes_.begin(), availableScenes_.end());
    }
}

bool Application::loadModel(const std::filesystem::path& path, bool append) {
    if (pendingModelImport_.has_value()) {
        statusMessage_ = "A model is already loading; wait for it to finish before starting another import.";
        return false;
    }

    try {
        const auto resolved = resolvePath(path);
        const ModelImporter* importer = findImporter(resolved);
        if (importer == nullptr) {
            throw std::runtime_error("No model importer supports: " + resolved.string());
        }

        std::error_code fileSizeError;
        const std::uintmax_t fileSize = std::filesystem::file_size(resolved, fileSizeError);
        modelDiagnostics_.clear();
        modelDiagnostics_.push_back(ModelDiagnostic{
            ModelDiagnosticScope::File,
            ModelDiagnosticSeverity::Info,
            resolved.filename().string(),
            "CPU asset import started; the current scene will remain active until validation succeeds."
        });
        const std::string pathString = resolved.string();
        std::snprintf(modelPathBuffer_.data(), modelPathBuffer_.size(), "%s", pathString.c_str());
        statusMessage_ = "Loading " + resolved.filename().string() + " on a background CPU task...";
        lastLoadFailed_ = false;
        std::cout << statusMessage_ << '\n';

        auto future = std::async(std::launch::async, [importer, resolved]() {
            const auto startedAt = std::chrono::steady_clock::now();
            ModelImportResult loaded = importer->load(resolved);
            loaded.cpuTimeMilliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - startedAt
            ).count();
            return loaded;
        });
        pendingModelImport_.emplace(PendingModelImport{
            resolved,
            std::move(future),
            std::chrono::steady_clock::now(),
            fileSizeError ? 0U : fileSize,
            append,
            sceneGeneration_
        });
        return true;
    } catch (const std::exception& error) {
        lastLoadFailed_ = true;
        statusMessage_ = std::string("Load failed: ") + error.what();
        modelDiagnostics_.clear();
        modelDiagnostics_.push_back(ModelDiagnostic{
            ModelDiagnosticScope::File,
            ModelDiagnosticSeverity::Error,
            path.filename().string(),
            error.what()
        });
        std::cerr << statusMessage_ << '\n';
        return false;
    }
}

void Application::updateModelLoad() {
    if (!pendingModelImport_.has_value()
        || pendingModelImport_->future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        return;
    }

    PendingModelImport pending = std::move(*pendingModelImport_);
    pendingModelImport_.reset();
    try {
        ModelImportResult loaded = pending.future.get();
        if (pending.generation != sceneGeneration_) return;
        finishModelLoad(pending.path, std::move(loaded), pending.append);
    } catch (const std::exception& error) {
        if (pending.generation != sceneGeneration_) return;
        lastLoadFailed_ = true;
        lastLoadTotalMilliseconds_ = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - pending.startedAt
        ).count();
        statusMessage_ = "Load failed; current scene preserved: " + std::string(error.what());
        modelDiagnostics_.clear();
        modelDiagnostics_.push_back(ModelDiagnostic{
            ModelDiagnosticScope::File,
            ModelDiagnosticSeverity::Error,
            pending.path.filename().string(),
            error.what()
        });
        std::cerr << statusMessage_ << '\n';
    }
}

void Application::finishModelLoad(const std::filesystem::path& path, ModelImportResult loaded, bool append) {
    const auto gpuUploadStartedAt = std::chrono::steady_clock::now();
    const glm::vec3 modelBoundsMin = loaded.model.boundsMin;
    const glm::vec3 modelBoundsMax = loaded.model.boundsMax;
    const glm::vec3 extent = modelBoundsMax - modelBoundsMin;
    const float maximumExtent = std::max({extent.x, extent.y, extent.z});
    if (!std::isfinite(maximumExtent) || maximumExtent <= 1e-8f) {
        throw std::runtime_error("Model bounds are empty or degenerate");
    }

    std::vector<TextureUploadWarning> textureWarnings;
    auto newModel = std::make_unique<GpuModel>(
        std::move(loaded.model),
        renderer_->textureCache(),
        textureWarnings
    );

    lastCpuImportMilliseconds_ = loaded.cpuTimeMilliseconds;
    lastGpuUploadMilliseconds_ = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - gpuUploadStartedAt
    ).count();
    lastLoadTotalMilliseconds_ = lastCpuImportMilliseconds_ + lastGpuUploadMilliseconds_;
    lastLoadFailed_ = false;
    modelDiagnostics_ = std::move(loaded.diagnostics);
    for (auto& warning : textureWarnings) {
        modelDiagnostics_.push_back(ModelDiagnostic{
            ModelDiagnosticScope::Texture,
            ModelDiagnosticSeverity::Warning,
            std::move(warning.textureName),
            std::move(warning.message)
        });
    }

    if (append && (model_ || emptySceneSession_ || loadedSceneDocument_ || !scene_.entities().empty())) {
        // GPU resources are owned by the scene session, including shared duplicates.
        const auto* gpu = newModel.get();
        importedModels_.push_back(std::move(newModel));
        selectEntity(scene_.createEntity(
            path.filename().u8string(), gpu, path.generic_u8string()
        ));
        SceneEntity* entity = scene_.find(selectedSceneEntity_);
        entity->transform.assetTransform =
            glm::scale(glm::mat4(1.0f), glm::vec3(1.4f / maximumExtent))
            * glm::translate(glm::mat4(1.0f), -0.5f * (modelBoundsMin + modelBoundsMax));
        entity->transform.translation.x = 1.7f * static_cast<float>(importedModels_.size() - (emptySceneSession_ ? 1 : 0));
        statusMessage_ = "Added " + path.filename().u8string() + " to scene";
        std::cout << statusMessage_ << " (entities: " << scene_.size() << ")\n";
        return;
    }
    emptySceneSession_ = false;
    loadedSceneDocument_ = false;
    currentScenePath_.clear();
    scene_.clear();
    importedModels_.clear();
    model_ = std::move(newModel);
    currentModelPath_ = path;
    loadedMeshCount_ = model_->meshCount();
    loadedSubmeshCount_ = model_->submeshCount();
    loadedTransparentSubmeshCount_ = model_->transparentSubmeshCount();
    loadedVertexCount_ = model_->vertexCount();
    loadedTriangleCount_ = model_->triangleCount();
    loadedMaterialCount_ = model_->materialCount();
    loadedTextureCount_ = model_->textureCount();
    loadedDecodedTextureCount_ = model_->loadedTextureCount();
    loadedFallbackTextureCount_ = model_->fallbackTextureCount();
    loadedTextureMemoryBytes_ = model_->textureMemoryBytes();
    animationClipIndex_ = 0U;
    if (!animationTimeFixed_) animationTimeSeconds_ = 0.0f;
    if (model_->hasSkinning()) {
        animationEnabled_ = animationDemoEnabled_
            || std::getenv("MYRENDERER_ANIMATION") == nullptr
            || animationEnabled_;
        rendererSettings_.showGrid = false;
        rendererSettings_.showAxes = false;
        showGroundPlane_ = false;
        rendererSettings_.skyboxEnabled = false;
        rendererSettings_.backgroundColor = glm::vec3(0.012f, 0.016f, 0.026f);
        rendererSettings_.bloom = false;
        rendererSettings_.shadowsEnabled = false;
        camera_.setOrbitPose(glm::vec3(0.0f, 0.6f, 0.0f), 0.0f, 4.0f, 3.6f, 38.0f);
    }
    modelCenter_ = 0.5f * (modelBoundsMin + modelBoundsMax);
    modelNormalizationScale_ = 1.4f / maximumExtent;
    resetObjectTransform();
    const bool loadedPrismFixture = lowercase(path.filename().string()) == "prism_spectrum.gltf";
    const bool loadedGlassVolumeFixture =
        lowercase(path.filename().string()) == "glass_volume_sphere.gltf";
    const bool wasGlassVolumeDemo = glassVolumeDemoEnabled_;
    if (loadedPrismFixture || loadedGlassVolumeFixture) {
        lightStressDemoEnabled_ = false;
        instanceStressDemoEnabled_ = false;
        rendererSettings_.instanceOptimizationEnabled = false;
        rendererSettings_.localLights.clear();
    }
    if (loadedPrismFixture) {
        activatePrismDemoPreset(false);
    } else {
        deactivatePrismDemoPreset();
        glassVolumeDemoEnabled_ = loadedGlassVolumeFixture;
        if (loadedGlassVolumeFixture) {
            modelPosition_ = glassCausticsDemoEnabled_
                ? glm::vec3(0.0f, 0.0f, 0.0f)
                : glm::vec3(-0.46f, 0.0f, 0.0f);
            showComparisonObject_ = !glassCausticsDemoEnabled_;
            showGroundPlane_ = glassCausticsDemoEnabled_;
            rendererSettings_.showGrid = false;
            rendererSettings_.showAxes = false;
            rendererSettings_.backgroundColor = glassCausticsDemoEnabled_
                ? glm::vec3(0.0015f, 0.0020f, 0.0030f)
                : glm::vec3(0.018f, 0.022f, 0.03f);
            rendererSettings_.environmentIntensity = glassCausticsDemoEnabled_ ? 0.38f : 0.85f;
            rendererSettings_.skyboxEnabled = !glassCausticsDemoEnabled_;
            rendererSettings_.volumeGlassOverrideEnabled = true;
            applyVolumeGlassPreset(volumeGlassPreset_);
            rendererSettings_.causticsEnabled = glassCausticsDemoEnabled_;
            rendererSettings_.causticsMode = CausticsMode::LightSpace;
            rendererSettings_.causticsStrength = 2.4f;
            rendererSettings_.causticsScale = 1.15f;
            rendererSettings_.causticsDirection = glassCausticsDemoEnabled_
                ? glm::vec3(-0.62f, 0.0f, 0.18f)
                : glm::vec3(0.0f);
            rendererSettings_.causticsSharpness = 0.78f;
            rendererSettings_.coloredTransmissionShadowsEnabled = true;
            if (const char* value = std::getenv("MYRENDERER_CAUSTICS")) {
                rendererSettings_.causticsEnabled = std::atoi(value) != 0;
            }
            if (const char* value = std::getenv("MYRENDERER_CAUSTICS_MODE")) {
                rendererSettings_.causticsMode = std::atoi(value) == 0
                    ? CausticsMode::Projector
                    : CausticsMode::LightSpace;
            }
            if (const char* value = std::getenv("MYRENDERER_TRANSMISSION_SHADOWS")) {
                rendererSettings_.coloredTransmissionShadowsEnabled = std::atoi(value) != 0;
            }
            if (const char* value = std::getenv("MYRENDERER_TRANSMISSION")) {
                rendererSettings_.transmissionEnabled = std::atoi(value) != 0;
            }
            if (const char* value = std::getenv("MYRENDERER_DISPERSION_ENABLED")) {
                rendererSettings_.dispersionEnabled = std::atoi(value) != 0;
            }
            if (const char* value = std::getenv("MYRENDERER_DISPERSION")) {
                rendererSettings_.dispersionStrength = std::clamp(
                    std::strtof(value, nullptr), 0.0f, 2.5f
                );
            }
            if (const char* value = std::getenv("MYRENDERER_IOR")) {
                rendererSettings_.indexOfRefractionOverride = std::clamp(
                    std::strtof(value, nullptr), 1.0f, 3.0f
                );
            }
            groundColor_ = glassCausticsDemoEnabled_
                ? glm::vec3(0.82f, 0.84f, 0.88f)
                : groundColor_;
            camera_.setOrbitPose(
                glassCausticsDemoEnabled_
                    ? glm::vec3(0.0f, -0.12f, 0.0f)
                    : glm::vec3(0.0f),
                glassCausticsDemoEnabled_ ? -12.0f : 0.0f,
                glassCausticsDemoEnabled_ ? 30.0f : 0.0f,
                glassCausticsDemoEnabled_ ? 3.4f : 3.35f,
                38.0f
            );
        } else {
            if (wasGlassVolumeDemo) {
                showComparisonObject_ = false;
                showGroundPlane_ = true;
                rendererSettings_.showGrid = true;
                rendererSettings_.showAxes = true;
                rendererSettings_.volumeGlassOverrideEnabled = false;
                rendererSettings_.causticsEnabled = false;
                rendererSettings_.skyboxEnabled = true;
            }
            glassCausticsDemoEnabled_ = false;
            if (instanceStressDemoEnabled_) {
                activateInstanceStressPreset(false);
            } else if (lightStressDemoEnabled_) {
                activateLightStressPreset(false);
            } else {
                camera_.reset();
            }
        }
    }

    rebuildSceneEntities();
    if (sceneFoundationDemoEnabled_) {
        camera_.setOrbitPose(glm::vec3(0.0f, -0.05f, 0.0f), -18.0f, 24.0f, 8.8f, 42.0f);
        rendererSettings_.showGrid = false;
        rendererSettings_.showAxes = false;
        showGroundPlane_ = true;
        groundOffset_ = -0.82f;
    }
    const std::string pathString = currentModelPath_.string();
    std::snprintf(modelPathBuffer_.data(), modelPathBuffer_.size(), "%s", pathString.c_str());
    statusMessage_ = "Loaded " + currentModelPath_.filename().string() + " ("
                   + std::to_string(loadedMeshCount_) + " mesh, "
                   + std::to_string(loadedSubmeshCount_) + " submesh, "
                   + std::to_string(loadedTransparentSubmeshCount_) + " transparent, "
                   + std::to_string(loadedVertexCount_) + " vertices, "
                   + std::to_string(loadedTriangleCount_) + " triangles, "
                   + std::to_string(loadedMaterialCount_) + " materials, "
                   + std::to_string(loadedTextureCount_) + " textures, "
                   + std::to_string(loadedDecodedTextureCount_) + " decoded, "
                   + std::to_string(loadedFallbackTextureCount_) + " fallback; "
                   + std::to_string(model_->jointCount()) + " joints, "
                   + std::to_string(model_->animationCount()) + " animations; "
                   + std::to_string(static_cast<int>(lastLoadTotalMilliseconds_)) + " ms)";
    std::cout << statusMessage_ << '\n';
    for (const ModelDiagnostic& diagnostic : modelDiagnostics_) {
        if (diagnostic.severity == ModelDiagnosticSeverity::Info) {
            continue;
        }
        std::cout << diagnosticSeverityName(diagnostic.severity) << " ["
                  << diagnosticScopeName(diagnostic.scope) << "] "
                  << diagnostic.context << ": " << diagnostic.message << '\n';
    }
}

void Application::queueDroppedFiles(int count, const char** paths) {
    if (count <= 0 || paths == nullptr) {
        return;
    }
    for (int index = 0; index < count; ++index) {
        const std::filesystem::path candidate = std::filesystem::u8path(paths[index]);
        if (findImporter(candidate) != nullptr) {
            droppedModelPaths_.push_back(candidate);
        }
    }
    statusMessage_ = "Queued " + std::to_string(droppedModelPaths_.size()) + " model(s) for import.";
}

const ModelImporter* Application::findImporter(const std::filesystem::path& path) const {
    for (const auto& importer : importers_) {
        if (importer->supports(path)) {
            return importer.get();
        }
    }
    return nullptr;
}

std::filesystem::path Application::resolvePath(const std::filesystem::path& path) const {
    if (path.empty()) {
        throw std::runtime_error("Model path is empty");
    }
    if (std::filesystem::exists(path)) {
        return std::filesystem::absolute(path).lexically_normal();
    }
    const auto fromSource = sourceRoot_ / path;
    if (std::filesystem::exists(fromSource)) {
        return std::filesystem::absolute(fromSource).lexically_normal();
    }
    const auto byFilename = sourceRoot_ / "assets" / "models" / path.filename();
    if (std::filesystem::exists(byFilename)) {
        return std::filesystem::absolute(byFilename).lexically_normal();
    }
    return std::filesystem::absolute(path).lexically_normal();
}

std::filesystem::path Application::nextScreenshotPath() const {
    const std::filesystem::path directory = sourceRoot_ / "screenshots";
    const std::string stem = currentModelPath_.empty() ? "viewport" : currentModelPath_.stem().string();
    for (std::size_t sequence = 1; sequence < 10000U; ++sequence) {
        const std::filesystem::path candidate = directory
            / (stem + "_" + std::to_string(sequence) + ".png");
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return directory / (stem + "_latest.png");
}

void Application::resetObjectTransform() {
    modelPosition_ = glm::vec3(0.0f);
    modelRotationDegrees_ = glm::vec3(0.0f);
    modelScale_ = 1.0f;
}


void Application::rebuildLocalLights() {
    static constexpr std::array<int, 3> tierCounts{8, 32, 64};
    static constexpr std::array<glm::vec3, 8> palette{
        glm::vec3(1.00f, 0.18f, 0.10f),
        glm::vec3(1.00f, 0.52f, 0.08f),
        glm::vec3(0.95f, 0.88f, 0.22f),
        glm::vec3(0.18f, 0.90f, 0.42f),
        glm::vec3(0.10f, 0.62f, 1.00f),
        glm::vec3(0.30f, 0.24f, 1.00f),
        glm::vec3(0.76f, 0.18f, 1.00f),
        glm::vec3(1.00f, 0.18f, 0.58f)
    };
    const int count = tierCounts[static_cast<std::size_t>(
        std::clamp(localLightTierIndex_, 0, 2)
    )];
    const int columns = count == 8 ? 4 : 8;
    const int rows = count / columns;
    rendererSettings_.localLights.clear();
    rendererSettings_.localLights.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        const int column = index % columns;
        const int row = index / columns;
        const float x = -3.15f + 6.30f * static_cast<float>(column)
            / static_cast<float>(std::max(columns - 1, 1));
        const float z = -3.15f + 6.30f * static_cast<float>(row)
            / static_cast<float>(std::max(rows - 1, 1));
        const bool spot = (column + row) % 2 != 0;
        LocalLight light;
        light.type = spot ? LocalLightType::Spot : LocalLightType::Point;
        light.position = glm::vec3(
            x,
            spot ? 1.85f : 0.22f + 0.12f * static_cast<float>(index % 3),
            z
        );
        light.radius = spot ? 4.3f : 2.8f;
        light.color = palette[static_cast<std::size_t>(index) % palette.size()];
        light.intensity = spot ? 18.0f : 11.0f;
        light.direction = spot
            ? glm::normalize(glm::vec3(-x * 0.10f, -1.75f, -z * 0.10f))
            : glm::vec3(0.0f, -1.0f, 0.0f);
        light.outerConeCosine = 0.82f;
        rendererSettings_.localLights.push_back(light);
    }
}

void Application::activateLightStressPreset(bool loadFixture) {
    deactivatePrismDemoPreset();
    instanceStressDemoEnabled_ = false;
    rendererSettings_.instanceOptimizationEnabled = false;
    lightStressDemoEnabled_ = true;
    glassVolumeDemoEnabled_ = false;
    glassCausticsDemoEnabled_ = false;
    autoRotate_ = false;
    showGroundPlane_ = true;
    showComparisonObject_ = false;
    modelPosition_ = glm::vec3(0.0f);
    modelRotationDegrees_ = glm::vec3(0.0f);
    modelScale_ = 1.0f;
    groundOffset_ = -0.72f;
    groundColor_ = glm::vec3(0.10f, 0.115f, 0.14f);
    rendererSettings_.showGrid = false;
    rendererSettings_.showAxes = false;
    rendererSettings_.backgroundColor = glm::vec3(0.0025f, 0.0035f, 0.0060f);
    rendererSettings_.skyboxEnabled = false;
    rendererSettings_.pbrEnabled = true;
    rendererSettings_.iblEnabled = true;
    rendererSettings_.environmentIntensity = 0.08f;
    rendererSettings_.ambientStrength = 0.015f;
    rendererSettings_.diffuseStrength = 1.0f;
    rendererSettings_.shadowsEnabled = false;
    rendererSettings_.coloredTransmissionShadowsEnabled = false;
    rendererSettings_.causticsEnabled = false;
    rendererSettings_.transmissionEnabled = false;
    rendererSettings_.toneMapping = true;
    rendererSettings_.bloom = true;
    rendererSettings_.exposure = 1.0f;
    rendererSettings_.bloomThreshold = 0.85f;
    rendererSettings_.bloomIntensity = 0.10f;
    rebuildLocalLights();
    camera_.setOrbitPose(glm::vec3(0.0f, -0.40f, 0.0f), 42.0f, 30.0f, 10.5f, 48.0f);
    statusMessage_ = "Local light stress: 100 objects, "
        + std::to_string(rendererSettings_.localLights.size())
        + " point/spot lights";
    if (loadFixture) {
        loadModel(sourceRoot_ / "assets" / "models" / "cube.obj");
    }
}

void Application::activateInstanceStressPreset(bool loadFixture) {
    deactivatePrismDemoPreset();
    lightStressDemoEnabled_ = false;
    instanceStressDemoEnabled_ = true;
    glassVolumeDemoEnabled_ = false;
    glassCausticsDemoEnabled_ = false;
    rendererSettings_.localLights.clear();
    if (loadFixture) rendererSettings_.instanceOptimizationEnabled = true;
    autoRotate_ = false;
    showGroundPlane_ = false;
    showComparisonObject_ = false;
    modelPosition_ = glm::vec3(0.0f);
    modelRotationDegrees_ = glm::vec3(0.0f);
    modelScale_ = 1.0f;
    rendererSettings_.showGrid = false;
    rendererSettings_.showAxes = false;
    rendererSettings_.backgroundColor = glm::vec3(0.008f, 0.011f, 0.018f);
    rendererSettings_.skyboxEnabled = false;
    rendererSettings_.pbrEnabled = true;
    rendererSettings_.iblEnabled = true;
    rendererSettings_.environmentIntensity = 0.16f;
    rendererSettings_.ambientStrength = 0.035f;
    rendererSettings_.diffuseStrength = 1.0f;
    rendererSettings_.shadowsEnabled = false;
    rendererSettings_.coloredTransmissionShadowsEnabled = false;
    rendererSettings_.causticsEnabled = false;
    rendererSettings_.transmissionEnabled = false;
    rendererSettings_.toneMapping = true;
    rendererSettings_.bloom = false;
    rendererSettings_.lodMediumThresholdPixels = 7.0f;
    rendererSettings_.lodHighThresholdPixels = 14.0f;
    camera_.setOrbitPose(glm::vec3(0.0f), 38.0f, 28.0f, 34.0f, 46.0f);
    statusMessage_ = rendererSettings_.instanceOptimizationEnabled
        ? "Instance stress: 2,500 spheres with batching, frustum culling, and LOD"
        : "Instance stress baseline: 2,500 independent sphere submissions";
    if (loadFixture) {
        loadModel(sourceRoot_ / "assets" / "models" / "sphere.obj");
    }
}

void Application::activateGlassCausticsPreset() {
    lightStressDemoEnabled_ = false;
    instanceStressDemoEnabled_ = false;
    rendererSettings_.instanceOptimizationEnabled = false;
    rendererSettings_.localLights.clear();
    glassCausticsDemoEnabled_ = true;
    volumeGlassPreset_ = VolumeGlassPreset::Crystal;
    if (!loadModel(sourceRoot_ / "assets" / "models" / "glass_volume_sphere.gltf")) {
        glassCausticsDemoEnabled_ = false;
    }
}

void Application::applyVolumeGlassPreset(VolumeGlassPreset preset) {
    volumeGlassPreset_ = preset;
    rendererSettings_.volumeGlassOverrideEnabled = true;
    rendererSettings_.volumeGlassTransmission = 1.0f;
    rendererSettings_.dispersionEnabled = true;

    switch (preset) {
    case VolumeGlassPreset::Clear:
        rendererSettings_.volumeGlassAttenuationColor = glm::vec3(1.0f);
        rendererSettings_.volumeGlassAttenuationDistance = 8.0f;
        rendererSettings_.volumeGlassRoughness = 0.04f;
        rendererSettings_.dispersionStrength = 0.0f;
        break;
    case VolumeGlassPreset::Olive:
        rendererSettings_.volumeGlassAttenuationColor = glm::vec3(0.68f, 0.86f, 0.22f);
        rendererSettings_.volumeGlassAttenuationDistance = 0.85f;
        rendererSettings_.volumeGlassRoughness = 0.06f;
        rendererSettings_.dispersionStrength = 0.0f;
        break;
    case VolumeGlassPreset::Amber:
        rendererSettings_.volumeGlassAttenuationColor = glm::vec3(1.0f, 0.48f, 0.12f);
        rendererSettings_.volumeGlassAttenuationDistance = 0.72f;
        rendererSettings_.volumeGlassRoughness = 0.08f;
        rendererSettings_.dispersionStrength = 0.0f;
        break;
    case VolumeGlassPreset::Crystal:
        rendererSettings_.volumeGlassAttenuationColor = glm::vec3(0.78f, 0.92f, 1.0f);
        rendererSettings_.volumeGlassAttenuationDistance = 2.0f;
        rendererSettings_.volumeGlassRoughness = 0.06f;
        rendererSettings_.dispersionStrength = 2.0f;
        break;
    }
}

void Application::activatePrismDemoPreset(bool loadFixture) {
    lightStressDemoEnabled_ = false;
    instanceStressDemoEnabled_ = false;
    rendererSettings_.instanceOptimizationEnabled = false;
    rendererSettings_.localLights.clear();
    if (!prismDemoPreviousState_.has_value()) {
        prismDemoPreviousState_.emplace(PrismDemoPreviousState{
            rendererSettings_,
            autoRotate_,
            showGroundPlane_,
            showComparisonObject_
        });
    }
    prismDemoEnabled_ = true;
    prismCameraLocked_ = true;
    prismOpticalPreset_ = PrismOpticalPreset::CrownGlass;
    prismParameters_ = prismOpticalPresetParameters(prismOpticalPreset_);
    autoRotate_ = false;
    showGroundPlane_ = false;
    showComparisonObject_ = false;
    rendererSettings_.showGrid = false;
    rendererSettings_.showAxes = false;
    rendererSettings_.showPrismIncidentBeam = true;
    rendererSettings_.backgroundColor = glm::vec3(0.0015f, 0.0020f, 0.0025f);
    rendererSettings_.skyboxEnabled = false;
    rendererSettings_.shadowsEnabled = false;
    rendererSettings_.pbrEnabled = true;
    rendererSettings_.iblEnabled = true;
    rendererSettings_.transmissionEnabled = true;
    rendererSettings_.toneMapping = true;
    rendererSettings_.bloom = true;
    rendererSettings_.msaaSamples = 4;
    rendererSettings_.environmentIntensity = 0.58f;
    rendererSettings_.refractionScale = 0.28f;
    rendererSettings_.refractionSteps = 20;
    rendererSettings_.volumeThicknessScale = 1.0f;
    rendererSettings_.geometricThicknessEnabled = true;
    rendererSettings_.glassDebugView = GlassDebugView::Final;
    rendererSettings_.exposure = 1.20f;
    rendererSettings_.bloomThreshold = 0.75f;
    rendererSettings_.bloomIntensity = 0.22f;
    rendererSettings_.prismBeamOutputLength = 2.4f;
    rendererSettings_.prismBeamWidth = 0.055f;
    rendererSettings_.prismBeamIntensity = 5.0f;
    rendererSettings_.prismBeamEdgeSoftness = 0.72f;
    rendererSettings_.prismBeamBloomContribution = 0.35f;
    rendererSettings_.showPrismOpticalPathDebug = false;
    if (const char* value = std::getenv("MYRENDERER_MSAA")) {
        rendererSettings_.msaaSamples = std::atoi(value) <= 1 ? 1 : 4;
    }
    if (const char* value = std::getenv("MYRENDERER_PRISM_PRESET")) {
        const int requestedPreset = std::clamp(std::atoi(value), 0, 3);
        prismOpticalPreset_ = static_cast<PrismOpticalPreset>(requestedPreset);
        prismParameters_ = prismOpticalPresetParameters(prismOpticalPreset_);
    }
    if (const char* value = std::getenv("MYRENDERER_PRISM_BEAM_WIDTH")) {
        rendererSettings_.prismBeamWidth = std::clamp(
            std::strtof(value, nullptr),
            0.005f,
            0.16f
        );
    }
    if (const char* value = std::getenv("MYRENDERER_PRISM_BEAM_INTENSITY")) {
        rendererSettings_.prismBeamIntensity = std::clamp(
            std::strtof(value, nullptr),
            0.0f,
            16.0f
        );
    }
    if (const char* value = std::getenv("MYRENDERER_PRISM_BEAM_SOFTNESS")) {
        rendererSettings_.prismBeamEdgeSoftness = std::clamp(
            std::strtof(value, nullptr),
            0.01f,
            1.0f
        );
    }
    if (const char* value = std::getenv("MYRENDERER_PRISM_BLOOM_CONTRIBUTION")) {
        rendererSettings_.prismBeamBloomContribution = std::clamp(
            std::strtof(value, nullptr),
            0.0f,
            2.0f
        );
    }
    if (const char* value = std::getenv("MYRENDERER_GEOMETRIC_THICKNESS")) {
        rendererSettings_.geometricThicknessEnabled = std::atoi(value) != 0;
    }
    if (const char* value = std::getenv("MYRENDERER_PRISM_SAMPLES")) {
        static constexpr std::array<int, 4> qualityTiers{7, 15, 21, 31};
        const int requestedSamples = std::atoi(value);
        prismParameters_.spectralSampleCount = *std::min_element(
            qualityTiers.begin(),
            qualityTiers.end(),
            [requestedSamples](int left, int right) {
                return std::abs(left - requestedSamples) < std::abs(right - requestedSamples);
            }
        );
    }
    if (const char* value = std::getenv("MYRENDERER_PRISM_SPECTRUM_MODE")) {
        const std::string requestedMode = lowercase(value);
        if (requestedMode == "7" || requestedMode == "seven" || requestedMode == "seven-band") {
            prismParameters_.spectrumMode = PrismSpectrumMode::SevenBand;
        }
    }
    if (const char* value = std::getenv("MYRENDERER_PRISM_BEAM_ANGLE")) {
        prismParameters_.beamAngleDegrees = std::clamp(
            std::strtof(value, nullptr),
            -30.0f,
            30.0f
        );
    }
    if (const char* value = std::getenv("MYRENDERER_PRISM_IOR")) {
        prismParameters_.centralIndexOfRefraction = std::clamp(
            std::strtof(value, nullptr),
            1.0f,
            3.0f
        );
    }
    if (const char* value = std::getenv("MYRENDERER_PRISM_DISPERSION")) {
        prismParameters_.dispersion = std::clamp(
            std::strtof(value, nullptr),
            0.0f,
            2.5f
        );
    }
    if (const char* value = std::getenv("MYRENDERER_PRISM_WHITE_POINT")) {
        prismParameters_.whitePointKelvin = std::clamp(
            std::strtof(value, nullptr),
            1000.0f,
            12000.0f
        );
    }
    if (const char* value = std::getenv("MYRENDERER_PRISM_DEBUG")) {
        rendererSettings_.showPrismOpticalPathDebug = std::atoi(value) != 0;
    }
    if (const char* value = std::getenv("MYRENDERER_PRISM_SHOW_MODEL")) {
        prismModelVisible_ = std::atoi(value) != 0;
    }
    updatePrismDemoOptics();
    restorePrismHeroShot();

    if (loadFixture) {
        loadModel(sourceRoot_ / "assets" / "models" / "prism_spectrum.gltf");
    }
}

void Application::deactivatePrismDemoPreset() {
    if (!prismDemoEnabled_ && !prismDemoPreviousState_.has_value()) {
        return;
    }

    prismDemoEnabled_ = false;
    prismCameraLocked_ = false;
    if (prismDemoPreviousState_.has_value()) {
        rendererSettings_ = std::move(prismDemoPreviousState_->rendererSettings);
        autoRotate_ = prismDemoPreviousState_->autoRotate;
        showGroundPlane_ = prismDemoPreviousState_->showGroundPlane;
        showComparisonObject_ = prismDemoPreviousState_->showComparisonObject;
        prismDemoPreviousState_.reset();
    } else {
        rendererSettings_.indexOfRefractionOverride = 0.0f;
        rendererSettings_.dispersionStrength = 0.0f;
    }

    // Prism-only overlays must never leak into a regular model, including from
    // a previously customized or legacy state snapshot.
    rendererSettings_.showPrismIncidentBeam = false;
    rendererSettings_.showPrismOpticalPathDebug = false;
    rendererSettings_.prismOpticalPathValid = false;
    rendererSettings_.prismTotalInternalReflection = false;
    rendererSettings_.prismSpectrum.samples.clear();
}

void Application::updatePrismDemoOptics() {
    const PrismDemoSolution solution = solvePrismDemo(prismParameters_);
    rendererSettings_.prismSpectrum = solution.spectrum;
    rendererSettings_.prismOpticalPathValid = solution.valid;
    rendererSettings_.prismTotalInternalReflection = solution.totalInternalReflection;
    rendererSettings_.prismBeamWhitePoint = solution.linearWhitePoint;
    rendererSettings_.indexOfRefractionOverride = prismParameters_.centralIndexOfRefraction;
    rendererSettings_.dispersionEnabled = prismParameters_.dispersion > 0.0f;
    rendererSettings_.dispersionStrength = prismParameters_.dispersion;
}

void Application::applyPrismOpticalPreset(PrismOpticalPreset preset) {
    prismOpticalPreset_ = preset;
    prismParameters_ = prismOpticalPresetParameters(preset);
    updatePrismDemoOptics();
}

void Application::restorePrismHeroShot() {
    camera_.setOrbitPose(glm::vec3(0.0f), 0.0f, 0.0f, 4.8f, 35.0f);
}

void Application::writePrismBenchmarkReport() {
    if (benchmarkOutputPath_.empty() || renderer_ == nullptr) return;

    std::vector<double> solveTimes;
    solveTimes.reserve(256U);
    std::size_t solveChecksum = 0U;
    for (int iteration = 0; iteration < 256; ++iteration) {
        const auto startedAt = std::chrono::steady_clock::now();
        const PrismDemoSolution solution = solvePrismDemo(prismParameters_);
        const auto finishedAt = std::chrono::steady_clock::now();
        solveTimes.push_back(std::chrono::duration<double, std::milli>(
            finishedAt - startedAt
        ).count());
        solveChecksum += solution.spectrum.samples.size();
    }

    if (!benchmarkOutputPath_.parent_path().empty()) {
        std::filesystem::create_directories(benchmarkOutputPath_.parent_path());
    }
    std::ofstream report(benchmarkOutputPath_);
    if (!report) {
        std::cerr << "Cannot write benchmark report: " << benchmarkOutputPath_ << '\n';
        return;
    }
    const std::size_t geometryMemoryBytes = loadedVertexCount_ * sizeof(Vertex)
        + loadedTriangleCount_ * 3U * sizeof(std::uint32_t);
    const double gpuFrameP50 = percentile(benchmarkGpuFrameTimes_, 0.50);
    const std::size_t opaqueTrafficBytes = renderer_->estimatedOpaqueTrafficBytesPerFrame();
    const double estimatedOpaqueTrafficGiBPerSecond = gpuFrameP50 > 0.0
        ? (static_cast<double>(opaqueTrafficBytes) / (1024.0 * 1024.0 * 1024.0))
            / (gpuFrameP50 / 1000.0)
        : 0.0;
    const std::size_t spotLightCount = static_cast<std::size_t>(std::count_if(
        rendererSettings_.localLights.begin(),
        rendererSettings_.localLights.end(),
        [](const LocalLight& light) { return light.type == LocalLightType::Spot; }
    ));
    report << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schemaVersion\": 1,\n"
           << "  \"gpu\": " << std::quoted(gpuDescription_) << ",\n"
           << "  \"width\": " << renderer_->renderWidth() << ",\n"
           << "  \"height\": " << renderer_->renderHeight() << ",\n"
           << "  \"msaaSamples\": " << renderer_->activeMsaaSamples() << ",\n"
           << "  \"renderPath\": "
           << std::quoted(rendererSettings_.renderPath == RenderPath::Deferred
                ? "deferred" : "forward") << ",\n"
           << "  \"skinningEnabled\": "
           << (model_ != nullptr && model_->hasSkinning() ? "true" : "false") << ",\n"
           << "  \"animationEnabled\": " << (animationEnabled_ ? "true" : "false") << ",\n"
           << "  \"skinningDebugView\": " << rendererSettings_.skinningDebugView << ",\n"
           << "  \"jointCount\": " << (model_ == nullptr ? 0U : model_->jointCount()) << ",\n"
           << "  \"animationCount\": " << (model_ == nullptr ? 0U : model_->animationCount()) << ",\n"
           << "  \"spectralSamples\": " << rendererSettings_.prismSpectrum.samples.size() << ",\n"
           << "  \"lightStressScene\": " << (lightStressDemoEnabled_ ? "true" : "false") << ",\n"
           << "  \"instanceStressScene\": " << (instanceStressDemoEnabled_ ? "true" : "false") << ",\n"
           << "  \"instanceOptimizationEnabled\": "
           << (rendererSettings_.instanceOptimizationEnabled ? "true" : "false") << ",\n"
           << "  \"frustumCullingEnabled\": "
           << (rendererSettings_.frustumCullingEnabled ? "true" : "false") << ",\n"
           << "  \"lodSelectionEnabled\": "
           << (rendererSettings_.lodSelectionEnabled ? "true" : "false") << ",\n"
           << "  \"stressInstanceCount\": "
           << (instanceStressDemoEnabled_ ? 2500 : (lightStressDemoEnabled_ ? 100 : 1)) << ",\n"
           << "  \"submittedInstances\": " << renderer_->submittedInstanceCount() << ",\n"
           << "  \"visibleInstances\": " << renderer_->visibleInstanceCount() << ",\n"
           << "  \"culledInstances\": " << renderer_->culledInstanceCount() << ",\n"
           << "  \"lod0Instances\": " << renderer_->lodInstanceCounts()[0] << ",\n"
           << "  \"lod1Instances\": " << renderer_->lodInstanceCounts()[1] << ",\n"
           << "  \"lod2Instances\": " << renderer_->lodInstanceCounts()[2] << ",\n"
           << "  \"renderedInstanceTriangles\": "
           << renderer_->renderedInstanceTriangleCount() << ",\n"
           << "  \"instancePreparationMs\": "
           << renderer_->instancePreparationMilliseconds() << ",\n"
           << "  \"localLightCount\": " << rendererSettings_.localLights.size() << ",\n"
           << "  \"pointLightCount\": "
           << rendererSettings_.localLights.size() - spotLightCount << ",\n"
           << "  \"spotLightCount\": " << spotLightCount << ",\n"
           << "  \"drawCalls\": " << renderer_->drawCallCount() << ",\n"
           << "  \"cpuOpticsP50Ms\": " << percentile(solveTimes, 0.50) << ",\n"
           << "  \"cpuOpticsP95Ms\": " << percentile(solveTimes, 0.95) << ",\n"
           << "  \"cpuFrameP50Ms\": " << percentile(benchmarkCpuFrameTimes_, 0.50) << ",\n"
           << "  \"cpuFrameP95Ms\": " << percentile(benchmarkCpuFrameTimes_, 0.95) << ",\n"
           << "  \"gpuFrameP50Ms\": " << gpuFrameP50 << ",\n"
           << "  \"gpuFrameP95Ms\": " << percentile(benchmarkGpuFrameTimes_, 0.95) << ",\n"
           << "  \"gpuBeamP50Ms\": " << percentile(benchmarkBeamGpuTimes_, 0.50) << ",\n"
           << "  \"gpuBeamP95Ms\": " << percentile(benchmarkBeamGpuTimes_, 0.95) << ",\n"
           << "  \"gpuCausticsP50Ms\": " << percentile(benchmarkCausticsGpuTimes_, 0.50) << ",\n"
           << "  \"gpuCausticsP95Ms\": " << percentile(benchmarkCausticsGpuTimes_, 0.95) << ",\n"
           << "  \"cpuFrameMeasurements\": " << benchmarkCpuFrameTimes_.size() << ",\n"
           << "  \"gpuFrameMeasurements\": " << benchmarkGpuFrameTimes_.size() << ",\n"
           << "  \"gpuBeamMeasurements\": " << benchmarkBeamGpuTimes_.size() << ",\n"
           << "  \"gpuCausticsMeasurements\": " << benchmarkCausticsGpuTimes_.size() << ",\n"
           << "  \"gpuPasses\": {\n";
    std::size_t passIndex = 0U;
    for (const auto& [name, samples] : benchmarkPassGpuTimes_) {
        report << "    " << std::quoted(name) << ": {"
               << "\"p50Ms\": " << percentile(samples, 0.50) << ", "
               << "\"p95Ms\": " << percentile(samples, 0.95) << ", "
               << "\"measurements\": " << samples.size() << "}"
               << (++passIndex < benchmarkPassGpuTimes_.size() ? "," : "")
               << "\n";
    }
    report << "  },\n"
           << "  \"renderMemoryBytes\": " << renderer_->estimatedRenderMemoryBytes() << ",\n"
           << "  \"estimatedOpaqueTrafficBytesPerFrame\": " << opaqueTrafficBytes << ",\n"
           << "  \"estimatedOpaqueTrafficGiBPerSecondAtGpuP50\": "
           << estimatedOpaqueTrafficGiBPerSecond << ",\n"
           << "  \"textureMemoryBytes\": " << loadedTextureMemoryBytes_ << ",\n"
           << "  \"geometryMemoryBytes\": " << geometryMemoryBytes << ",\n"
           << "  \"totalMeasuredMemoryBytes\": "
           << renderer_->estimatedRenderMemoryBytes()
                + loadedTextureMemoryBytes_ + geometryMemoryBytes << ",\n"
           << "  \"solveChecksum\": " << solveChecksum << "\n"
           << "}\n";
    std::cout << "Saved renderer benchmark: " << benchmarkOutputPath_ << '\n';
}

void Application::updatePrismReelFrame() {
    const float t = prismReelFrameCount_ > 1
        ? static_cast<float>(prismReelFrameIndex_)
            / static_cast<float>(prismReelFrameCount_ - 1)
        : 1.0f;
    prismParameters_ = prismOpticalPresetParameters(PrismOpticalPreset::CrownGlass);
    prismOpticalPreset_ = PrismOpticalPreset::CrownGlass;
    rendererSettings_.showPrismOpticalPathDebug = false;
    rendererSettings_.showPrismIncidentBeam = true;

    if (t < 0.20f) {
        prismParameters_.dispersion = 0.0f;
    } else if (t < 0.55f) {
        const float local = std::clamp((t - 0.20f) / 0.35f, 0.0f, 1.0f);
        const float smooth = local * local * (3.0f - 2.0f * local);
        prismParameters_.dispersion = 0.55f * smooth;
    } else if (t < 0.80f) {
        const float local = std::clamp((t - 0.55f) / 0.25f, 0.0f, 1.0f);
        prismParameters_.dispersion = 0.55f;
        prismParameters_.beamAngleDegrees = 2.0f + 10.0f * local;
    } else {
        const float local = std::clamp((t - 0.80f) / 0.20f, 0.0f, 1.0f);
        prismOpticalPreset_ = PrismOpticalPreset::ExaggeratedCover;
        prismParameters_ = prismOpticalPresetParameters(prismOpticalPreset_);
        prismParameters_.beamAngleDegrees = 12.0f + (7.65f - 12.0f) * local;
        rendererSettings_.prismBeamBloomContribution = 0.35f + 0.30f * local;
    }
    updatePrismDemoOptics();
    restorePrismHeroShot();
}
