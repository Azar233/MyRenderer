#include "module/BuiltinModules.h"
#include "render/Camera.h"
#include "render/Renderer.h"

#include <algorithm>
#include <memory>

#include <glm/common.hpp>

namespace {

class CoastalSequenceModule final : public ISceneModule {
public:
    ModuleManifest manifest() const override {
        ModuleManifest result;
        result.id = BuiltinModules::coastalSequenceId;
        result.displayName = "Coastal Day to Night";
        result.kind = ModuleKind::Scene;
        result.cmakeTarget = "MyRendererModules";
        result.sourceRoot = "src/module";
        result.apiVersion = moduleApiVersion;
        result.buildId = moduleBuildId();
        return result;
    }

    void registerParameters(ParameterRegistry& p) override {
        p.registerBool("enabled", "Enabled", true, "Animate the coastal presentation.");
        p.registerFloat("sunStartElevation", "Sun start elevation", 48.0f, -15.0f, 90.0f);
        p.registerFloat("sunEndElevation", "Sun end elevation", -8.0f, -15.0f, 90.0f);
        p.registerFloat("sunStartAzimuth", "Sun start azimuth", 118.0f, -360.0f, 360.0f);
        p.registerFloat("sunEndAzimuth", "Sun end azimuth", 250.0f, -360.0f, 360.0f);
        p.registerFloat("moonIntensity", "Moon intensity", 1.0f, 0.0f, 4.0f);
        p.registerFloat("starIntensity", "Star intensity", 1.0f, 0.0f, 4.0f);
        p.registerFloat("fogStart", "Fog start", 0.35f, 0.0f, 5.0f);
        p.registerFloat("fogEnd", "Fog end", 1.1f, 0.0f, 5.0f);
        p.registerFloat("windStartX", "Wind start X", 0.4f, -4.0f, 4.0f);
        p.registerFloat("windStartZ", "Wind start Z", 0.1f, -4.0f, 4.0f);
        p.registerFloat("windEndX", "Wind end X", 1.0f, -4.0f, 4.0f);
        p.registerFloat("windEndZ", "Wind end Z", 0.5f, -4.0f, 4.0f);
        p.registerFloat("waveStartAmplitude", "Wave start amplitude", 0.08f, 0.0f, 2.0f);
        p.registerFloat("waveEndAmplitude", "Wave end amplitude", 0.34f, 0.0f, 2.0f);
        p.registerFloat("waveStartSpeed", "Wave start speed", 0.6f, 0.0f, 4.0f);
        p.registerFloat("waveEndSpeed", "Wave end speed", 1.4f, 0.0f, 4.0f);
        p.registerFloat("cameraStartYaw", "Camera start yaw", -8.0f, -180.0f, 180.0f);
        p.registerFloat("cameraEndYaw", "Camera end yaw", 4.0f, -180.0f, 180.0f);
        p.registerFloat("cameraStartDistance", "Camera start distance", 24.0f, 0.5f, 100.0f);
        p.registerFloat("cameraEndDistance", "Camera end distance", 21.0f, 0.5f, 100.0f);
    }

    bool initialize(const SceneContext&, std::string&) override { return true; }
    void reset() override {}
    bool fixedUpdate(SceneContext&, double, std::string&) override { return true; }

    void applyPresentation(const SceneContext& context,
        const CameraOrbitState& authoredCamera, const RendererSettings& authoredRenderer,
        CameraOrbitState& camera, RendererSettings& renderer) const override {
        camera = authoredCamera;
        renderer = authoredRenderer;
        const ParameterRegistry& p = context.parameters();
        if (!p.boolValue("enabled", true)) return;
        const Timeline& timeline = context.timeline();
        const int span = timeline.endFrame() - timeline.startFrame();
        const float linear = span > 0 ? std::clamp(
            static_cast<float>(timeline.frame() - timeline.startFrame()) / static_cast<float>(span),
            0.0f, 1.0f) : 0.0f;
        const float t = linear * linear * (3.0f - 2.0f * linear);
        const auto blend = [&p, t](const char* start, const char* end,
            float startDefault, float endDefault) {
            return glm::mix(p.floatValue(start, startDefault),
                p.floatValue(end, endDefault), t);
        };
        renderer.atmosphere.enabled = true;
        renderer.atmosphere.sunElevationDegrees = blend(
            "sunStartElevation", "sunEndElevation", 48.0f, -8.0f);
        renderer.atmosphere.sunAzimuthDegrees = blend(
            "sunStartAzimuth", "sunEndAzimuth", 118.0f, 250.0f);
        renderer.atmosphere.nightSkyEnabled = true;
        renderer.atmosphere.moonIntensity = p.floatValue("moonIntensity", 1.0f);
        renderer.atmosphere.starIntensity = p.floatValue("starIntensity", 1.0f);
        // The analytic atmosphere is a daytime single-scattering approximation.
        // Fade its solar energy through civil twilight instead of letting a sun
        // below the horizon illuminate the scene like a second daytime key.
        const float daylight = std::clamp(
            (renderer.atmosphere.sunElevationDegrees + 8.0f) / 12.0f, 0.0f, 1.0f);
        const float twilightEnergy = 0.03f + 0.97f * daylight;
        renderer.atmosphere.skyIntensity *= twilightEnergy;
        renderer.atmosphere.sunIntensity *= twilightEnergy;
        renderer.atmosphere.aerialPerspectiveEnabled = true;
        renderer.atmosphere.aerialPerspectiveStrength = blend("fogStart", "fogEnd", 0.35f, 1.1f);
        renderer.water.enabled = true;
        renderer.water.preset = WaterPreset::Custom;
        renderer.water.windDirection = glm::vec2(
            blend("windStartX", "windEndX", 0.4f, 1.0f),
            blend("windStartZ", "windEndZ", 0.1f, 0.5f));
        renderer.water.amplitude = blend(
            "waveStartAmplitude", "waveEndAmplitude", 0.08f, 0.34f);
        renderer.water.speed = blend("waveStartSpeed", "waveEndSpeed", 0.6f, 1.4f);
        renderer.water.timeSeconds = static_cast<float>(timeline.frame() - timeline.startFrame())
            / static_cast<float>(timeline.framesPerSecond());
        camera.yawDegrees = blend("cameraStartYaw", "cameraEndYaw", -8.0f, 4.0f);
        camera.distance = blend("cameraStartDistance", "cameraEndDistance", 24.0f, 21.0f);
    }

    std::string serializeState() const override { return "coastal-sequence|v1"; }
};

} // namespace

std::unique_ptr<ISceneModule> makeCoastalSequenceModule() {
    return std::make_unique<CoastalSequenceModule>();
}
