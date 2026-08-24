#pragma once

#include <array>
#include <limits>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

struct PrismRay2D {
    glm::vec2 origin{0.0f};
    glm::vec2 direction{1.0f, 0.0f};
};

struct PrismOpticalPath {
    bool valid{false};
    bool totalInternalReflection{false};
    glm::vec2 entryPoint{0.0f};
    glm::vec2 exitPoint{0.0f};
    glm::vec2 entryNormal{0.0f};
    glm::vec2 exitNormal{0.0f};
    glm::vec2 incidentDirection{1.0f, 0.0f};
    glm::vec2 internalDirection{1.0f, 0.0f};
    glm::vec2 exitDirection{1.0f, 0.0f};
    float entryTransmittance{0.0f};
    float exitTransmittance{0.0f};
    float totalTransmittance{0.0f};
};

enum class PrismSpectrumMode {
    Continuous,
    SevenBand
};

struct PrismSpectralSample {
    float wavelengthNanometers{550.0f};
    float indexOfRefraction{1.5f};
    glm::vec3 linearRgb{1.0f};
    float transmittance{0.0f};
    float normalizedEnergy{0.0f};
    PrismOpticalPath path;
};

struct SpectralBeamData {
    PrismRay2D incidentRay;
    PrismSpectrumMode mode{PrismSpectrumMode::Continuous};
    std::vector<PrismSpectralSample> samples;
};

// Traces one monochromatic center ray through a convex triangular prism
// cross-section. Triangle vertices may be clockwise or counter-clockwise.
PrismOpticalPath traceTriangularPrism(
    const std::array<glm::vec2, 3>& triangle,
    const PrismRay2D& ray,
    float prismIndexOfRefraction
);

float prismIorAtWavelength(
    float centralIndexOfRefraction,
    float dispersion,
    float wavelengthNanometers
);

glm::vec3 wavelengthToLinearSrgb(float wavelengthNanometers);

SpectralBeamData tracePrismSpectrum(
    const std::array<glm::vec2, 3>& triangle,
    const PrismRay2D& ray,
    float centralIndexOfRefraction,
    float dispersion,
    int sampleCount,
    PrismSpectrumMode mode,
    float attenuationDistance = std::numeric_limits<float>::infinity(),
    glm::vec3 attenuationColor = glm::vec3(1.0f)
);
