#include "app/Application.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "app/AppIcon.h"
#include "app/FileDialog.h"
#include "io/AssimpImporter.h"
#include "io/ModelImporter.h"
#include "io/ObjLoader.h"
#include "optics/PrismDemo.h"
#include "optics/PrismOptics.h"
#include "render/GpuModel.h"
#include "render/OpenGlDebug.h"
#include "render/Renderer.h"

namespace {

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
    : sourceRoot_(std::filesystem::path(MYRENDERER_SOURCE_DIR)) {
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
            std::clamp(std::atoi(value), 0, 4)
        );
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

    std::filesystem::path modelToLoad = initialModel;
    if (modelToLoad.empty()) {
        const auto defaultModel = prismDemoEnabled_
            ? sourceRoot_ / "assets" / "models" / "prism_spectrum.gltf"
            : (glassCausticsDemoEnabled_
                ? sourceRoot_ / "assets" / "models" / "glass_volume_sphere.gltf"
                : (instanceStressDemoEnabled_
                    ? sourceRoot_ / "assets" / "models" / "sphere.obj"
                    : sourceRoot_ / "assets" / "models" / "cube.obj"));
        modelToLoad = std::filesystem::exists(defaultModel)
            ? defaultModel
            : (availableModels_.empty() ? std::filesystem::path{} : availableModels_.front());
    }
    if (!modelToLoad.empty()) {
        loadModel(modelToLoad);
    } else {
        statusMessage_ = "No supported model was found in assets/models";
    }

    if (const char* screenshotPath = std::getenv("MYRENDERER_SCREENSHOT")) {
        pendingScreenshotPath_ = std::filesystem::absolute(screenshotPath).lexically_normal();
        // Let newly uploaded materials and driver-specialized shader state settle
        // before recording an automated visual baseline.
        pendingScreenshotWarmupFrames_ = 2;
    }
    const char* recoveryModelValue = std::getenv("MYRENDERER_RECOVERY_TEST");
    const std::filesystem::path recoveryModel = recoveryModelValue == nullptr
        ? std::filesystem::path{}
        : std::filesystem::path(recoveryModelValue);
    bool recoveryScheduled = recoveryModel.empty();

    int smokeTestFrames = std::getenv("MYRENDERER_SMOKE_TEST") == nullptr ? -1 : 5;
    previousFrameTime_ = glfwGetTime();
    while (!glfwWindowShouldClose(window_)) {
        const double cpuFrameStart = glfwGetTime();
        glfwPollEvents();
        if (droppedModelPath_.has_value() && !pendingModelImport_.has_value()) {
            const std::filesystem::path dropped = std::move(*droppedModelPath_);
            droppedModelPath_.reset();
            loadModel(dropped);
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
        if (prismReelMode_ && model_ != nullptr && !pendingModelImport_.has_value()
            && prismReelWarmupFrames_ == 0) {
            updatePrismReelFrame();
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        drawMainMenu();
        ImGui::DockSpaceOverViewport();
        drawScenePanel();
        drawInspectorPanel();
        drawViewportPanel();
        drawAboutPopup();
        if (showImGuiDemo_) {
            ImGui::ShowDemoWindow(&showImGuiDemo_);
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
        if (smokeTestFrames > 0 && !pendingModelImport_.has_value() && --smokeTestFrames == 0) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
    }

    const bool recoveryPassed = recoveryModel.empty()
        || (recoveryScheduled && lastLoadFailed_ && model_ != nullptr);
    shutdown();
    return recoveryPassed ? 0 : 2;
}

void Application::initializeWindow() {
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
    glfwSetWindowSizeLimits(window_, 960, 600, GLFW_DONT_CARE, GLFW_DONT_CARE);
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
    io.IniFilename = "MyRenderer.ini";

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.085f, 0.11f, 1.0f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.055f, 0.065f, 0.09f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.12f, 0.17f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.17f, 0.23f, 0.36f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.23f, 0.32f, 0.50f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.16f, 0.22f, 0.35f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.34f, 0.54f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.42f, 0.67f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.42f, 0.67f, 1.0f, 1.0f);

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
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open model...", "Ctrl+O", false, !pendingModelImport_.has_value())) {
            std::string dialogError;
            const auto selected = openModelFileDialog(dialogError);
            if (selected.has_value()) {
                loadModel(*selected);
            } else if (!dialogError.empty()) {
                statusMessage_ = "Open failed: " + dialogError;
            }
        }
        if (ImGui::BeginMenu("Open bundled model")) {
            for (const auto& path : availableModels_) {
                if (ImGui::MenuItem(path.filename().string().c_str())) {
                    loadModel(path);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(
            "Reload current",
            "Ctrl+R",
            false,
            !currentModelPath_.empty() && !pendingModelImport_.has_value()
        )) {
            loadModel(currentModelPath_);
        }
        if (ImGui::MenuItem("Save viewport PNG", nullptr, false, model_ != nullptr)) {
            pendingScreenshotPath_ = nextScreenshotPath();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Esc")) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Reset camera", "F")) {
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
        ImGui::MenuItem("Wireframe", nullptr, &rendererSettings_.wireframe);
        ImGui::MenuItem("Back-face culling", nullptr, &rendererSettings_.cullBackFaces);
        ImGui::Separator();
        ImGui::MenuItem("Ground grid", nullptr, &rendererSettings_.showGrid);
        ImGui::MenuItem("Ground plane", nullptr, &showGroundPlane_);
        ImGui::MenuItem("Comparison object", nullptr, &showComparisonObject_);
        ImGui::MenuItem("XYZ axes", nullptr, &rendererSettings_.showAxes);
        ImGui::MenuItem("Auto rotate", nullptr, &autoRotate_);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("Dear ImGui demo", nullptr, &showImGuiDemo_);
        if (ImGui::MenuItem("About MyRenderer")) {
            showAbout_ = true;
        }
        ImGui::EndMenu();
    }

    const std::string fps = std::to_string(static_cast<int>(ImGui::GetIO().Framerate)) + " FPS";
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - ImGui::CalcTextSize(fps.c_str()).x - 16.0f));
    ImGui::TextDisabled("%s", fps.c_str());
    ImGui::EndMainMenuBar();
}

void Application::drawScenePanel() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float menuHeight = ImGui::GetFrameHeight();
    const ImVec2 contentPosition(viewport->Pos.x, viewport->Pos.y + menuHeight);
    const ImVec2 contentSize(viewport->Size.x, viewport->Size.y - menuHeight);
    ImGui::SetNextWindowPos(contentPosition, ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(285.0f, contentSize.y), ImGuiCond_Once);
    if (!ImGui::Begin("Scene")) {
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Scene objects");
    if (model_) {
        const std::string currentMeshLabel = currentModelPath_.filename().string() + "##CurrentMesh";
        ImGui::TreeNodeEx(currentMeshLabel.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::TreePop();
        ImGui::TextDisabled("Meshes: %zu", loadedMeshCount_);
        ImGui::TextDisabled("Submeshes: %zu", loadedSubmeshCount_);
        ImGui::TextDisabled("Vertices: %zu", loadedVertexCount_);
        ImGui::TextDisabled("Triangles: %zu", loadedTriangleCount_);
        if (showGroundPlane_) {
            ImGui::TreeNodeEx("Ground receiver", ImGuiTreeNodeFlags_Leaf);
            ImGui::TreePop();
        }
        if (showComparisonObject_) {
            ImGui::TreeNodeEx("Comparison instance", ImGuiTreeNodeFlags_Leaf);
            ImGui::TreePop();
        }
        if (lightStressDemoEnabled_) {
            ImGui::TreeNodeEx("Stress instances x100", ImGuiTreeNodeFlags_Leaf);
            ImGui::TreePop();
            ImGui::TextDisabled("Local lights: %zu", rendererSettings_.localLights.size());
        }
        if (instanceStressDemoEnabled_) {
            ImGui::TreeNodeEx("Instance stress x2500", ImGuiTreeNodeFlags_Leaf);
            ImGui::TreePop();
            ImGui::TextDisabled(
                "Visible / culled: %zu / %zu",
                renderer_->visibleInstanceCount(),
                renderer_->culledInstanceCount()
            );
        }
        if (rendererSettings_.showPrismIncidentBeam) {
            ImGui::TreeNodeEx("Incident beam (Prism-0 placeholder)", ImGuiTreeNodeFlags_Leaf);
            ImGui::TreePop();
        }
    } else {
        ImGui::TextDisabled("No model loaded");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Model assets");
    for (const auto& path : availableModels_) {
        const bool selected = !currentModelPath_.empty() && path.filename() == currentModelPath_.filename();
        const std::string assetLabel = path.filename().string() + "##Asset_" + path.string();
        if (ImGui::Selectable(assetLabel.c_str(), selected)) {
            loadModel(path);
        }
    }
    if (availableModels_.empty()) {
        ImGui::TextDisabled("No supported model files found");
    }
    if (unsupportedModelCount_ > 0) {
        ImGui::Spacing();
        ImGui::TextDisabled("%zu model(s) await a format importer", unsupportedModelCount_);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Open path");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##ModelPath", modelPathBuffer_.data(), modelPathBuffer_.size());
    const bool loadInProgress = pendingModelImport_.has_value();
    ImGui::BeginDisabled(loadInProgress);
    if (ImGui::Button("Browse...", ImVec2(-1.0f, 0.0f))) {
        std::string dialogError;
        const auto selected = openModelFileDialog(dialogError);
        if (selected.has_value()) {
            const std::string selectedPath = selected->string();
            std::snprintf(modelPathBuffer_.data(), modelPathBuffer_.size(), "%s", selectedPath.c_str());
            loadModel(*selected);
        } else if (!dialogError.empty()) {
            statusMessage_ = "Open failed: " + dialogError;
        }
    }
    if (ImGui::Button("Load entered path", ImVec2(-1.0f, 0.0f))) {
        loadModel(std::filesystem::u8path(modelPathBuffer_.data()));
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("You can also drop OBJ, DAE, glTF or GLB files onto the window.");

    ImGui::Spacing();
    ImGui::SeparatorText("Status");
    ImGui::TextWrapped("%s", statusMessage_.c_str());
    if (pendingModelImport_.has_value()) {
        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - pendingModelImport_->startedAt
        ).count();
        const float activity = static_cast<float>(std::fmod(elapsed * 0.35, 1.0));
        ImGui::ProgressBar(activity, ImVec2(-1.0f, 0.0f), "Importing on CPU...");
        ImGui::TextDisabled(
            "%.2f MiB | %.1f s elapsed | current scene stays active",
            static_cast<double>(pendingModelImport_->fileSize) / (1024.0 * 1024.0),
            elapsed
        );
    }
    drawDiagnostics();
    ImGui::End();
}

void Application::drawInspectorPanel() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float menuHeight = ImGui::GetFrameHeight();
    const ImVec2 contentPosition(viewport->Pos.x, viewport->Pos.y + menuHeight);
    const ImVec2 contentSize(viewport->Size.x, viewport->Size.y - menuHeight);
    ImGui::SetNextWindowPos(
        ImVec2(contentPosition.x + contentSize.x - 335.0f, contentPosition.y),
        ImGuiCond_Once
    );
    ImGui::SetNextWindowSize(ImVec2(335.0f, contentSize.y), ImGuiCond_Once);
    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("InspectorTabs")) {
        if (ImGui::BeginTabItem("Object")) {
            ImGui::SeparatorText("Transform");
            ImGui::DragFloat3("Position", &modelPosition_.x, 0.01f, 0.0f, 0.0f, "%.2f");
            ImGui::DragFloat3("Rotation", &modelRotationDegrees_.x, 0.25f, -360.0f, 360.0f, "%.1f deg");
            ImGui::SliderFloat("Scale", &modelScale_, 0.1f, 4.0f, "%.2f");
            ImGui::Checkbox("Auto rotate", &autoRotate_);
            if (ImGui::Button("Reset transform", ImVec2(-1.0f, 0.0f))) {
                resetObjectTransform();
                camera_.reset(modelPosition_);
            }

            ImGui::SeparatorText("Stage");
            ImGui::Checkbox("Ground receiver", &showGroundPlane_);
            ImGui::ColorEdit3("Ground color", &groundColor_.x);
            ImGui::DragFloat("Ground offset", &groundOffset_, 0.01f, -3.0f, 0.0f, "%.2f");
            ImGui::Checkbox("Comparison object", &showComparisonObject_);

            ImGui::SeparatorText("Material");
            ImGui::ColorEdit3("Base color tint", &rendererSettings_.baseColor.x);
            ImGui::SliderFloat("Ambient", &rendererSettings_.ambientStrength, 0.0f, 1.0f);
            ImGui::SliderFloat("Diffuse", &rendererSettings_.diffuseStrength, 0.0f, 2.0f);
            ImGui::SliderFloat("Specular", &rendererSettings_.specularStrength, 0.0f, 2.0f);
            ImGui::SliderFloat("Shininess", &rendererSettings_.shininess, 1.0f, 256.0f, "%.0f", ImGuiSliderFlags_Logarithmic);

            ImGui::SeparatorText("Asset statistics");
            if (model_) {
                ImGui::Text("Meshes: %zu", loadedMeshCount_);
                ImGui::Text("Submeshes / draw calls: %zu", loadedSubmeshCount_);
                ImGui::Text("Transparent submeshes: %zu", loadedTransparentSubmeshCount_);
                ImGui::Text("Materials: %zu", loadedMaterialCount_);
                ImGui::Text("Textures: %zu", loadedTextureCount_);
                ImGui::Text("Decoded textures: %zu", loadedDecodedTextureCount_);
                ImGui::Text("Fallback textures: %zu", loadedFallbackTextureCount_);
                ImGui::Text(
                    "Estimated texture memory: %.2f MiB",
                    static_cast<double>(loadedTextureMemoryBytes_) / (1024.0 * 1024.0)
                );
            } else {
                ImGui::TextDisabled("No model loaded");
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Renderer")) {
            ImGui::SeparatorText("PBR & environment");
            int renderPath = static_cast<int>(rendererSettings_.renderPath);
            const char* renderPaths[] = {"Forward", "Deferred (hybrid)"};
            if (ImGui::Combo("Opaque render path", &renderPath, renderPaths, 2)) {
                rendererSettings_.renderPath = static_cast<RenderPath>(renderPath);
            }
            ImGui::BeginDisabled(rendererSettings_.renderPath != RenderPath::Deferred);
            int gBufferDebug = static_cast<int>(rendererSettings_.gBufferDebugView);
            const char* gBufferDebugViews[] = {
                "Final lighting",
                "Albedo",
                "Encoded normal",
                "Metallic / Roughness",
                "Depth"
            };
            if (ImGui::Combo("G-buffer debug", &gBufferDebug, gBufferDebugViews, 5)) {
                rendererSettings_.gBufferDebugView = static_cast<GBufferDebugView>(gBufferDebug);
            }
            ImGui::EndDisabled();
            ImGui::SeparatorText("Local light stress");
            bool stressEnabled = lightStressDemoEnabled_;
            if (ImGui::Checkbox("Enable stress scene", &stressEnabled)) {
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
            if (ImGui::Combo("Local light tier", &localLightTierIndex_, lightTiers, 3)) {
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
            ImGui::SeparatorText("Instance submission stress");
            bool instanceStressEnabled = instanceStressDemoEnabled_;
            if (ImGui::Checkbox("Enable 2,500-instance scene", &instanceStressEnabled)) {
                instanceStressDemoEnabled_ = instanceStressEnabled;
                if (instanceStressDemoEnabled_) {
                    activateInstanceStressPreset(true);
                } else {
                    rendererSettings_.instanceOptimizationEnabled = false;
                    statusMessage_ = "Instance stress scene disabled";
                }
            }
            ImGui::BeginDisabled(!instanceStressDemoEnabled_);
            ImGui::Checkbox(
                "GPU instancing / batching",
                &rendererSettings_.instanceOptimizationEnabled
            );
            ImGui::BeginDisabled(!rendererSettings_.instanceOptimizationEnabled);
            ImGui::Checkbox("CPU frustum culling", &rendererSettings_.frustumCullingEnabled);
            ImGui::Checkbox("Projected-size LOD", &rendererSettings_.lodSelectionEnabled);
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
            ImGui::Checkbox("Metallic-roughness PBR", &rendererSettings_.pbrEnabled);
            ImGui::Checkbox("Image-based lighting", &rendererSettings_.iblEnabled);
            ImGui::Checkbox("Skybox", &rendererSettings_.skyboxEnabled);
            ImGui::Checkbox("Shadow mapping", &rendererSettings_.shadowsEnabled);
            ImGui::Checkbox(
                "Colored transmission shadows",
                &rendererSettings_.coloredTransmissionShadowsEnabled
            );
            ImGui::Checkbox("HDR caustics", &rendererSettings_.causticsEnabled);
            if (rendererSettings_.causticsEnabled) {
                int causticsMode = static_cast<int>(rendererSettings_.causticsMode);
                const char* causticsModes[] = {"Projector / decal", "Light-space RGB"};
                if (ImGui::Combo("Caustics mode", &causticsMode, causticsModes, 2)) {
                    rendererSettings_.causticsMode = static_cast<CausticsMode>(causticsMode);
                }
                ImGui::SliderFloat(
                    "Caustics strength",
                    &rendererSettings_.causticsStrength,
                    0.0f,
                    8.0f,
                    "%.2f"
                );
                ImGui::SliderFloat(
                    "Caustics scale",
                    &rendererSettings_.causticsScale,
                    0.1f,
                    3.0f,
                    "%.2f"
                );
                ImGui::SliderFloat3(
                    "Caustics direction",
                    &rendererSettings_.causticsDirection.x,
                    -1.5f,
                    1.5f,
                    "%.2f"
                );
                ImGui::SliderFloat(
                    "Caustics sharpness",
                    &rendererSettings_.causticsSharpness,
                    0.0f,
                    1.0f,
                    "%.2f"
                );
                ImGui::BeginDisabled(
                    rendererSettings_.causticsMode != CausticsMode::Projector
                );
                ImGui::Checkbox("Animate caustics", &rendererSettings_.causticsAnimated);
                ImGui::EndDisabled();
                ImGui::TextDisabled(
                    "Caustics map: 1024 x 1024 | GPU %.3f ms",
                    renderer_->hasCausticsGpuTime()
                        ? renderer_->causticsGpuTimeMilliseconds()
                        : 0.0
                );
            }
            ImGui::SeparatorText("Glass feature toggles");
            ImGui::Checkbox("Glass transmission", &rendererSettings_.transmissionEnabled);
            ImGui::Checkbox("Dispersion", &rendererSettings_.dispersionEnabled);
            ImGui::Checkbox("Geometric glass thickness", &rendererSettings_.geometricThicknessEnabled);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Uses a back-face depth pass for closed glass meshes; "
                    "falls back to the material thickness/texture when no exit surface is found."
                );
            }
            ImGui::Checkbox(
                "Two-interface refraction",
                &rendererSettings_.twoInterfaceRefractionEnabled
            );
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Traces the curved exit surface and applies Snell refraction again "
                    "when light leaves the glass."
                );
            }
            ImGui::SliderFloat(
                "Refraction scale",
                &rendererSettings_.refractionScale,
                0.0f,
                0.8f,
                "%.3f"
            );
            ImGui::SliderInt("Refraction steps", &rendererSettings_.refractionSteps, 4, 32);
            ImGui::SliderFloat(
                "Volume thickness scale",
                &rendererSettings_.volumeThicknessScale,
                0.0f,
                4.0f,
                "%.2f"
            );
            ImGui::Checkbox(
                "Volume glass material override",
                &rendererSettings_.volumeGlassOverrideEnabled
            );
            if (rendererSettings_.volumeGlassOverrideEnabled) {
                int presetIndex = static_cast<int>(volumeGlassPreset_);
                const char* presetNames[] = {"Clear", "Olive", "Amber", "Crystal"};
                if (ImGui::Combo("Glass preset", &presetIndex, presetNames, 4)) {
                    applyVolumeGlassPreset(static_cast<VolumeGlassPreset>(presetIndex));
                }
                ImGui::SliderFloat(
                    "Glass transmission",
                    &rendererSettings_.volumeGlassTransmission,
                    0.0f,
                    1.0f,
                    "%.2f"
                );
                ImGui::SliderFloat(
                    "Glass roughness",
                    &rendererSettings_.volumeGlassRoughness,
                    0.04f,
                    1.0f,
                    "%.2f"
                );
                ImGui::ColorEdit3(
                    "Attenuation color",
                    &rendererSettings_.volumeGlassAttenuationColor.x
                );
                ImGui::SliderFloat(
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
                ImGui::SliderFloat(
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
            if (prismDemoEnabled_) {
                ImGui::SeparatorText("Prism spectrum");
                int presetIndex = static_cast<int>(prismOpticalPreset_);
                const char* presetNames[] = {
                    prismOpticalPresetName(PrismOpticalPreset::CrownGlass),
                    prismOpticalPresetName(PrismOpticalPreset::WaterLike),
                    prismOpticalPresetName(PrismOpticalPreset::DiamondLike),
                    prismOpticalPresetName(PrismOpticalPreset::ExaggeratedCover)
                };
                if (ImGui::Combo("Optical preset", &presetIndex, presetNames, 4)) {
                    applyPrismOpticalPreset(static_cast<PrismOpticalPreset>(presetIndex));
                }

                bool opticsChanged = false;
                opticsChanged |= ImGui::SliderFloat(
                    "Beam direction",
                    &prismParameters_.beamAngleDegrees,
                    -20.0f,
                    20.0f,
                    "%.2f deg"
                );
                opticsChanged |= ImGui::SliderFloat(
                    "Central IOR",
                    &prismParameters_.centralIndexOfRefraction,
                    1.0f,
                    2.6f,
                    "%.3f"
                );
                opticsChanged |= ImGui::SliderFloat(
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
                if (ImGui::Combo("Spectral samples", &sampleTierIndex, sampleLabels, 4)) {
                    prismParameters_.spectralSampleCount = sampleTiers[static_cast<std::size_t>(sampleTierIndex)];
                    opticsChanged = true;
                }
                int spectrumMode = static_cast<int>(prismParameters_.spectrumMode);
                const char* spectrumModes[] = {"Continuous", "Seven-band"};
                if (ImGui::Combo("Spectrum mode", &spectrumMode, spectrumModes, 2)) {
                    prismParameters_.spectrumMode = static_cast<PrismSpectrumMode>(spectrumMode);
                    opticsChanged = true;
                }
                opticsChanged |= ImGui::SliderFloat(
                    "White point",
                    &prismParameters_.whitePointKelvin,
                    2000.0f,
                    12000.0f,
                    "%.0f K"
                );
                if (opticsChanged) {
                    updatePrismDemoOptics();
                }

                ImGui::Checkbox(
                    "Spectral beam ribbons",
                    &rendererSettings_.showPrismIncidentBeam
                );
                ImGui::SliderFloat(
                    "Beam width",
                    &rendererSettings_.prismBeamWidth,
                    0.005f,
                    0.16f,
                    "%.3f"
                );
                ImGui::SliderFloat(
                    "Beam intensity",
                    &rendererSettings_.prismBeamIntensity,
                    0.0f,
                    16.0f,
                    "%.2f"
                );
                ImGui::SliderFloat(
                    "Edge softness",
                    &rendererSettings_.prismBeamEdgeSoftness,
                    0.01f,
                    1.0f,
                    "%.2f"
                );
                ImGui::SliderFloat(
                    "Bloom contribution",
                    &rendererSettings_.prismBeamBloomContribution,
                    0.0f,
                    2.0f,
                    "%.2f"
                );
                ImGui::Checkbox(
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
                if (ImGui::TreeNode("Optical path details")) {
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
                ImGui::Checkbox("Lock hero camera", &prismCameraLocked_);
                ImGui::Checkbox("Auto rotate prism", &autoRotate_);
                if (ImGui::Button("Restore prism hero shot", ImVec2(-1.0f, 0.0f))) {
                    restorePrismHeroShot();
                }
            }
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
            if (ImGui::Combo("Glass debug view", &glassDebugView, glassDebugViews, 13)) {
                rendererSettings_.glassDebugView = static_cast<GlassDebugView>(glassDebugView);
            }
            ImGui::SliderFloat("Environment", &rendererSettings_.environmentIntensity, 0.0f, 2.0f, "%.2f");
            ImGui::TextDisabled("Shadow map: %d x %d", renderer_->shadowResolution(), renderer_->shadowResolution());

            ImGui::SeparatorText("Post processing");
            ImGui::Checkbox("ACES tone mapping", &rendererSettings_.toneMapping);
            ImGui::Checkbox("Bloom", &rendererSettings_.bloom);
            ImGui::SliderFloat("Exposure", &rendererSettings_.exposure, 0.1f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::BeginDisabled(!rendererSettings_.bloom);
            ImGui::SliderFloat("Bloom threshold", &rendererSettings_.bloomThreshold, 0.1f, 4.0f, "%.2f");
            ImGui::SliderFloat("Bloom intensity", &rendererSettings_.bloomIntensity, 0.0f, 1.0f, "%.2f");
            ImGui::EndDisabled();

            ImGui::SeparatorText("Rasterization");
            ImGui::Checkbox("Wireframe", &rendererSettings_.wireframe);
            ImGui::Checkbox("Back-face culling", &rendererSettings_.cullBackFaces);
            ImGui::Checkbox("Normal mapping", &rendererSettings_.normalMapping);
            ImGui::Checkbox("Ground grid", &rendererSettings_.showGrid);
            ImGui::Checkbox("XYZ axes + gizmo", &rendererSettings_.showAxes);
            ImGui::ColorEdit3("Background", &rendererSettings_.backgroundColor.x);
            const char* msaaOptions[] = {"1x", "4x"};
            int msaaSelection = rendererSettings_.msaaSamples > 1 ? 1 : 0;
            if (ImGui::Combo("MSAA", &msaaSelection, msaaOptions, 2)) {
                rendererSettings_.msaaSamples = msaaSelection == 0 ? 1 : 4;
            }
            ImGui::TextDisabled("Active samples: %dx", renderer_->activeMsaaSamples());

            ImGui::SeparatorText("Directional light");
            ImGui::DragFloat3("Direction", &rendererSettings_.lightDirection.x, 0.01f, -1.0f, 1.0f, "%.2f");

            ImGui::SeparatorText("Camera");
            float fieldOfView = camera_.fieldOfView();
            if (ImGui::SliderFloat("Field of view", &fieldOfView, 15.0f, 90.0f, "%.0f deg")) {
                camera_.setFieldOfView(fieldOfView);
            }
            if (ImGui::Button("Frame model", ImVec2(-1.0f, 0.0f))) {
                camera_.reset(modelPosition_);
            }

            ImGui::SeparatorText("Runtime");
            if (ImGui::Checkbox("VSync", &vsync_)) {
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
            for (const auto& passName : renderer_->activePassNames()) {
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
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void Application::drawViewportPanel() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float menuHeight = ImGui::GetFrameHeight();
    const ImVec2 contentPosition(viewport->Pos.x, viewport->Pos.y + menuHeight);
    const ImVec2 contentSize(viewport->Size.x, viewport->Size.y - menuHeight);
    ImGui::SetNextWindowPos(ImVec2(contentPosition.x + 285.0f, contentPosition.y), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(std::max(contentSize.x - 620.0f, 320.0f), contentSize.y), ImGuiCond_Once);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin("Viewport");
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return;
    }

    ImGui::SetCursorPos(ImVec2(10.0f, 30.0f));
    if (ImGui::SmallButton("Frame")) {
        camera_.reset(modelPosition_);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Save PNG")) {
        pendingScreenshotPath_ = nextScreenshotPath();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &rendererSettings_.showGrid);
    ImGui::SameLine();
    ImGui::Checkbox("Ground", &showGroundPlane_);
    ImGui::SameLine();
    ImGui::Checkbox("Axes", &rendererSettings_.showAxes);
    ImGui::SameLine();
    ImGui::TextDisabled("RMB orbit | MMB pan | Wheel zoom");
    ImGui::SetCursorPosY(55.0f);

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
    glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), modelPosition_);
    modelMatrix = glm::rotate(modelMatrix, glm::radians(modelRotationDegrees_.z), glm::vec3(0.0f, 0.0f, 1.0f));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(modelRotationDegrees_.y), glm::vec3(0.0f, 1.0f, 0.0f));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(modelRotationDegrees_.x), glm::vec3(1.0f, 0.0f, 0.0f));
    modelMatrix = glm::scale(modelMatrix, glm::vec3(modelScale_));
    modelMatrix *= normalization;

    std::vector<RenderItem> renderItems;
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
    if (model_ != nullptr && !lightStressDemoEnabled_ && !instanceStressDemoEnabled_
        && (!prismDemoEnabled_ || prismModelVisible_)) {
        renderItems.push_back(RenderItem{
            model_.get(),
            modelMatrix,
            rendererSettings_.baseColor,
            true,
            true
        });
        if (showComparisonObject_) {
            glm::mat4 comparisonMatrix = glm::translate(
                glm::mat4(1.0f),
                modelPosition_ + (glassVolumeDemoEnabled_
                    ? glm::vec3(0.92f, 0.0f, 0.0f)
                    : glm::vec3(0.95f, 0.0f, 0.35f))
            );
            if (!glassVolumeDemoEnabled_) {
                comparisonMatrix = glm::rotate(
                    comparisonMatrix,
                    glm::radians(-28.0f),
                    glm::vec3(0.0f, 1.0f, 0.0f)
                );
            }
            comparisonMatrix = glm::scale(
                comparisonMatrix,
                glm::vec3(modelScale_ * (glassVolumeDemoEnabled_ ? 0.88f : 0.50f))
            );
            comparisonMatrix *= normalization;
            renderItems.push_back(RenderItem{
                model_.get(),
                comparisonMatrix,
                glassVolumeDemoEnabled_
                    ? glm::vec3(1.0f)
                    : glm::vec3(0.72f, 0.82f, 1.0f),
                true,
                true
            });
        }
    }
    if (glassVolumeDemoEnabled_ && !glassCausticsDemoEnabled_
        && glassBackdropModel_ != nullptr) {
        renderItems.push_back(RenderItem{
            glassBackdropModel_.get(),
            glm::mat4(1.0f),
            glm::vec3(1.0f),
            true,
            false
        });
    }
    if (showGroundPlane_ && groundModel_ != nullptr) {
        const glm::mat4 groundMatrix = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(modelPosition_.x, modelPosition_.y + groundOffset_ * modelScale_, modelPosition_.z)
        );
        renderItems.push_back(RenderItem{
            groundModel_.get(),
            groundMatrix,
            groundColor_,
            true,
            false
        });
    }
    rendererSettings_.causticsReceiverPlaneY =
        modelPosition_.y + groundOffset_ * modelScale_ + 0.002f;
    rendererSettings_.causticsAnimationPhase = rendererSettings_.causticsAnimated
        ? static_cast<float>(std::fmod(glfwGetTime() * 0.16, 1.0))
        : 0.0f;
    renderer_->render(renderItems, camera_, rendererSettings_, width, height);

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

    if (!pendingScreenshotPath_.empty() && model_ != nullptr && !pendingModelImport_.has_value()
        && pendingScreenshotWarmupFrames_ > 0) {
        --pendingScreenshotWarmupFrames_;
    } else if (!pendingScreenshotPath_.empty() && model_ != nullptr
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
        AxisGuide{{1.0f, 0.0f, 0.0f}, "X", IM_COL32(255, 70, 70, 255)},
        AxisGuide{{0.0f, 1.0f, 0.0f}, "Y", IM_COL32(70, 235, 95, 255)},
        AxisGuide{{0.0f, 0.0f, 1.0f}, "Z", IM_COL32(70, 125, 255, 255)}
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
    constexpr float radius = 34.0f;
    drawList->AddCircleFilled(center, 47.0f, IM_COL32(10, 13, 20, 185), 32);
    drawList->AddCircle(center, 47.0f, IM_COL32(115, 125, 150, 145), 32, 1.0f);
    for (const AxisGuide& axis : axes) {
        const ImVec2 endpoint(
            center.x + axis.cameraDirection.x * radius,
            center.y - axis.cameraDirection.y * radius
        );
        drawList->AddLine(center, endpoint, axis.color, 3.0f);
        drawList->AddCircleFilled(endpoint, 4.5f, axis.color, 12);
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

    if (!ImGui::TreeNodeEx("Import diagnostics", ImGuiTreeNodeFlags_DefaultOpen)) {
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
        ImGui::OpenPopup("About MyRenderer");
        showAbout_ = false;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("About MyRenderer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("MyRenderer 0.1.0");
        ImGui::Separator();
        ImGui::Text("C++17 / OpenGL 3.3 / GPU rasterization");
        ImGui::TextWrapped("A compact GPU renderer with a format-independent model pipeline.");
        if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Application::discoverModels() {
    availableModels_.clear();
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
}

bool Application::loadModel(const std::filesystem::path& path) {
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
            fileSizeError ? 0U : fileSize
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
        finishModelLoad(pending.path, std::move(loaded));
    } catch (const std::exception& error) {
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

void Application::finishModelLoad(const std::filesystem::path& path, ModelImportResult loaded) {
    const auto gpuUploadStartedAt = std::chrono::steady_clock::now();
    std::vector<TextureUploadWarning> textureWarnings;
    auto newModel = std::make_unique<GpuModel>(
        loaded.model,
        renderer_->textureCache(),
        textureWarnings
    );
    const glm::vec3 extent = loaded.model.boundsMax - loaded.model.boundsMin;
    const float maximumExtent = std::max({extent.x, extent.y, extent.z});
    if (!std::isfinite(maximumExtent) || maximumExtent <= 1e-8f) {
        throw std::runtime_error("Model bounds are empty or degenerate");
    }

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
    modelCenter_ = 0.5f * (loaded.model.boundsMin + loaded.model.boundsMax);
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
            droppedModelPath_ = candidate;
            statusMessage_ = "Dropped " + candidate.filename().string() + "; queued for loading.";
            return;
        }
    }
    const std::filesystem::path candidate = std::filesystem::u8path(paths[0]);
    droppedModelPath_ = candidate;
    statusMessage_ = "Dropped file is not a supported model: " + candidate.filename().string();
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
