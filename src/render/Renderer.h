#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class Camera;
class DebugGrid;
class EnvironmentMap;
class GpuModel;
class PostProcessor;
class RenderTarget;
class Shader;
class ShadowMap;
class TextureCache;

enum class GlassDebugView {
    Final = 0,
    Reflection = 1,
    Refraction = 2,
    IndexOfRefraction = 3,
    RefractedUv = 4,
    Thickness = 5,
    Transmittance = 6,
    RgbDispersion = 7
};

struct RendererSettings {
    glm::vec3 backgroundColor{0.055f, 0.065f, 0.085f};
    glm::vec3 baseColor{1.0f};
    glm::vec3 lightDirection{-0.45f, -0.8f, -0.35f};
    float ambientStrength{0.18f};
    float diffuseStrength{0.92f};
    float specularStrength{0.28f};
    float shininess{48.0f};
    int msaaSamples{4};
    bool wireframe{false};
    bool cullBackFaces{false};
    bool normalMapping{true};
    bool showGrid{true};
    bool showAxes{true};
    bool pbrEnabled{true};
    bool iblEnabled{true};
    bool shadowsEnabled{true};
    bool transmissionEnabled{true};
    bool skyboxEnabled{true};
    bool toneMapping{true};
    bool bloom{true};
    float environmentIntensity{0.55f};
    float refractionScale{0.18f};
    int refractionSteps{12};
    float volumeThicknessScale{1.0f};
    float dispersionStrength{0.0f};
    GlassDebugView glassDebugView{GlassDebugView::Final};
    float exposure{1.0f};
    float bloomThreshold{1.0f};
    float bloomIntensity{0.12f};
};

struct RenderItem {
    const GpuModel* model{nullptr};
    glm::mat4 modelMatrix{1.0f};
    glm::vec3 tint{1.0f};
    bool visible{true};
    bool castsShadow{true};
};

class Renderer {
public:
    Renderer(
        const std::filesystem::path& vertexShaderPath,
        const std::filesystem::path& fragmentShaderPath,
        const std::filesystem::path& debugVertexShaderPath,
        const std::filesystem::path& debugFragmentShaderPath
    );
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void render(
        const std::vector<RenderItem>& renderItems,
        const Camera& camera,
        const RendererSettings& settings,
        int width,
        int height
    );

    unsigned int colorTexture() const;
    bool saveScreenshot(const std::filesystem::path& path, std::string& error) const;
    int activeMsaaSamples() const;
    bool hasGpuFrameTime() const { return hasGpuFrameTime_; }
    double gpuFrameTimeMilliseconds() const { return gpuFrameTimeMilliseconds_; }
    std::size_t drawCallCount() const { return drawCallCount_; }
    const std::vector<std::string>& activePassNames() const { return activePassNames_; }
    int shadowResolution() const;
    TextureCache& textureCache();

private:
    std::unique_ptr<Shader> shader_;
    std::unique_ptr<DebugGrid> debugGrid_;
    std::unique_ptr<EnvironmentMap> environmentMap_;
    std::unique_ptr<ShadowMap> shadowMap_;
    std::unique_ptr<Shader> shadowShader_;
    std::unique_ptr<PostProcessor> postProcessor_;
    std::unique_ptr<RenderTarget> renderTarget_;
    std::unique_ptr<TextureCache> textureCache_;
    std::array<unsigned int, 4> timingQueries_{};
    std::array<bool, 4> timingQueryPending_{};
    std::size_t nextTimingQuery_{0};
    double gpuFrameTimeMilliseconds_{0.0};
    std::size_t drawCallCount_{0};
    bool hasGpuFrameTime_{false};
    std::vector<std::string> activePassNames_;
};
