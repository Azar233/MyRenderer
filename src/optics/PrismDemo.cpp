#include "optics/PrismDemo.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/trigonometric.hpp>

namespace {

float srgbChannelToLinear(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.04045f
        ? value / 12.92f
        : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

} // namespace

const std::array<glm::vec2, 3>& prismDemoCrossSection() {
    static const std::array<glm::vec2, 3> triangle{
        glm::vec2(-0.70f, -0.48125f),
        glm::vec2(0.70f, -0.48125f),
        glm::vec2(0.0f, 0.74375f)
    };
    return triangle;
}

const char* prismOpticalPresetName(PrismOpticalPreset preset) {
    switch (preset) {
    case PrismOpticalPreset::CrownGlass: return "Crown Glass";
    case PrismOpticalPreset::WaterLike: return "Water-like";
    case PrismOpticalPreset::DiamondLike: return "Diamond-like";
    case PrismOpticalPreset::ExaggeratedCover: return "Exaggerated Cover";
    default: return "Crown Glass";
    }
}

PrismDemoParameters prismOpticalPresetParameters(PrismOpticalPreset preset) {
    PrismDemoParameters parameters;
    switch (preset) {
    case PrismOpticalPreset::WaterLike:
        parameters.centralIndexOfRefraction = 1.333f;
        parameters.dispersion = 0.36f;
        parameters.attenuationDistance = 20.0f;
        parameters.attenuationColor = glm::vec3(0.94f, 0.985f, 1.0f);
        break;
    case PrismOpticalPreset::DiamondLike:
        parameters.centralIndexOfRefraction = 2.417f;
        parameters.dispersion = 0.36f;
        parameters.attenuationDistance = 15.0f;
        parameters.attenuationColor = glm::vec3(0.98f, 0.995f, 1.0f);
        break;
    case PrismOpticalPreset::ExaggeratedCover:
        parameters.centralIndexOfRefraction = 1.62f;
        parameters.dispersion = 1.35f;
        parameters.spectrumMode = PrismSpectrumMode::SevenBand;
        parameters.whitePointKelvin = 7200.0f;
        parameters.attenuationDistance = 10.0f;
        parameters.attenuationColor = glm::vec3(0.92f, 0.97f, 1.0f);
        break;
    case PrismOpticalPreset::CrownGlass:
    default:
        break;
    }
    return parameters;
}

glm::vec3 colorTemperatureToLinearSrgb(float kelvin) {
    const float temperature = std::clamp(kelvin, 1000.0f, 40000.0f) / 100.0f;
    float red = 255.0f;
    if (temperature > 66.0f) {
        red = 329.698727446f * std::pow(temperature - 60.0f, -0.1332047592f);
    }
    const float green = temperature <= 66.0f
        ? 99.4708025861f * std::log(temperature) - 161.1195681661f
        : 288.1221695283f * std::pow(temperature - 60.0f, -0.0755148492f);
    float blue = 255.0f;
    if (temperature <= 19.0f) {
        blue = 0.0f;
    } else if (temperature < 66.0f) {
        blue = 138.5177312231f * std::log(temperature - 10.0f) - 305.0447927307f;
    }
    const glm::vec3 srgb = glm::clamp(
        glm::vec3(red, green, blue) / 255.0f,
        glm::vec3(0.0f),
        glm::vec3(1.0f)
    );
    return glm::vec3(
        srgbChannelToLinear(srgb.r),
        srgbChannelToLinear(srgb.g),
        srgbChannelToLinear(srgb.b)
    );
}

PrismDemoSolution solvePrismDemo(const PrismDemoParameters& inputParameters) {
    PrismDemoParameters parameters = inputParameters;
    parameters.centralIndexOfRefraction = std::clamp(
        parameters.centralIndexOfRefraction,
        1.0f,
        3.0f
    );
    parameters.dispersion = std::clamp(parameters.dispersion, 0.0f, 2.5f);
    parameters.whitePointKelvin = std::clamp(parameters.whitePointKelvin, 1000.0f, 12000.0f);
    const float angle = glm::radians(std::clamp(parameters.beamAngleDegrees, -30.0f, 30.0f));
    const PrismRay2D incidentRay{
        glm::vec2(-2.4f, -0.15f),
        glm::vec2(std::cos(angle), std::sin(angle))
    };

    PrismDemoSolution solution;
    solution.linearWhitePoint = colorTemperatureToLinearSrgb(parameters.whitePointKelvin);
    solution.spectrum = tracePrismSpectrum(
        prismDemoCrossSection(),
        incidentRay,
        parameters.centralIndexOfRefraction,
        parameters.dispersion,
        parameters.spectralSampleCount,
        parameters.spectrumMode,
        parameters.attenuationDistance,
        parameters.attenuationColor
    );
    for (const PrismSpectralSample& sample : solution.spectrum.samples) {
        solution.valid |= sample.path.valid;
        solution.totalInternalReflection |= sample.path.totalInternalReflection;
    }
    return solution;
}
