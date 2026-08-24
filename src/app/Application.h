#pragma once

#include <array>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

#include "io/ModelImporter.h"
#include "optics/PrismDemo.h"
#include "render/Camera.h"
#include "render/Renderer.h"

struct GLFWwindow;
class GpuModel;
class ModelImporter;
class Renderer;

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run(const std::filesystem::path& initialModel = {});

private:
    void initializeWindow();
    void initializeGui();
    void initializeRenderer();
    void initializeImporters();
    void shutdown();

    void drawMainMenu();
    void drawScenePanel();
    void drawInspectorPanel();
    void drawViewportPanel();
    void drawOrientationGizmo();
    void drawAboutPopup();
    void drawDiagnostics();

    void discoverModels();
    bool loadModel(const std::filesystem::path& path);
    void updateModelLoad();
    void finishModelLoad(const std::filesystem::path& path, ModelImportResult loaded);
    void queueDroppedFiles(int count, const char** paths);
    const ModelImporter* findImporter(const std::filesystem::path& path) const;
    std::filesystem::path resolvePath(const std::filesystem::path& path) const;
    std::filesystem::path nextScreenshotPath() const;
    void resetObjectTransform();
    void activatePrismDemoPreset(bool loadFixture);
    void updatePrismDemoOptics();
    void applyPrismOpticalPreset(PrismOpticalPreset preset);
    void restorePrismHeroShot();
    void writePrismBenchmarkReport();
    void updatePrismReelFrame();

    GLFWwindow* window_{nullptr};
    bool guiInitialized_{false};
    bool shutdownComplete_{false};

    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<GpuModel> model_;
    std::unique_ptr<GpuModel> groundModel_;
    std::vector<std::unique_ptr<ModelImporter>> importers_;
    Camera camera_;
    RendererSettings rendererSettings_;

    std::filesystem::path sourceRoot_;
    std::filesystem::path currentModelPath_;
    std::filesystem::path pendingScreenshotPath_;
    int pendingScreenshotWarmupFrames_{0};
    std::filesystem::path benchmarkOutputPath_;
    std::filesystem::path prismReelFramesDirectory_;
    std::vector<std::filesystem::path> availableModels_;
    std::array<char, 1024> modelPathBuffer_{};
    std::string statusMessage_{"Ready"};
    std::string gpuDescription_;
    std::vector<ModelDiagnostic> modelDiagnostics_;

    struct PendingModelImport {
        std::filesystem::path path;
        std::future<ModelImportResult> future;
        std::chrono::steady_clock::time_point startedAt;
        std::uintmax_t fileSize{0};
    };
    std::optional<PendingModelImport> pendingModelImport_;
    std::optional<std::filesystem::path> droppedModelPath_;

    glm::vec3 modelCenter_{0.0f};
    glm::vec3 modelPosition_{0.0f};
    glm::vec3 modelRotationDegrees_{0.0f};
    float modelNormalizationScale_{1.0f};
    float modelScale_{1.0f};
    glm::vec3 groundColor_{0.58f, 0.60f, 0.64f};
    float groundOffset_{-0.72f};
    std::size_t loadedMeshCount_{0};
    std::size_t loadedSubmeshCount_{0};
    std::size_t loadedTransparentSubmeshCount_{0};
    std::size_t loadedVertexCount_{0};
    std::size_t loadedTriangleCount_{0};
    std::size_t loadedMaterialCount_{0};
    std::size_t loadedTextureCount_{0};
    std::size_t loadedDecodedTextureCount_{0};
    std::size_t loadedFallbackTextureCount_{0};
    std::size_t loadedTextureMemoryBytes_{0};
    std::size_t unsupportedModelCount_{0};
    double cpuFrameTimeMilliseconds_{0.0};
    double lastCpuImportMilliseconds_{0.0};
    double lastGpuUploadMilliseconds_{0.0};
    double lastLoadTotalMilliseconds_{0.0};
    std::vector<double> benchmarkCpuFrameTimes_;
    std::vector<double> benchmarkGpuFrameTimes_;
    std::vector<double> benchmarkBeamGpuTimes_;
    std::size_t lastBenchmarkGpuFrameSerial_{0};
    std::size_t lastBenchmarkBeamSerial_{0};
    int renderWidthOverride_{0};
    int renderHeightOverride_{0};
    int benchmarkWarmupFrames_{60};
    int benchmarkMeasurementFrames_{180};
    int benchmarkRenderedFrames_{0};
    int prismReelFrameIndex_{0};
    int prismReelFrameCount_{360};
    int prismReelWarmupFrames_{8};

    bool showAbout_{false};
    bool showImGuiDemo_{false};
    bool autoRotate_{false};
    bool showGroundPlane_{true};
    bool showComparisonObject_{false};
    bool vsync_{true};
    bool lastLoadFailed_{false};
    bool prismDemoEnabled_{false};
    bool prismCameraLocked_{true};
    bool prismModelVisible_{true};
    bool benchmarkMode_{false};
    bool prismReelMode_{false};
    PrismOpticalPreset prismOpticalPreset_{PrismOpticalPreset::CrownGlass};
    PrismDemoParameters prismParameters_{};
    double previousFrameTime_{0.0};
};
