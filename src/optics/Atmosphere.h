#pragma once

#include <vector>

#include <glm/vec3.hpp>

// Analytic single-scattering sky shared by every consumer of the environment: the raster
// skybox cubemap, its irradiance / prefiltered mips, the CPU path tracer's environment
// sampling and the headless batch runtime all evaluate this one model, so a rendered sky
// and a traced sky cannot disagree.
//
// Model and its limits (deliberate, documented rather than implied):
//   - Rayleigh and Mie single scattering with a closed-form exponential-atmosphere integral,
//     `beta * H * m * (1 - exp(-tau)) / tau`, where `m` is the Kasten-Young relative air mass
//     and `tau` the air mass scaled vertical optical depth. No numerical march, so the
//     evaluator stays cheap enough to rebuild environment cubemaps.
//   - The sun's transmittance is evaluated at the ground and treated as constant along the
//     view ray. That is the standard practical approximation and keeps the sky blue overhead
//     and red at sunset.
//   - No multiple scattering and no ozone layer: the zenith is therefore darker than reality
//     during twilight and the deep-blue twilight band is missing. Both are recorded as
//     follow-ups in `docs/atmosphere-sky.md` rather than faked.
//   - Directions below the horizon return the radiance a Lambertian ground of `groundAlbedo`
//     reflects: the cosine-weighted sky irradiance plus the direct sun, so the environment's
//     lower hemisphere carries the same sun the key light does. They never return NaN.

namespace atmosphere {

struct AtmosphereParameters {
    bool enabled{false};
    // Sun position in degrees: elevation above the horizon, azimuth measured from +Z towards +X.
    float sunElevationDegrees{35.0f};
    float sunAzimuthDegrees{135.0f};
    // Mie (aerosol) multiplier on the sea-level coefficient 21e-6 /m. 1 is a clean,
    // ~20-30 km visibility sky; larger values are hazier and grey out the zenith.
    float turbidity{1.0f};
    float skyIntensity{1.0f};
    float sunIntensity{1.0f};
    float groundAlbedo{0.10f};
    // Optional artistic night sky. Off for existing scenes; the moon follows a repeatable
    // presentation orbit driven by the sun, not a calendar-based ephemeris.
    bool nightSkyEnabled{false};
    float moonIntensity{1.0f};
    float starIntensity{1.0f};
    // Aerial perspective: the air between the camera and the geometry, integrated with the same
    // coefficients as the sky (`opticalDepthAlongSegment`). Off by default so a scene that never
    // asked for it keeps exactly the pixels it had.
    bool aerialPerspectiveEnabled{false};
    float aerialPerspectiveStrength{1.0f};
    // Scale height of the density profile, expressed in *world* units so a scene can tune the
    // effect without knowing the model's metre convention: the total atmospheric column is
    // `scaleHeight * worldsPerMetre` metres. Distances beyond a few scale heights stop fading.
    float aerialPerspectiveScaleHeight{60.0f};
};

// Unit vector pointing from the scene towards the sun. The raster directional light travels
// along `-sunDirection()`, which is what `RendererSettings::lightDirection` stores.
glm::vec3 sunDirection(const AtmosphereParameters& parameters);
glm::vec3 moonDirection(const AtmosphereParameters& parameters);
float nightVisibility(const AtmosphereParameters& parameters);
float moonKeyStrength(const AtmosphereParameters& parameters);

// Whether two parameter sets are the same *in a render*. Evaluating the sky is expensive -- an
// environment rebuild is a few hundred milliseconds, an equirectangular radiance map is tens -- so
// callers cache the result and only pay for it when the sun moved far enough to be visible or a
// parameter moved by more than float noise. Keeping the tolerances here means the raster
// environment, the CPU path tracer's sky and any future consumer agree on what "changed" means
// instead of each inventing its own threshold.
bool parametersMatch(const AtmosphereParameters& a, const AtmosphereParameters& b);

// Spectral radiance of the sky in `viewDirection` (unit, +Y up). Finite and non-negative for
// every input, including a sun below the horizon.
glm::vec3 skyRadiance(const glm::vec3& viewDirection, const AtmosphereParameters& parameters);

// Colour of the direct sun after atmospheric extinction, before `sunIntensity` is applied.
// Intended as the directional light colour so the light and the sky stay consistent.
glm::vec3 sunTransmittance(const AtmosphereParameters& parameters);

// Optical depth of the whole vertical column, per RGB channel: what a ray from the ground straight
// up accumulates. This is the scale aerial perspective is expressed in -- a ray walking
// `length / scaleHeight` scale heights horizontally accumulates one whole column -- and it is
// `-log(sunTransmittance())` for a sun at the zenith.
glm::vec3 verticalOpticalDepth(const AtmosphereParameters& parameters);

// `sunTransmittance` normalised so its brightest channel is 1: the *spectrum* of the key light
// with no brightness left in it. Brightness and colour are separate concerns because the raster
// key light scales the existing `diffuseStrength` / `specularStrength` by the transmittance
// luminance; multiplying by the raw transmittance as well would apply the extinction twice. A
// disabled (or fully extinguished) atmosphere returns white, so a scene without a sky keeps the
// neutral light it has always had.
glm::vec3 skyLightColor(const AtmosphereParameters& parameters);

// Radiance of the sun disk for the given view direction, already scaled by intensity. Kept
// separate from `skyRadiance` so callers that only want the atmosphere can skip the disk.
glm::vec3 sunDiskRadiance(const glm::vec3& viewDirection, const AtmosphereParameters& parameters);

// Angular radius of the rendered sun disk in degrees. Slightly wider than the real 0.265 deg
// so the disk is not lost at environment cubemap resolutions.
float sunAngularRadiusDegrees();

// Irradiance a surface facing the sun receives, in the same units as `skyRadiance`. The disk's
// radiance scale is chosen so the clear-sky ratio `E_sun / E_sky` is about 10, which is what
// makes the environment's ground hemisphere and its specular reflections see the same sun the
// analytic key light does.
glm::vec3 sunIrradiance(const AtmosphereParameters& parameters);

// Optical depth between two points, per RGB channel, for a ray of `segmentLength` world units whose
// direction has a vertical component of `directionY`. `worldUnitsPerMetre` converts the scene's
// world units into the model's metres, so the coefficients and the scale height keep their physical
// meaning while a scene stays free to use any unit scale.
//
// The vertical density profile is `rho(y) = exp(-(y - eyeY) / scaleHeight)`, so the column a ray
// accumulates is `airMass * (1 - exp(-heightDelta / scaleHeight))` times the constituent's own
// vertical depth, where the height difference along the segment is `segmentLength * directionY`.
// A horizontal ray therefore accumulates one scale height's worth of column per scale height
// travelled, a ray pointing up saturates on the full vertical column, and a segment of zero length
// is transparent. Finite and non-negative for every input, including a zero direction component.
glm::vec3 opticalDepthAlongSegment(
    const AtmosphereParameters& parameters,
    float segmentLength,
    float directionY,
    float worldUnitsPerMetre
);

// Deterministic equirectangular sampling of `skyRadiance` plus the sun disk: row 0 is the
// +Y pole and column 0 looks along +Z, matching the HDR loader used for file environments.
std::vector<glm::vec3> generateEquirect(
    const AtmosphereParameters& parameters,
    int width,
    int height
);

} // namespace atmosphere
