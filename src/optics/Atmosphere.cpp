#include "optics/Atmosphere.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace atmosphere {
namespace {

constexpr float pi = 3.14159265358979323846f;
// Earth-like scale heights in metres.
constexpr float rayleighScaleHeight = 8000.0f;
constexpr float mieScaleHeight = 1200.0f;
// Sea-level scattering coefficients (1/m). Rayleigh is what makes the sky blue; the Mie
// term is grey and only shaped by its phase function.
const glm::vec3 rayleighCoefficient(5.8e-6f, 13.5e-6f, 33.1e-6f);
constexpr float mieCoefficient = 21.0e-6f;
// Henyey-Greenstein anisotropy for a mixed rural aerosol. The literature spans roughly
// 0.5-0.8; 0.76-0.8 concentrates the aureole so strongly that the grey forward-scattered
// term washes the zenith out, which is not what a clear sky looks like.
constexpr float mieAnisotropy = 0.5f;
// The sun disk's radiance relative to the sky. The absolute scale is arbitrary -- the RGB values
// here are not photometric -- so it is pinned to the one ratio that is observable in the render:
// the clear-day illuminance ratio `E_sun / E_sky` of roughly 10. The disk subtends
// `2*pi*(1 - cos(0.6 deg))`, about 3.4e-4 sr, so a radiance of 4.6e3 against the sky's ~0.1
// radiance lands in that range. That matters beyond looks: the environment's ground hemisphere
// and its prefiltered specular both integrate the disk, so a token-bright sun there would leave
// reflections sunless and the lower hemisphere darker than the ground the key light actually
// lights.
constexpr float sunDiskRadianceScale = 4.6e3f;
constexpr float sunAngularRadius = 0.6f;
constexpr float moonAngularRadius = 1.3f; // wide enough to survive the environment map
float saturate(float value);

std::uint32_t starHash(std::uint32_t x) {
    x ^= x >> 16U;
    x *= 0x7feb352dU;
    x ^= x >> 15U;
    x *= 0x846ca68bU;
    return x ^ (x >> 16U);
}

glm::vec3 nightRadiance(const glm::vec3& direction, const AtmosphereParameters& parameters) {
    const float visibility = nightVisibility(parameters);
    if (!parameters.nightSkyEnabled || direction.y <= 0.0f) return glm::vec3(0.0f);
    const float fillT = saturate((10.0f - parameters.sunElevationDegrees) / 18.0f);
    const float twilightFill = fillT * fillT * (3.0f - 2.0f * fillT);
    if (twilightFill <= 0.0f) return glm::vec3(0.0f);
    const glm::vec3 moon = moonDirection(parameters);
    const float moonGlow = std::pow(std::max(glm::dot(direction, moon), 0.0f), 12.0f);
    glm::vec3 result = glm::vec3(0.040f, 0.060f, 0.110f) * twilightFill
        + glm::vec3(0.035f, 0.045f, 0.070f) * moonGlow
            * std::max(parameters.moonIntensity, 0.0f) * visibility;

    // Stable spherical cells yield small stars with no frame-dependent random state.
    // Centers stay inside their cells, so one lookup per sky sample is sufficient.
    const float phi = std::atan2(direction.x, direction.z) + pi;
    const float theta = std::acos(std::clamp(direction.y, 0.0f, 1.0f));
    const float u = phi * (64.0f / (2.0f * pi));
    const float v = theta * (48.0f / pi);
    const int cellX = static_cast<int>(u);
    const int cellY = static_cast<int>(v);
    const std::uint32_t hash = starHash(static_cast<std::uint32_t>(cellX)
        + 131U * static_cast<std::uint32_t>(cellY));
    if ((hash & 3U) == 0U) {
        const float centerU = static_cast<float>(cellX) + 0.35f
            + 0.3f * static_cast<float>((hash >> 8U) & 255U) / 255.0f;
        const float centerV = static_cast<float>(cellY) + 0.35f
            + 0.3f * static_cast<float>((hash >> 16U) & 255U) / 255.0f;
        const float angularX = (u - centerU) * (2.0f * pi / 64.0f) * std::sin(theta);
        const float angularY = (v - centerV) * (pi / 48.0f);
        const float distanceDegrees = glm::degrees(std::sqrt(angularX * angularX + angularY * angularY));
        const float star = saturate((0.45f - distanceDegrees) / 0.25f);
        const float brightness = 1.5f + 1.5f * static_cast<float>((hash >> 24U) & 255U) / 255.0f;
        result += glm::vec3(0.75f, 0.84f, 1.0f) * star * brightness
            * std::max(parameters.starIntensity, 0.0f) * visibility;
    }
    const float cosAngle = glm::dot(direction, moon);
    const float cosRadius = std::cos(glm::radians(moonAngularRadius));
    if (moon.y > 0.0f && cosAngle > cosRadius) {
        const float edge = saturate((cosAngle - cosRadius)
            / std::max(1.0f - cosRadius, 1.0e-5f) * 8.0f);
        result += glm::vec3(22.0f, 27.0f, 36.0f) * edge
            * std::max(parameters.moonIntensity, 0.0f) * visibility;
    }
    return result;
}

// Normalises defensively: a degenerate direction (for example a zero vector from an
// uninitialised uniform) must never turn into NaN radiance downstream.
bool normaliseDirection(const glm::vec3& value, glm::vec3& result) {
    const float lengthSquared = glm::dot(value, value);
    if (!(lengthSquared > 1.0e-12f)) return false;
    result = value / std::sqrt(lengthSquared);
    return true;
}

float saturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float rayleighPhase(float cosTheta) {
    return (3.0f / (16.0f * pi)) * (1.0f + cosTheta * cosTheta);
}

float miePhase(float cosTheta) {
    const float g = mieAnisotropy;
    const float g2 = g * g;
    const float denominator = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f - g2) / (4.0f * pi * std::max(denominator * std::sqrt(std::max(denominator, 1.0e-6f)), 1.0e-6f));
}

// Kasten-Young relative air mass for a zenith angle in degrees. Clamped so a sun or a view
// ray at or below the horizon still yields a finite, monotone value.
float relativeAirMass(float zenithDegrees) {
    const float clamped = std::clamp(zenithDegrees, 0.0f, 96.0f);
    const float cosine = std::cos(glm::radians(clamped));
    const float horizonTerm = std::pow(std::max(96.07995f - clamped, 0.01f), -1.6364f);
    return 1.0f / std::max(cosine + 0.50572f * horizonTerm, 1.0e-4f);
}

glm::vec3 rayleighOpticalDepth(float airMass) {
    return rayleighCoefficient * (rayleighScaleHeight * airMass);
}

float mieOpticalDepth(float airMass, const AtmosphereParameters& parameters) {
    return mieCoefficient * std::max(parameters.turbidity, 0.0f) * (mieScaleHeight * airMass);
}

// Single-scattering radiance for a view ray in the upper hemisphere. Shared by the public sky
// query and the ground-irradiance proxy so the two can never drift apart.
glm::vec3 scatteringRadiance(const glm::vec3& direction, const AtmosphereParameters& parameters) {
    const glm::vec3 sun = sunDirection(parameters);
    const float viewZenithDegrees = glm::degrees(std::acos(std::clamp(direction.y, -1.0f, 1.0f)));
    const float sunZenithDegrees = glm::degrees(std::acos(std::clamp(sun.y, -1.0f, 1.0f)));
    const float viewAirMass = relativeAirMass(viewZenithDegrees);
    const float sunAirMass = relativeAirMass(sunZenithDegrees);

    const glm::vec3 viewOpticalDepth = rayleighOpticalDepth(viewAirMass)
        + glm::vec3(mieOpticalDepth(viewAirMass, parameters));
    const glm::vec3 totalOpticalDepth = viewOpticalDepth
        + rayleighOpticalDepth(sunAirMass)
        + glm::vec3(mieOpticalDepth(sunAirMass, parameters));
    const glm::vec3 transmittance = glm::exp(-totalOpticalDepth);

    // Closed-form integral of an exponential atmosphere along the view ray: each component
    // contributes `beta * H * m * (1 - exp(-tau)) / tau`.
    const glm::vec3 rayleighIntegral = rayleighCoefficient * (rayleighScaleHeight * viewAirMass)
        * (glm::vec3(1.0f) - transmittance) / glm::max(totalOpticalDepth, glm::vec3(1.0e-12f));
    const glm::vec3 mieIntegral = glm::vec3(
        mieCoefficient * std::max(parameters.turbidity, 0.0f) * (mieScaleHeight * viewAirMass)
    ) * (glm::vec3(1.0f) - transmittance) / glm::max(totalOpticalDepth, glm::vec3(1.0e-12f));

    const float cosTheta = glm::dot(direction, sun);
    const glm::vec3 scattering = rayleighIntegral * rayleighPhase(cosTheta)
        + mieIntegral * miePhase(cosTheta);
    return scattering * sunTransmittance(parameters)
        * std::max(parameters.skyIntensity, 0.0f) * std::max(parameters.sunIntensity, 0.0f);
}

// Cosine-weighted average sky radiance over the upper hemisphere, which is what a Lambertian
// ground reflects back as `albedo * irradiance / pi`. Sampling the zenith alone (the first cut)
// left the lower hemisphere black whenever a low sun concentrates the sky into a horizon glow,
// which shows up as a black band under the horizon in any frame that sees past the ground
// geometry. Nine fixed directions -- the pole plus rings at 45 and 75 degrees -- give a smooth
// deterministic gradient for far less work than the per-texel exponential sweep around it.
glm::vec3 skyIrradianceOverPi(const AtmosphereParameters& parameters) {
    glm::vec3 accumulated(0.0f);
    float weightSum = 0.0f;
    for (int ring = 0; ring < 3; ++ring) {
        const float polarDegrees = ring == 0 ? 0.0f : (ring == 1 ? 45.0f : 75.0f);
        const int samples = ring == 0 ? 1 : 4;
        const float polar = glm::radians(polarDegrees);
        const float sampleWeight = std::cos(polar);
        for (int sample = 0; sample < samples; ++sample) {
            const float azimuth = 2.0f * pi * (static_cast<float>(sample) + 0.5f)
                / static_cast<float>(samples);
            const glm::vec3 direction(
                std::sin(polar) * std::sin(azimuth),
                std::cos(polar),
                std::sin(polar) * std::cos(azimuth)
            );
            accumulated += scatteringRadiance(direction, parameters) * sampleWeight;
            weightSum += sampleWeight;
        }
    }
    return accumulated / std::max(weightSum, 1.0e-6f);
}

} // namespace

glm::vec3 sunDirection(const AtmosphereParameters& parameters) {
    const float elevation = glm::radians(parameters.sunElevationDegrees);
    const float azimuth = glm::radians(parameters.sunAzimuthDegrees);
    const float horizontal = std::cos(elevation);
    return glm::vec3(
        horizontal * std::sin(azimuth),
        std::sin(elevation),
        horizontal * std::cos(azimuth)
    );
}

glm::vec3 moonDirection(const AtmosphereParameters& parameters) {
    // A visual day/night orbit with an offset that puts the moon in the coastal camera's
    // evening sky. A calendar-based orbit can replace this mapping later.
    const float elevation = glm::radians(-parameters.sunElevationDegrees * 0.6f);
    const float azimuth = glm::radians(parameters.sunAzimuthDegrees - 90.0f);
    const float horizontal = std::cos(elevation);
    return glm::vec3(horizontal * std::sin(azimuth), std::sin(elevation),
        horizontal * std::cos(azimuth));
}

float nightVisibility(const AtmosphereParameters& parameters) {
    if (!parameters.enabled || !parameters.nightSkyEnabled) return 0.0f;
    const float t = saturate((-parameters.sunElevationDegrees - 2.0f) / 6.0f);
    return t * t * (3.0f - 2.0f * t);
}

float moonKeyStrength(const AtmosphereParameters& parameters) {
    return nightVisibility(parameters) * std::max(parameters.moonIntensity, 0.0f) * 0.55f;
}

bool parametersMatch(const AtmosphereParameters& a, const AtmosphereParameters& b) {
    if (a.enabled != b.enabled) return false;
    if (a.nightSkyEnabled != b.nightSkyEnabled) return false;
    // The sun tolerances are angular degrees of movement; the rest are absolute parameter units.
    // Both are set at the point where the change stops being visible as noise in a rendered frame.
    constexpr float sunTolerance = 0.35f;
    constexpr float parameterTolerance = 0.01f;
    return std::abs(a.sunElevationDegrees - b.sunElevationDegrees) < sunTolerance
        && std::abs(a.sunAzimuthDegrees - b.sunAzimuthDegrees) < sunTolerance
        && std::abs(a.turbidity - b.turbidity) < parameterTolerance
        && std::abs(a.skyIntensity - b.skyIntensity) < parameterTolerance
        && std::abs(a.sunIntensity - b.sunIntensity) < parameterTolerance
        && std::abs(a.groundAlbedo - b.groundAlbedo) < parameterTolerance
        && std::abs(a.moonIntensity - b.moonIntensity) < parameterTolerance
        && std::abs(a.starIntensity - b.starIntensity) < parameterTolerance
        // Aerial perspective does not change the environment cubemap, but it does change what a
        // cached sky is used *for*, so a consumer caching derived data must see it as a change.
        && a.aerialPerspectiveEnabled == b.aerialPerspectiveEnabled
        && std::abs(a.aerialPerspectiveStrength - b.aerialPerspectiveStrength) < parameterTolerance
        && std::abs(a.aerialPerspectiveScaleHeight - b.aerialPerspectiveScaleHeight)
            < parameterTolerance;
}

glm::vec3 sunTransmittance(const AtmosphereParameters& parameters) {
    const glm::vec3 direction = sunDirection(parameters);
    // A sun below the horizon keeps a finite optical depth instead of exploding, so the
    // directional light fades out smoothly rather than switching off with a discontinuity.
    const float zenithDegrees = glm::degrees(std::acos(std::clamp(direction.y, -1.0f, 1.0f)));
    const float sunAirMass = relativeAirMass(zenithDegrees);
    return glm::exp(-(rayleighOpticalDepth(sunAirMass) + glm::vec3(mieOpticalDepth(sunAirMass, parameters))));
}

glm::vec3 opticalDepthAlongSegment(
    const AtmosphereParameters& parameters,
    float segmentLength,
    float directionY,
    float worldUnitsPerMetre
) {
    // The scene's world units are converted here rather than assumed, so a caller that models a
    // metre as one unit and a caller that models a kilometre as one unit reach the same optical
    // depth for the same physical distance.
    const float unitScale = worldUnitsPerMetre > 1.0e-9f ? 1.0f / worldUnitsPerMetre : 1.0f;
    const float length = std::max(segmentLength, 0.0f) * unitScale;
    if (!(length > 0.0f) || !std::isfinite(length)) return glm::vec3(0.0f);

    // A horizontal segment is the limit of the expression below, so the vertical component is
    // clamped away from zero and the exponential is written as `(1 - exp(-x)) / x * len` with
    // `x = len * y / scaleHeight`: at `y -> 0` that tends to `len / scaleHeight` instead of
    // dividing by zero.
    constexpr float minimumVerticalComponent = 1.0e-4f;
    const float vertical = std::max(std::abs(directionY), minimumVerticalComponent);
    // The air mass is the one the sky already uses, evaluated for the ray's own zenith angle, so a
    // ray that points up accumulates exactly the column `sunTransmittance` describes.
    const float zenithDegrees = glm::degrees(std::acos(std::clamp(vertical, 0.0f, 1.0f)));
    const float airMass = relativeAirMass(zenithDegrees);

    const float heightDelta = length * directionY;
    const float scaledHeight = heightDelta / rayleighScaleHeight;
    // Both constituents ride the same relative density profile: `beta * H * airMass * (1 - e^-x)`
    // with `x = heightDelta / H`, which is `beta * H * airMass * len / H` in the horizontal limit
    // and `beta * H * airMass` once the segment reaches the top of the atmosphere. That is the
    // same column `rayleighOpticalDepth` / `mieOpticalDepth` describe, so a ray that escapes
    // saturates on exactly the value `sunTransmittance` is built from. Using one scale height for
    // the path integral while each constituent keeps its own magnitude is the approximation this
    // shares with the rest of the model: it is exact at both limits and never overshoots between
    // them.
    const float columnFactor = std::abs(scaledHeight) < 1.0e-4f
        ? length / rayleighScaleHeight
        : (1.0f - std::exp(-scaledHeight)) * (length / heightDelta);

    const glm::vec3 rayleigh = rayleighCoefficient * (rayleighScaleHeight * airMass * columnFactor);
    const float mie = mieOpticalDepth(airMass, parameters) * columnFactor;
    const glm::vec3 result = rayleigh + glm::vec3(mie);
    // An upward ray through a tall column can overflow the exponential; clamping keeps a caller's
    // `exp(-depth)` at zero rather than at a NaN.
    return glm::max(result, glm::vec3(0.0f));
}

glm::vec3 verticalOpticalDepth(const AtmosphereParameters& parameters) {
    // A sun exactly at the zenith is the one configuration where `sunTransmittance` already *is* the
    // vertical column, so the column is defined by evaluating it there rather than by a second
    // implementation of the same sum.
    AtmosphereParameters overhead = parameters;
    overhead.enabled = true;
    overhead.sunElevationDegrees = 90.0f;
    overhead.sunAzimuthDegrees = 0.0f;
    const glm::vec3 transmittance = sunTransmittance(overhead);
    return glm::max(-glm::log(glm::max(transmittance, glm::vec3(1.0e-30f))), glm::vec3(0.0f));
}

glm::vec3 skyLightColor(const AtmosphereParameters& parameters) {
    if (!parameters.enabled) return glm::vec3(1.0f);
    const glm::vec3 transmittance = sunTransmittance(parameters);
    const float brightest = std::max(transmittance.r, std::max(transmittance.g, transmittance.b));
    // An atmosphere that has extinguished the sun completely keeps a neutral light: the key
    // light's energy is already carried by `sunTransmittance`'s luminance, so dividing by a
    // vanishing channel would only amplify float noise into a saturated colour.
    if (!std::isfinite(brightest) || brightest <= 1.0e-6f) return glm::vec3(1.0f);
    return glm::min(transmittance / brightest, glm::vec3(1.0f));
}

glm::vec3 sunDiskRadiance(const glm::vec3& viewDirection, const AtmosphereParameters& parameters) {
    if (!parameters.enabled) return glm::vec3(0.0f);
    glm::vec3 direction;
    if (!normaliseDirection(viewDirection, direction)) return glm::vec3(0.0f);
    const glm::vec3 sun = sunDirection(parameters);
    const float cosAngle = glm::dot(direction, sun);
    const float cosRadius = std::cos(glm::radians(sunAngularRadius));
    if (cosAngle < cosRadius) return glm::vec3(0.0f);
    // Soft edge over the last tenth of the disk so the boundary is not a hard pixel step.
    const float edge = saturate((cosAngle - cosRadius) / std::max(1.0f - cosRadius, 1.0e-5f) * 10.0f);
    return sunTransmittance(parameters) * sunDiskRadianceScale
        * std::max(parameters.sunIntensity, 0.0f) * edge;
}

glm::vec3 skyRadiance(const glm::vec3& viewDirection, const AtmosphereParameters& parameters) {
    if (!parameters.enabled) return glm::vec3(0.0f);
    glm::vec3 direction;
    if (!normaliseDirection(viewDirection, direction)) return glm::vec3(0.0f);

    if (direction.y < 0.0f) {
        // Ground hemisphere: what a Lambertian ground of `groundAlbedo` reflects, which is
        // `albedo/pi * (E_sky + E_sun)`. Both terms are needed -- the sky alone leaves the lower
        // hemisphere far darker than the ground the key light lights, which reads as a dark band
        // under the horizon in any frame that sees past the ground geometry.
        const glm::vec3 irradianceOverPi = skyIrradianceOverPi(parameters)
            + sunIrradiance(parameters) * (std::max(sunDirection(parameters).y, 0.0f) / pi);
        const float fillT = parameters.nightSkyEnabled
            ? saturate((10.0f - parameters.sunElevationDegrees) / 18.0f) : 0.0f;
        const float twilightFill = fillT * fillT * (3.0f - 2.0f * fillT);
        return (irradianceOverPi + glm::vec3(0.040f, 0.060f, 0.110f)
            * twilightFill) * std::clamp(parameters.groundAlbedo, 0.0f, 1.0f);
    }
    return scatteringRadiance(direction, parameters) + nightRadiance(direction, parameters);
}

float sunAngularRadiusDegrees() {
    return sunAngularRadius;
}

glm::vec3 sunIrradiance(const AtmosphereParameters& parameters) {
    if (!parameters.enabled) return glm::vec3(0.0f);
    const float cosRadius = std::cos(glm::radians(sunAngularRadius));
    const float solidAngle = 2.0f * pi * (1.0f - cosRadius);
    // The disk's radiance is uniform inside `sunAngularRadius`, so the irradiance on a surface
    // facing it is exactly radiance times solid angle.
    return sunDiskRadiance(sunDirection(parameters), parameters) * solidAngle;
}

std::vector<glm::vec3> generateEquirect(
    const AtmosphereParameters& parameters,
    int width,
    int height
) {
    std::vector<glm::vec3> pixels;
    if (width <= 0 || height <= 0) return pixels;
    pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    // The lower hemisphere is one view-independent value, so it is resolved once rather than per
    // texel: the ground integral costs nine scattering evaluations.
    const glm::vec3 ground = skyRadiance(glm::vec3(0.0f, -1.0f, 0.0f), parameters);
    for (int y = 0; y < height; ++y) {
        // Row 0 is the +Y pole, matching the file loader's convention.
        const float theta = pi * (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);
        for (int x = 0; x < width; ++x) {
            const float phi = 2.0f * pi * (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
            const glm::vec3 direction(
                sinTheta * std::sin(phi),
                cosTheta,
                sinTheta * std::cos(phi)
            );
            // A ground texel cannot see the sun disk: the ground occludes it.
            pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                   + static_cast<std::size_t>(x)] = direction.y < 0.0f
                ? ground
                : skyRadiance(direction, parameters)
                    + sunDiskRadiance(direction, parameters);
        }
    }
    return pixels;
}

} // namespace atmosphere
