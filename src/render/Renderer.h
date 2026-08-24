#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "optics/PrismOptics.h"

class Camera;
class CausticsMap;
class DebugGrid;
class EnvironmentMap;
class GpuModel;
class OpticalPathDebugRenderer;
class PostProcessor;
class RenderTarget;
class Shader;
class ShadowMap;
class SpectralBeamRenderer;
class TextureCache;

enum class GlassDebugView {
    Final = 0,
    Reflection = 1,
    Refraction = 2,
    IndexOfRefraction = 3,
    RefractedUv = 4,
    Thickness = 5,
    Transmittance = 6,
    RgbDispersion = 7,
    BackfaceThickness = 8,
    ExitSurfaceNormal = 9,
    ObjectId = 10,
    Caustics = 11,
    TransmissionShadow = 12
};

enum class CausticsMode {
    Projector = 0,
    LightSpace = 1
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
    bool coloredTransmissionShadowsEnabled{true};
    bool causticsEnabled{false};
    CausticsMode causticsMode{CausticsMode::LightSpace};
    float causticsStrength{1.8f};
    float causticsScale{1.0f};
    glm::vec3 causticsDirection{0.0f};
    float causticsSharpness{0.72f};
    bool causticsAnimated{false};
    float causticsAnimationPhase{0.0f};
    float causticsReceiverPlaneY{-0.72f};
    bool transmissionEnabled{true};
    bool skyboxEnabled{true};
    bool toneMapping{true};
    bool bloom{true};
    bool showPrismIncidentBeam{false};
    bool prismOpticalPathValid{false};
    bool prismTotalInternalReflection{false};
    float environmentIntensity{0.55f};
    float refractionScale{0.18f};
    int refractionSteps{12};
    float volumeThicknessScale{1.0f};
    bool geometricThicknessEnabled{true};
    bool twoInterfaceRefractionEnabled{true};
    bool volumeGlassOverrideEnabled{false};
    float volumeGlassTransmission{1.0f};
    float volumeGlassRoughness{0.06f};
    glm::vec3 volumeGlassAttenuationColor{0.68f, 0.86f, 0.22f};
    float volumeGlassAttenuationDistance{0.85f};
    float dispersionStrength{0.0f};
    GlassDebugView glassDebugView{GlassDebugView::Final};
    float exposure{1.0f};
    float bloomThreshold{1.0f};
    float bloomIntensity{0.12f};
    SpectralBeamData prismSpectrum;
    float prismBeamOutputLength{2.4f};
    float prismBeamWidth{0.045f};
    float prismBeamIntensity{5.0f};
    float prismBeamEdgeSoftness{0.65f};
    float prismBeamBloomContribution{0.35f};
    glm::vec3 prismBeamWhitePoint{1.0f};
    float indexOfRefractionOverride{0.0f};
    bool showPrismOpticalPathDebug{false};
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
    double latestGpuFrameMeasurementMilliseconds() const { return latestGpuFrameMeasurementMilliseconds_; }
    std::size_t gpuFrameMeasurementSerial() const { return gpuFrameMeasurementSerial_; }
    bool hasPrismBeamGpuTime() const { return hasPrismBeamGpuTime_; }
    double prismBeamGpuTimeMilliseconds() const { return prismBeamGpuTimeMilliseconds_; }
    double latestPrismBeamMeasurementMilliseconds() const { return latestPrismBeamMeasurementMilliseconds_; }
    std::size_t prismBeamMeasurementSerial() const { return prismBeamMeasurementSerial_; }
    bool hasCausticsGpuTime() const { return hasCausticsGpuTime_; }
    double causticsGpuTimeMilliseconds() const { return causticsGpuTimeMilliseconds_; }
    double latestCausticsMeasurementMilliseconds() const { return latestCausticsMeasurementMilliseconds_; }
    std::size_t causticsMeasurementSerial() const { return causticsMeasurementSerial_; }
    std::size_t drawCallCount() const { return drawCallCount_; }
    const std::vector<std::string>& activePassNames() const { return activePassNames_; }
    int shadowResolution() const;
    int renderWidth() const;
    int renderHeight() const;
    std::size_t estimatedRenderMemoryBytes() const;
    TextureCache& textureCache();

private:
    std::unique_ptr<Shader> shader_;
    std::unique_ptr<CausticsMap> causticsMap_;
    std::unique_ptr<DebugGrid> debugGrid_;
    std::unique_ptr<EnvironmentMap> environmentMap_;
    std::unique_ptr<OpticalPathDebugRenderer> opticalPathDebugRenderer_;
    std::unique_ptr<ShadowMap> shadowMap_;
    std::unique_ptr<SpectralBeamRenderer> spectralBeamRenderer_;
    std::unique_ptr<Shader> shadowShader_;
    std::unique_ptr<Shader> transmissionShadowShader_;
    std::unique_ptr<Shader> glassThicknessShader_;
    std::unique_ptr<PostProcessor> postProcessor_;
    std::unique_ptr<RenderTarget> renderTarget_;
    std::unique_ptr<TextureCache> textureCache_;
    std::array<unsigned int, 4> timingQueries_{};
    std::array<bool, 4> timingQueryPending_{};
    std::array<unsigned int, 4> beamStartQueries_{};
    std::array<unsigned int, 4> beamEndQueries_{};
    std::array<bool, 4> beamTimingPending_{};
    std::array<unsigned int, 4> causticsStartQueries_{};
    std::array<unsigned int, 4> causticsEndQueries_{};
    std::array<bool, 4> causticsTimingPending_{};
    std::size_t nextTimingQuery_{0};
    std::size_t nextBeamTimingQuery_{0};
    std::size_t nextCausticsTimingQuery_{0};
    double gpuFrameTimeMilliseconds_{0.0};
    double prismBeamGpuTimeMilliseconds_{0.0};
    double causticsGpuTimeMilliseconds_{0.0};
    double latestGpuFrameMeasurementMilliseconds_{0.0};
    double latestPrismBeamMeasurementMilliseconds_{0.0};
    double latestCausticsMeasurementMilliseconds_{0.0};
    std::size_t gpuFrameMeasurementSerial_{0};
    std::size_t prismBeamMeasurementSerial_{0};
    std::size_t causticsMeasurementSerial_{0};
    std::size_t drawCallCount_{0};
    bool hasGpuFrameTime_{false};
    bool hasPrismBeamGpuTime_{false};
    bool hasCausticsGpuTime_{false};
    std::vector<std::string> activePassNames_;
};
