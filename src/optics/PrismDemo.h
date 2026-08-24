#pragma once

#include <array>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "optics/PrismOptics.h"

enum class PrismOpticalPreset {
    CrownGlass = 0,
    WaterLike = 1,
    DiamondLike = 2,
    ExaggeratedCover = 3
};

struct PrismDemoParameters {
    float beamAngleDegrees{7.65f};
    float centralIndexOfRefraction{1.52f};
    float dispersion{0.33f};
    int spectralSampleCount{21};
    PrismSpectrumMode spectrumMode{PrismSpectrumMode::Continuous};
    float whitePointKelvin{6500.0f};
    float attenuationDistance{8.0f};
    glm::vec3 attenuationColor{0.88f, 0.96f, 1.0f};
};

struct PrismDemoSolution {
    SpectralBeamData spectrum;
    glm::vec3 linearWhitePoint{1.0f};
    bool valid{false};
    bool totalInternalReflection{false};
};

const char* prismOpticalPresetName(PrismOpticalPreset preset);
PrismDemoParameters prismOpticalPresetParameters(PrismOpticalPreset preset);
glm::vec3 colorTemperatureToLinearSrgb(float kelvin);
PrismDemoSolution solvePrismDemo(const PrismDemoParameters& parameters);

const std::array<glm::vec2, 3>& prismDemoCrossSection();
