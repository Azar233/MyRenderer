#include "optics/Atmosphere.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string describe(const glm::vec3& value) {
    return "(" + std::to_string(value.x) + ", " + std::to_string(value.y) + ", "
        + std::to_string(value.z) + ")";
}

void requireColour(bool condition, const char* message, const glm::vec3& value) {
    if (!condition) throw std::runtime_error(std::string(message) + ": " + describe(value));
}

bool allFiniteNonNegative(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z)
        && value.x >= 0.0f && value.y >= 0.0f && value.z >= 0.0f;
}

// A representative set of view directions: zenith, horizon and below the horizon, at eight
// azimuths, so a model change cannot hide an artefact in one quadrant.
std::vector<glm::vec3> sweepDirections() {
    std::vector<glm::vec3> directions;
    for (float elevation : {90.0f, 60.0f, 30.0f, 10.0f, 1.0f, -1.0f, -30.0f, -90.0f}) {
        for (int azimuth = 0; azimuth < 8; ++azimuth) {
            const float elevationRadians = glm::radians(elevation);
            const float azimuthRadians = glm::radians(azimuth * 45.0f);
            directions.push_back(glm::vec3(
                std::cos(elevationRadians) * std::sin(azimuthRadians),
                std::sin(elevationRadians),
                std::cos(elevationRadians) * std::cos(azimuthRadians)
            ));
        }
    }
    return directions;
}

void requireFiniteEverywhere(const atmosphere::AtmosphereParameters& parameters, const char* context) {
    for (const glm::vec3& direction : sweepDirections()) {
        require(allFiniteNonNegative(atmosphere::skyRadiance(direction, parameters)), context);
        require(allFiniteNonNegative(
                    atmosphere::sunDiskRadiance(direction, parameters)
                        + atmosphere::skyRadiance(direction, parameters)
                ),
                context);
    }
}

} // namespace

int main() {
    try {
        atmosphere::AtmosphereParameters parameters;
        require(!parameters.enabled, "the atmosphere must be off by default");
        require(allFiniteNonNegative(atmosphere::skyRadiance(glm::vec3(0.0f, 1.0f, 0.0f), parameters)),
                "a disabled atmosphere must still return a finite sky");
        require(glm::length(atmosphere::skyRadiance(glm::vec3(0.0f, 1.0f, 0.0f), parameters)) == 0.0f,
                "a disabled atmosphere must return black");

        parameters.enabled = true;

        // The sun direction is the single input the sky, the light and the disk share.
        parameters.sunElevationDegrees = 45.0f;
        parameters.sunAzimuthDegrees = 90.0f;
        const glm::vec3 sun = atmosphere::sunDirection(parameters);
        require(std::abs(glm::length(sun) - 1.0f) < 1.0e-5f, "the sun direction must be a unit vector");
        require(std::abs(sun.x - std::cos(glm::radians(45.0f))) < 1.0e-5f,
                "azimuth 90 must place the sun on +X");
        require(std::abs(sun.y - std::sin(glm::radians(45.0f))) < 1.0e-5f,
                "elevation must control the sun height");
        require(std::abs(sun.z) < 1.0e-5f, "azimuth 90 must leave no +Z component");

        // Overhead sun: the zenith must be blue, and much brighter than the sky opposite the
        // horizon, which is the defining shape of a daytime Rayleigh sky.
        parameters.sunElevationDegrees = 70.0f;
        const glm::vec3 noonZenith = atmosphere::skyRadiance(glm::vec3(0.0f, 1.0f, 0.0f), parameters);
        require(allFiniteNonNegative(noonZenith), "the noon zenith must be finite and non-negative");
        requireColour(noonZenith.b > noonZenith.r * 1.5f, "the noon zenith must be blue", noonZenith);
        // The phase functions must brighten the sky towards the sun: that sun-side brightening is
        // what a coastal Hero Scene reads as daylight, and it is what this model guarantees. It
        // deliberately does *not* reproduce the real clear-sky fact that the anti-solar horizon is
        // the darkest part of the sky -- single scattering lacks the multiple scattering and
        // density profile that cause it. That limitation is recorded in docs/atmosphere-sky.md.
        const glm::vec3 noonSunDirection = atmosphere::sunDirection(parameters);
        const glm::vec3 towardSunHorizon = atmosphere::skyRadiance(
            glm::normalize(glm::vec3(noonSunDirection.x, 0.08f, noonSunDirection.z)), parameters
        );
        const glm::vec3 antiSunHorizon = atmosphere::skyRadiance(
            glm::normalize(glm::vec3(-noonSunDirection.x, 0.08f, -noonSunDirection.z)), parameters
        );
        require(glm::length(towardSunHorizon) > glm::length(antiSunHorizon) * 1.2f,
                "the sky towards the sun must be brighter than the anti-solar sky");
        require(allFiniteNonNegative(towardSunHorizon) && allFiniteNonNegative(antiSunHorizon),
                "both horizons must be finite");

        // Low sun: the sky around the sun reddens, which is the sunset signal the light colour
        // and the aerial perspective both depend on.
        atmosphere::AtmosphereParameters sunset = parameters;
        sunset.sunElevationDegrees = 2.0f;
        const glm::vec3 sunsetNearSun = atmosphere::skyRadiance(
            atmosphere::sunDirection(sunset), sunset
        );
        require(allFiniteNonNegative(sunsetNearSun), "the sunset horizon must be finite");
        require(sunsetNearSun.r > sunsetNearSun.b * 1.5f,
                "the sky towards a low sun must be red");
        const glm::vec3 noonNearSun = atmosphere::skyRadiance(
            atmosphere::sunDirection(parameters), parameters
        );
        require(noonNearSun.b > sunsetNearSun.b * 0.5f,
                "a high sun must not be as red as a low one");

        // Sun transmittance drives the directional light colour: dimmer and redder as the sun
        // sets, and never brighter than unity.
        const glm::vec3 noonSun = atmosphere::sunTransmittance(parameters);
        const glm::vec3 sunsetSun = atmosphere::sunTransmittance(sunset);
        require(allFiniteNonNegative(noonSun) && allFiniteNonNegative(sunsetSun),
                "sun transmittance must be finite");
        require(noonSun.r <= 1.0f && noonSun.b <= 1.0f, "sun transmittance must not exceed unity");
        require(glm::length(noonSun) > glm::length(sunsetSun),
                "the sun must lose energy as it sets");
        require(sunsetSun.r > sunsetSun.b, "a setting sun must redden");

        // The key light's colour is that same transmittance with its brightness divided out, so a
        // setting sun reddens the light instead of only dimming it. Brightness stays in
        // `sunTransmittance`'s luminance, which is why the two are separate: multiplying the raw
        // transmittance into the light would apply the extinction twice.
        const glm::vec3 noonColour = atmosphere::skyLightColor(parameters);
        const glm::vec3 sunsetColour = atmosphere::skyLightColor(sunset);
        require(allFiniteNonNegative(noonColour) && allFiniteNonNegative(sunsetColour),
                "the key light colour must be finite and non-negative");
        const float noonPeak = std::max(noonColour.r, std::max(noonColour.g, noonColour.b));
        const float sunsetPeak = std::max(sunsetColour.r, std::max(sunsetColour.g, sunsetColour.b));
        require(std::abs(noonPeak - 1.0f) < 1.0e-5f && std::abs(sunsetPeak - 1.0f) < 1.0e-5f,
                "the key light colour must be normalised to its brightest channel");
        require(sunsetColour.r > sunsetColour.b * 1.5f,
                "a setting sun must redden the key light, not only dim it");
        // Red is the peak channel on both sides, so the warmth is a ratio, not a raw component:
        // every other channel has to give way relative to red as the sun drops.
        require(sunsetColour.g / sunsetColour.r < noonColour.g / noonColour.r
                    && sunsetColour.b / sunsetColour.r < noonColour.b / noonColour.r,
                "the sun must be redder near the horizon than overhead");
        // The light is never *blue*: these coefficients are tuned well above the physical Rayleigh
        // values so the sky reads as blue without a numeric march, which leaves blue the most
        // attenuated channel even with the sun overhead. The contract locked in here is the
        // direction of the change -- blue transmission recovers monotonically as the sun climbs --
        // not a claim that a high sun is spectrally white. `docs/atmosphere-sky.md` records it.
        float previousBlue = 0.0f;
        for (float elevation : {2.0f, 20.0f, 45.0f, 90.0f}) {
            atmosphere::AtmosphereParameters sweep = parameters;
            sweep.sunElevationDegrees = elevation;
            const glm::vec3 colour = atmosphere::skyLightColor(sweep);
            require(colour.b > previousBlue,
                    "blue transmission must recover as the sun rises");
            require(colour.r >= colour.b,
                    "the key light must never turn bluer than it is red");
            previousBlue = colour.b;
        }
        const glm::vec3 white{1.0f};
        require(atmosphere::skyLightColor(atmosphere::AtmosphereParameters{}) == white,
                "a disabled atmosphere must leave the key light neutral");
        atmosphere::AtmosphereParameters extinguished = parameters;
        extinguished.sunElevationDegrees = -10.0f;
        require(atmosphere::skyLightColor(extinguished) == white
                    || allFiniteNonNegative(atmosphere::skyLightColor(extinguished)),
                "a sun far below the horizon must not turn the light into NaN or a negative colour");
        require(atmosphere::skyLightColor(parameters) == noonColour,
                "the key light colour must be deterministic");

        // Aerial perspective integrates the same coefficients over a finite segment instead of to
        // the top of the atmosphere, so it has to agree with the sky at both limits: zero length is
        // transparent, and a segment long enough to escape saturates on the transmittance the key
        // light already uses.
        const float segmentLength = 420.0f;
        // One world unit is one metre here, so the numbers below are directly comparable with the
        // model's own metre-scale coefficients.
        const float testWorldUnitsPerMetre = 1.0f;
        const glm::vec3 horizontalDepth = atmosphere::opticalDepthAlongSegment(
            parameters, segmentLength, 0.0f, testWorldUnitsPerMetre
        );
        require(allFiniteNonNegative(horizontalDepth) && glm::length(horizontalDepth) > 0.0f,
                "a horizontal segment must accumulate a finite non-zero optical depth");
        require(glm::length(atmosphere::opticalDepthAlongSegment(
                    parameters, 0.0f, -1.0f, testWorldUnitsPerMetre
                )) == 0.0f,
                "a zero-length segment must be transparent");
        require(glm::length(atmosphere::opticalDepthAlongSegment(
                    parameters, -5.0f, -1.0f, testWorldUnitsPerMetre
                )) == 0.0f,
                "a negative segment must not accumulate negative optical depth");
        const glm::vec3 nearDepth = atmosphere::opticalDepthAlongSegment(
            parameters, segmentLength, -1.0f, testWorldUnitsPerMetre
        );
        const glm::vec3 farDepth = atmosphere::opticalDepthAlongSegment(
            parameters, segmentLength * 1000.0f, -1.0f, testWorldUnitsPerMetre
        );
        require(glm::length(farDepth) > glm::length(nearDepth),
                "a longer segment must accumulate more optical depth");
        // A scene is free to model its own unit scale; the optical depth must follow the physical
        // distance, so one unit standing for ten metres has to thin the air tenfold.
        const glm::vec3 scaledDepth = atmosphere::opticalDepthAlongSegment(
            parameters, segmentLength, -1.0f, 10.0f
        );
        require(glm::length(scaledDepth * 10.0f - nearDepth) < glm::length(nearDepth) * 0.2f,
                "the unit scale must convert world units into metres");
        // Looking straight up, the column above the camera is the whole atmosphere, so the depth has
        // to saturate instead of growing without bound -- and it must stop at a property of the
        // atmosphere, not of where the sun happens to be.
        const glm::vec3 upwardDepth = atmosphere::opticalDepthAlongSegment(
            parameters, 1.0e7f, 1.0f, testWorldUnitsPerMetre
        );
        atmosphere::AtmosphereParameters elsewhere = parameters;
        elsewhere.sunElevationDegrees = 30.0f;
        require(glm::length(atmosphere::opticalDepthAlongSegment(
                    elsewhere, 1.0e7f, 1.0f, testWorldUnitsPerMetre
                ) - upwardDepth) == 0.0f,
                "the vertical column must not depend on the sun's elevation");
        // Cross-check the saturation against the model's own column. `sunTransmittance` is evaluated
        // at the *sun's* zenith angle, so undoing it needs that angle's air mass rather than the
        // vertical ray's: the Kasten-Young fit gives 1.0637 at the sun's 20 deg zenith and 0.99971
        // for a perfectly vertical ray, and the column factor is 1 for a ray that escapes. What the
        // residual is allowed to contain is only that air-mass convention, nothing else.
        const glm::vec3 atmosphereColumn = glm::vec3(
            -glm::log(atmosphere::sunTransmittance(parameters))
        ) / 1.0636999870686308f;
        require(glm::length(upwardDepth - atmosphereColumn)
                    < glm::length(atmosphereColumn) * 0.01f,
                "an escaping upward segment must saturate on the full atmospheric column");
        // The whole vertical column is also its own public query, because aerial perspective is
        // expressed in units of it.
        require(glm::length(atmosphere::verticalOpticalDepth(parameters) - upwardDepth)
                    < glm::length(upwardDepth) * 0.01f,
                "verticalOpticalDepth must agree with an escaping vertical segment");
        require(glm::length(atmosphere::verticalOpticalDepth(parameters)) > 0.0f,
                "the vertical column must not be empty");
        atmosphere::AtmosphereParameters denser = parameters;
        denser.turbidity = 4.0f;
        require(allFiniteNonNegative(atmosphere::verticalOpticalDepth(denser))
                    && glm::length(atmosphere::verticalOpticalDepth(denser))
                        > glm::length(atmosphere::verticalOpticalDepth(parameters)),
                "more aerosol must deepen the vertical column");
        atmosphere::AtmosphereParameters thicker = parameters;
        thicker.skyIntensity = 5.0f;
        require(glm::length(atmosphere::verticalOpticalDepth(thicker)
                    - atmosphere::verticalOpticalDepth(parameters)) == 0.0f,
                "the vertical column must not depend on sky intensity");
        atmosphere::AtmosphereParameters dense = parameters;
        dense.turbidity = 4.0f;
        require(glm::length(atmosphere::opticalDepthAlongSegment(
                    dense, segmentLength, 0.1f, testWorldUnitsPerMetre
                )) > glm::length(atmosphere::opticalDepthAlongSegment(
                    parameters, segmentLength, 0.1f, testWorldUnitsPerMetre
                )),
                "more aerosol must thicken a horizontal segment");
        require(allFiniteNonNegative(atmosphere::opticalDepthAlongSegment(
                    parameters, 1.0e9f, 0.0f, testWorldUnitsPerMetre
                )),
                "an enormous horizontal segment must stay finite");
        require(allFiniteNonNegative(atmosphere::opticalDepthAlongSegment(
                    parameters, 1.0e9f, 1.0e-9f
                , testWorldUnitsPerMetre)),
                "a nearly horizontal segment must not divide by zero");

        // The sun disk is the brightest feature and only covers its own angular radius.
        const glm::vec3 disk = atmosphere::sunDiskRadiance(noonSunDirection, parameters);
        require(glm::length(disk) > glm::length(noonZenith) * 100.0f,
                "the sun disk must dominate the sky radiance");
        require(atmosphere::sunAngularRadiusDegrees() > 0.265f,
                "the rendered disk must be at least as wide as the real sun");
        const glm::vec3 offDisk = atmosphere::sunDiskRadiance(
            glm::normalize(noonSunDirection + glm::vec3(0.0f, 0.2f, 0.0f)), parameters
        );
        require(glm::length(offDisk) == 0.0f, "the disk must not leak outside its radius");

        // The disk's absolute scale is pinned to the clear-day illuminance ratio, because the
        // environment's ground hemisphere and its prefiltered specular both integrate it: a token
        // sun there leaves the lower hemisphere darker than the ground the key light lights.
        const glm::vec3 sunIrradiance = atmosphere::sunIrradiance(parameters);
        require(allFiniteNonNegative(sunIrradiance), "sun irradiance must be finite");
        // E_sky is pi times the cosine-weighted average sky radiance. A uniform sky at the noon
        // zenith's brightness is a close enough reference for a ratio guard.
        const float skyIrradiance = 3.14159265358979323846f * glm::length(noonZenith);
        const float sunToSky = glm::length(sunIrradiance) / std::max(skyIrradiance, 1.0e-6f);
        require(sunToSky > 2.0f && sunToSky < 60.0f,
                "the sun must dominate the sky irradiance by a daylight ratio");
        atmosphere::AtmosphereParameters brighterSun = parameters;
        brighterSun.sunIntensity = 4.0f;
        require(glm::length(atmosphere::sunIrradiance(brighterSun))
                    > glm::length(sunIrradiance) * 3.5f,
                "sun irradiance must scale with sun intensity");
        atmosphere::AtmosphereParameters disabledAtmosphere;
        require(glm::length(atmosphere::sunIrradiance(disabledAtmosphere)) == 0.0f,
                "a disabled atmosphere must radiate nothing");

        // The ground hemisphere is what a Lambertian ground reflects, so it must carry the sun as
        // well as the sky, and it must stay finite with the sun below the horizon.
        const glm::vec3 ground = atmosphere::skyRadiance(glm::vec3(0.0f, -1.0f, 0.0f), parameters);
        require(allFiniteNonNegative(ground), "the ground hemisphere must be finite");
        atmosphere::AtmosphereParameters unlitSun = parameters;
        unlitSun.sunIntensity = 0.0f;
        require(glm::length(ground)
                    > glm::length(atmosphere::skyRadiance(glm::vec3(0.0f, -1.0f, 0.0f), unlitSun)),
                "the ground must reflect the direct sun, not only the sky");
        atmosphere::AtmosphereParameters belowHorizon = parameters;
        belowHorizon.sunElevationDegrees = -8.0f;
        require(allFiniteNonNegative(
                    atmosphere::skyRadiance(glm::vec3(0.0f, -1.0f, 0.0f), belowHorizon)
                ),
                "a sun below the horizon must not make the ground negative or NaN");

        // Rotating view and sun together must not change the sky: only the angle between them
        // and the view elevation matter.
        atmosphere::AtmosphereParameters rotated = parameters;
        rotated.sunAzimuthDegrees = parameters.sunAzimuthDegrees + 47.0f;
        const glm::vec3 view = glm::normalize(glm::vec3(0.4f, 0.35f, 0.85f));
        // sunDirection() measures azimuth from +Z towards +X, so the matching rotation about Y is
        // x' = x cos + z sin, z' = -x sin + z cos.
        const float rotatedAzimuth = glm::radians(47.0f);
        const glm::vec3 rotatedView(
            view.x * std::cos(rotatedAzimuth) + view.z * std::sin(rotatedAzimuth),
            view.y,
            -view.x * std::sin(rotatedAzimuth) + view.z * std::cos(rotatedAzimuth)
        );
        const glm::vec3 base = atmosphere::skyRadiance(view, parameters);
        const glm::vec3 rotatedSky = atmosphere::skyRadiance(rotatedView, rotated);
        require(glm::length(base - rotatedSky) < glm::length(base) * 1.0e-4f,
                "the sky must depend only on the angle to the sun, not on absolute azimuth");

        // Hazier air scatters more and flattens the colour contrast.
        atmosphere::AtmosphereParameters hazy = parameters;
        hazy.turbidity = 6.0f;
        require(glm::length(atmosphere::skyRadiance(glm::vec3(0.0f, 1.0f, 0.0f), hazy))
                    > glm::length(noonZenith),
                "more aerosol must brighten the sky");

        // Degenerate and hostile inputs must stay finite: sun below the horizon, below-zero
        // intensity, zenith view exactly at the horizon and an unnormalised direction.
        for (float elevation : {-90.0f, -30.0f, -1.0f, 0.0f, 1.0f, 89.9f, 90.0f}) {
            atmosphere::AtmosphereParameters extreme = parameters;
            extreme.sunElevationDegrees = elevation;
            requireFiniteEverywhere(extreme, "every sun elevation must produce a finite sky");
        }
        atmosphere::AtmosphereParameters zeroIntensity = parameters;
        zeroIntensity.skyIntensity = 0.0f;
        zeroIntensity.sunIntensity = 0.0f;
        requireFiniteEverywhere(zeroIntensity, "zero intensity must stay finite");
        atmosphere::AtmosphereParameters negative = parameters;
        negative.skyIntensity = -5.0f;
        negative.sunIntensity = -2.0f;
        negative.turbidity = -1.0f;
        negative.groundAlbedo = -1.0f;
        requireFiniteEverywhere(negative, "negative parameters must clamp instead of returning NaN");
        require(allFiniteNonNegative(atmosphere::skyRadiance(glm::vec3(0.0f), parameters)),
                "a zero view direction must not divide by zero");
        require(allFiniteNonNegative(
                    atmosphere::skyRadiance(glm::vec3(1.0e6f, 1.0f, -1.0e6f), parameters)
                ),
                "an unnormalised view direction must be handled");

        // Determinism: the same parameters must produce bitwise identical radiance.
        const glm::vec3 again = atmosphere::skyRadiance(view, parameters);
        require(base.x == again.x && base.y == again.y && base.z == again.z,
                "the model must be deterministic");

        // Equirectangular generation follows the environment loader's orientation: row 0 is the
        // +Y pole, column 0 looks along +Z, and azimuth grows from +Z towards +X. The sun disk is
        // only ~1.2 degrees wide, so the grid has to be fine enough to contain it at all -- at
        // 64x32 the disk falls between pixel centres, which is worth knowing before rendering a
        // sky from a low-resolution map.
        parameters.sunElevationDegrees = 45.0f;
        parameters.sunAzimuthDegrees = 90.0f;
        const int width = 512;
        const int height = 256;
        const std::vector<glm::vec3> equirect = atmosphere::generateEquirect(parameters, width, height);
        require(equirect.size() == static_cast<std::size_t>(width) * height,
                "the generated environment must cover the whole image");
        for (const glm::vec3& pixel : equirect) {
            require(allFiniteNonNegative(pixel), "every generated pixel must be finite and non-negative");
        }
        std::size_t brightest = 0U;
        for (std::size_t index = 1U; index < equirect.size(); ++index) {
            if (glm::length(equirect[index]) > glm::length(equirect[brightest])) brightest = index;
        }
        const int brightestRow = static_cast<int>(brightest / static_cast<std::size_t>(width));
        const int brightestColumn = static_cast<int>(brightest % static_cast<std::size_t>(width));
        const int expectedRow = static_cast<int>((45.0f / 180.0f) * static_cast<float>(height));
        const int expectedColumn = static_cast<int>((90.0f / 360.0f) * static_cast<float>(width));
        require(std::abs(brightestRow - expectedRow) <= 1,
                "the sun disk must sit at the requested elevation");
        require(std::abs(brightestColumn - expectedColumn) <= 1,
                "the sun disk must sit at the requested azimuth");
        const glm::vec3 zenithPixel = equirect[static_cast<std::size_t>(width) / 2U];
        require(glm::length(equirect[brightest]) > glm::length(zenithPixel) * 10.0f,
                "the sun must dominate the sky it is rendered into");
        require(atmosphere::generateEquirect(parameters, width, height) == equirect,
                "equirectangular generation must be deterministic");
        require(atmosphere::generateEquirect(parameters, 0, 0).empty(),
                "an empty request must produce no pixels");
        require(atmosphere::generateEquirect(parameters, 64, 32).size() == 64U * 32U,
                "a coarse request must still produce a full image");
        std::cout << "Atmosphere model tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Atmosphere model tests failed: " << error.what() << '\n';
        return 1;
    }
}
