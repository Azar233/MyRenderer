#include "optics/PrismOptics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/common.hpp>
#include <glm/vec3.hpp>

namespace {

constexpr float epsilon = 1.0e-5f;

float cross2D(const glm::vec2& a, const glm::vec2& b) {
    return a.x * b.y - a.y * b.x;
}

struct SurfaceHit {
    float distance{0.0f};
    std::size_t edgeIndex{0U};
    glm::vec2 point{0.0f};
    glm::vec2 outwardNormal{0.0f};
};

std::optional<SurfaceHit> nearestSurfaceHit(
    const std::array<glm::vec2, 3>& triangle,
    const glm::vec2& origin,
    const glm::vec2& direction,
    bool entering
) {
    std::optional<SurfaceHit> nearest;
    for (std::size_t edgeIndex = 0; edgeIndex < triangle.size(); ++edgeIndex) {
        const glm::vec2& from = triangle[edgeIndex];
        const glm::vec2& to = triangle[(edgeIndex + 1U) % triangle.size()];
        const glm::vec2 edge = to - from;
        const float denominator = cross2D(direction, edge);
        if (std::abs(denominator) <= epsilon) {
            continue;
        }

        const glm::vec2 toSurface = from - origin;
        const float distance = cross2D(toSurface, edge) / denominator;
        const float edgePosition = cross2D(toSurface, direction) / denominator;
        if (distance <= epsilon || edgePosition < -epsilon || edgePosition > 1.0f + epsilon) {
            continue;
        }

        const glm::vec2 outwardNormal = glm::normalize(glm::vec2(edge.y, -edge.x));
        const float facing = glm::dot(direction, outwardNormal);
        if ((entering && facing >= -epsilon) || (!entering && facing <= epsilon)) {
            continue;
        }
        if (!nearest.has_value() || distance < nearest->distance) {
            nearest = SurfaceHit{
                distance,
                edgeIndex,
                origin + direction * distance,
                outwardNormal
            };
        }
    }
    return nearest;
}

bool refractDirection(
    const glm::vec2& incident,
    const glm::vec2& normalAgainstIncident,
    float incidentIor,
    float transmittedIor,
    glm::vec2& refracted
) {
    const float eta = incidentIor / transmittedIor;
    const float cosine = std::clamp(-glm::dot(normalAgainstIncident, incident), 0.0f, 1.0f);
    const float discriminant = 1.0f - eta * eta * (1.0f - cosine * cosine);
    if (discriminant < 0.0f) {
        refracted = glm::vec2(0.0f);
        return false;
    }
    refracted = glm::normalize(
        eta * incident
        + (eta * cosine - std::sqrt(std::max(discriminant, 0.0f))) * normalAgainstIncident
    );
    return true;
}

float fresnelTransmittance(
    const glm::vec2& incident,
    const glm::vec2& normalAgainstIncident,
    float incidentIor,
    float transmittedIor
) {
    const float cosine = std::clamp(-glm::dot(normalAgainstIncident, incident), 0.0f, 1.0f);
    const float ratio = (incidentIor - transmittedIor) / (incidentIor + transmittedIor);
    const float f0 = ratio * ratio;
    const float reflectance = f0 + (1.0f - f0) * std::pow(1.0f - cosine, 5.0f);
    return std::clamp(1.0f - reflectance, 0.0f, 1.0f);
}

} // namespace

float prismIorAtWavelength(
    float centralIndexOfRefraction,
    float dispersion,
    float wavelengthNanometers
) {
    const float centralIor = std::max(centralIndexOfRefraction, 1.0f);
    if (dispersion <= epsilon) {
        return centralIor;
    }
    const float abbeNumber = 20.0f / dispersion;
    const float wavelength = std::clamp(wavelengthNanometers, 380.0f, 700.0f);
    const float wavelengthTerm = 523655.0f / (wavelength * wavelength) - 1.5168f;
    return std::max(centralIor + (centralIor - 1.0f) / abbeNumber * wavelengthTerm, 1.0f);
}

glm::vec3 wavelengthToLinearSrgb(float wavelengthNanometers) {
    const float wavelength = std::clamp(wavelengthNanometers, 380.0f, 700.0f);
    const auto gaussian = [](float value) { return std::exp(-0.5f * value * value); };

    // Wyman, Sloan, and Shirley's analytic fit to the CIE 1931 2-degree
    // color matching functions: https://jcgt.org/published/0002/02/01/
    const float tx1 = (wavelength - 442.0f) * (wavelength < 442.0f ? 0.0624f : 0.0374f);
    const float tx2 = (wavelength - 599.8f) * (wavelength < 599.8f ? 0.0264f : 0.0323f);
    const float tx3 = (wavelength - 501.1f) * (wavelength < 501.1f ? 0.0490f : 0.0382f);
    const float x = 0.362f * gaussian(tx1) + 1.056f * gaussian(tx2) - 0.065f * gaussian(tx3);

    const float ty1 = (wavelength - 568.8f) * (wavelength < 568.8f ? 0.0213f : 0.0247f);
    const float ty2 = (wavelength - 530.9f) * (wavelength < 530.9f ? 0.0613f : 0.0322f);
    const float y = 0.821f * gaussian(ty1) + 0.286f * gaussian(ty2);

    const float tz1 = (wavelength - 437.0f) * (wavelength < 437.0f ? 0.0845f : 0.0278f);
    const float tz2 = (wavelength - 459.0f) * (wavelength < 459.0f ? 0.0385f : 0.0725f);
    const float z = 1.217f * gaussian(tz1) + 0.681f * gaussian(tz2);

    glm::vec3 rgb(
        3.2406f * x - 1.5372f * y - 0.4986f * z,
        -0.9689f * x + 1.8758f * y + 0.0415f * z,
        0.0557f * x - 0.2040f * y + 1.0570f * z
    );
    rgb = glm::max(rgb, glm::vec3(0.0f));
    return rgb;
}

SpectralBeamData tracePrismSpectrum(
    const std::array<glm::vec2, 3>& triangle,
    const PrismRay2D& ray,
    float centralIndexOfRefraction,
    float dispersion,
    int sampleCount,
    PrismSpectrumMode mode,
    float attenuationDistance,
    glm::vec3 attenuationColor
) {
    SpectralBeamData spectrum;
    spectrum.incidentRay = ray;
    spectrum.mode = mode;
    static constexpr std::array<float, 7> sevenBandWavelengths{
        650.0f, 610.0f, 580.0f, 540.0f, 500.0f, 460.0f, 420.0f
    };
    static constexpr std::array<int, 4> continuousQualityTiers{7, 15, 21, 31};
    const int continuousCount = *std::min_element(
        continuousQualityTiers.begin(),
        continuousQualityTiers.end(),
        [sampleCount](int left, int right) {
            return std::abs(left - sampleCount) < std::abs(right - sampleCount);
        }
    );
    const int count = mode == PrismSpectrumMode::SevenBand
        ? static_cast<int>(sevenBandWavelengths.size())
        : continuousCount;
    spectrum.samples.reserve(static_cast<std::size_t>(count));

    float totalRawEnergy = 0.0f;
    for (int index = 0; index < count; ++index) {
        const float wavelength = mode == PrismSpectrumMode::SevenBand
            ? sevenBandWavelengths[static_cast<std::size_t>(index)]
            : 380.0f + 320.0f * static_cast<float>(index) / static_cast<float>(count - 1);
        const float ior = prismIorAtWavelength(centralIndexOfRefraction, dispersion, wavelength);
        const glm::vec3 linearRgb = wavelengthToLinearSrgb(wavelength);
        PrismOpticalPath path = traceTriangularPrism(triangle, ray, ior);
        float beerLambertTransmittance = 1.0f;
        if (path.valid && std::isfinite(attenuationDistance) && attenuationDistance > epsilon) {
            const glm::vec3 safeAttenuation = glm::clamp(
                attenuationColor,
                glm::vec3(epsilon),
                glm::vec3(1.0f)
            );
            const float colorWeight = std::max(
                linearRgb.r + linearRgb.g + linearRgb.b,
                epsilon
            );
            const float spectralAttenuation = glm::dot(linearRgb, safeAttenuation)
                / colorWeight;
            const float distanceInGlass = glm::length(path.exitPoint - path.entryPoint);
            beerLambertTransmittance = std::pow(
                std::max(spectralAttenuation, epsilon),
                distanceInGlass / attenuationDistance
            );
        }
        const float rawEnergy = path.valid
            ? path.totalTransmittance * beerLambertTransmittance
            : 0.0f;
        totalRawEnergy += rawEnergy;
        spectrum.samples.push_back(PrismSpectralSample{
            wavelength,
            ior,
            linearRgb,
            rawEnergy,
            rawEnergy,
            std::move(path)
        });
    }

    if (totalRawEnergy > epsilon) {
        for (PrismSpectralSample& sample : spectrum.samples) {
            sample.normalizedEnergy /= totalRawEnergy;
        }
    }
    return spectrum;
}

PrismOpticalPath traceTriangularPrism(
    const std::array<glm::vec2, 3>& inputTriangle,
    const PrismRay2D& ray,
    float prismIndexOfRefraction
) {
    PrismOpticalPath path;
    const float directionLengthSquared = glm::dot(ray.direction, ray.direction);
    if (directionLengthSquared <= epsilon || prismIndexOfRefraction < 1.0f) {
        return path;
    }

    std::array<glm::vec2, 3> triangle = inputTriangle;
    const float signedDoubleArea = cross2D(triangle[1] - triangle[0], triangle[2] - triangle[0]);
    if (std::abs(signedDoubleArea) <= epsilon) {
        return path;
    }
    if (signedDoubleArea < 0.0f) {
        std::swap(triangle[1], triangle[2]);
    }

    path.incidentDirection = glm::normalize(ray.direction);
    const auto entry = nearestSurfaceHit(triangle, ray.origin, path.incidentDirection, true);
    if (!entry.has_value()) {
        return path;
    }

    path.entryPoint = entry->point;
    path.entryNormal = entry->outwardNormal;
    path.entryTransmittance = fresnelTransmittance(
        path.incidentDirection,
        path.entryNormal,
        1.0f,
        prismIndexOfRefraction
    );
    if (!refractDirection(
            path.incidentDirection,
            path.entryNormal,
            1.0f,
            prismIndexOfRefraction,
            path.internalDirection)) {
        return path;
    }

    const glm::vec2 insideOrigin = path.entryPoint + path.internalDirection * (epsilon * 8.0f);
    const auto exit = nearestSurfaceHit(triangle, insideOrigin, path.internalDirection, false);
    if (!exit.has_value()) {
        return path;
    }
    path.exitPoint = exit->point;
    path.exitNormal = exit->outwardNormal;

    const glm::vec2 exitNormalAgainstIncident = -path.exitNormal;
    path.exitTransmittance = fresnelTransmittance(
        path.internalDirection,
        exitNormalAgainstIncident,
        prismIndexOfRefraction,
        1.0f
    );
    path.totalInternalReflection = !refractDirection(
        path.internalDirection,
        exitNormalAgainstIncident,
        prismIndexOfRefraction,
        1.0f,
        path.exitDirection
    );
    if (path.totalInternalReflection) {
        path.exitDirection = glm::normalize(
            path.internalDirection
            - 2.0f * glm::dot(path.internalDirection, path.exitNormal) * path.exitNormal
        );
        path.exitTransmittance = 0.0f;
    }
    path.totalTransmittance = path.entryTransmittance * path.exitTransmittance;
    path.valid = true;
    return path;
}
