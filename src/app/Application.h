#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

#include "render/Camera.h"

struct GLFWwindow;
class Mesh;
class RenderTarget;
class Shader;

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run(const std::filesystem::path& initialModel = {});

private:
    struct RenderSettings {
        glm::vec3 backgroundColor{0.055f, 0.065f, 0.085f};
        glm::vec3 baseColor{0.66f, 0.72f, 0.88f};
        glm::vec3 lightDirection{-0.45f, -0.8f, -0.35f};
        float ambientStrength{0.18f};
        float diffuseStrength{0.92f};
        float specularStrength{0.28f};
        float shininess{48.0f};
        bool wireframe{false};
        bool cullBackFaces{false};
        bool autoRotate{false};
        bool vsync{true};
    };

    void initializeWindow();
    void initializeGui();
    void initializeRenderer();
    void shutdown();

    void drawMainMenu();
    void drawScenePanel();
    void drawInspectorPanel();
    void drawViewportPanel();
    void drawAboutPopup();
    void renderScene(int width, int height);

    void discoverModels();
    bool loadModel(const std::filesystem::path& path);
    std::filesystem::path resolvePath(const std::filesystem::path& path) const;
    void resetObjectTransform();

    GLFWwindow* window_{nullptr};
    bool guiInitialized_{false};
    bool shutdownComplete_{false};

    std::unique_ptr<Shader> shader_;
    std::unique_ptr<Mesh> mesh_;
    std::unique_ptr<RenderTarget> renderTarget_;
    Camera camera_;
    RenderSettings settings_;

    std::filesystem::path sourceRoot_;
    std::filesystem::path currentModelPath_;
    std::vector<std::filesystem::path> availableModels_;
    std::array<char, 1024> modelPathBuffer_{};
    std::string statusMessage_{"Ready"};
    std::string modelWarnings_;
    std::string gpuDescription_;

    glm::vec3 modelCenter_{0.0f};
    glm::vec3 modelRotationDegrees_{0.0f};
    float modelNormalizationScale_{1.0f};
    float modelScale_{1.0f};
    std::size_t loadedVertexCount_{0};
    std::size_t loadedTriangleCount_{0};
    std::size_t unsupportedModelCount_{0};

    bool showAbout_{false};
    bool showImGuiDemo_{false};
    double previousFrameTime_{0.0};
};
