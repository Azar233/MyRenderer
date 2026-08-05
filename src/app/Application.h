#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

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
    void drawAboutPopup();

    void discoverModels();
    bool loadModel(const std::filesystem::path& path);
    const ModelImporter* findImporter(const std::filesystem::path& path) const;
    std::filesystem::path resolvePath(const std::filesystem::path& path) const;
    std::filesystem::path nextScreenshotPath() const;
    void resetObjectTransform();

    GLFWwindow* window_{nullptr};
    bool guiInitialized_{false};
    bool shutdownComplete_{false};

    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<GpuModel> model_;
    std::vector<std::unique_ptr<ModelImporter>> importers_;
    Camera camera_;
    RendererSettings rendererSettings_;

    std::filesystem::path sourceRoot_;
    std::filesystem::path currentModelPath_;
    std::filesystem::path pendingScreenshotPath_;
    std::vector<std::filesystem::path> availableModels_;
    std::array<char, 1024> modelPathBuffer_{};
    std::string statusMessage_{"Ready"};
    std::string modelWarnings_;
    std::string gpuDescription_;

    glm::vec3 modelCenter_{0.0f};
    glm::vec3 modelRotationDegrees_{0.0f};
    float modelNormalizationScale_{1.0f};
    float modelScale_{1.0f};
    std::size_t loadedMeshCount_{0};
    std::size_t loadedSubmeshCount_{0};
    std::size_t loadedVertexCount_{0};
    std::size_t loadedTriangleCount_{0};
    std::size_t loadedMaterialCount_{0};
    std::size_t loadedTextureCount_{0};
    std::size_t loadedDecodedTextureCount_{0};
    std::size_t loadedFallbackTextureCount_{0};
    std::size_t loadedTextureMemoryBytes_{0};
    std::size_t unsupportedModelCount_{0};

    bool showAbout_{false};
    bool showImGuiDemo_{false};
    bool autoRotate_{false};
    bool vsync_{true};
    double previousFrameTime_{0.0};
};
