#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "optics/PrismOptics.h"
#include "optics/PrismDemo.h"
#include "optics/SpectralBeamMesh.h"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool finite(const glm::vec2& value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

float angularDifference(const glm::vec2& a, const glm::vec2& b) {
    const float cosine = glm::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f);
    return std::acos(cosine);
}

} // namespace

int main() {
    try {
        const std::array<glm::vec2, 3> triangle{
            glm::vec2(-0.7f, -0.48125f),
            glm::vec2(0.7f, -0.48125f),
            glm::vec2(0.0f, 0.74375f)
        };

        const PrismRay2D heroRay{
            glm::vec2(-2.4f, -0.15f),
            glm::normalize(glm::vec2(-0.39f, 0.12f) - glm::vec2(-2.4f, -0.15f))
        };
        const PrismOpticalPath heroPath = traceTriangularPrism(triangle, heroRay, 1.52f);
        require(heroPath.valid, "hero ray should cross both prism interfaces");
        require(!heroPath.totalInternalReflection, "hero ray should leave crown glass");
        require(heroPath.entryPoint.x < 0.0f, "hero ray should enter through the left face");
        require(finite(heroPath.exitDirection), "hero exit direction should remain finite");
        require(
            heroPath.totalTransmittance > 0.0f && heroPath.totalTransmittance <= 1.0f,
            "Fresnel transmission should remain normalized"
        );

        const PrismRay2D normalEntryRay{
            glm::vec2(0.3f, -2.0f),
            glm::vec2(0.0f, 1.0f)
        };
        const PrismOpticalPath normalEntry = traceTriangularPrism(triangle, normalEntryRay, 1.52f);
        require(normalEntry.valid, "normal-entry ray should cross the prism");
        require(
            angularDifference(normalEntry.incidentDirection, normalEntry.internalDirection) < 1.0e-4f,
            "normal incidence should not bend at the entry interface"
        );

        const PrismOpticalPath repeated = traceTriangularPrism(triangle, heroRay, 1.52f);
        require(repeated.valid, "repeated trace should remain valid");
        require(
            glm::length(repeated.exitPoint - heroPath.exitPoint) < 1.0e-6f
                && glm::length(repeated.exitDirection - heroPath.exitDirection) < 1.0e-6f,
            "identical IOR inputs should produce identical paths"
        );

        const PrismOpticalPath lowerIor = traceTriangularPrism(triangle, heroRay, 1.33f);
        const PrismOpticalPath higherIor = traceTriangularPrism(triangle, heroRay, 1.72f);
        require(lowerIor.valid && higherIor.valid, "comparison IOR paths should remain valid");
        require(
            angularDifference(lowerIor.exitDirection, higherIor.exitDirection) > 1.0e-3f,
            "different IOR values should separate outgoing directions"
        );

        bool foundTotalInternalReflection = false;
        for (int sample = -12; sample <= 12 && !foundTotalInternalReflection; ++sample) {
            const float targetY = static_cast<float>(sample) * 0.045f;
            const PrismRay2D candidate{
                glm::vec2(-2.0f, targetY - 0.25f),
                glm::normalize(glm::vec2(-0.45f, targetY) - glm::vec2(-2.0f, targetY - 0.25f))
            };
            const PrismOpticalPath path = traceTriangularPrism(triangle, candidate, 2.4f);
            if (path.valid) {
                require(finite(path.internalDirection), "high-IOR internal direction should remain finite");
                require(finite(path.exitDirection), "TIR reflection direction should remain finite");
                foundTotalInternalReflection |= path.totalInternalReflection;
            }
        }
        require(foundTotalInternalReflection, "high-IOR angle sweep should exercise total internal reflection");

        const float redIor = prismIorAtWavelength(1.52f, 0.33f, 650.0f);
        const float violetIor = prismIorAtWavelength(1.52f, 0.33f, 420.0f);
        require(violetIor > redIor, "violet light should use a higher refractive index than red light");
        require(
            std::abs(prismIorAtWavelength(1.52f, 0.0f, 420.0f) - 1.52f) < 1.0e-6f,
            "zero dispersion should keep a wavelength-independent IOR"
        );

        const glm::vec3 red = wavelengthToLinearSrgb(650.0f);
        const glm::vec3 green = wavelengthToLinearSrgb(540.0f);
        const glm::vec3 blue = wavelengthToLinearSrgb(450.0f);
        require(red.r > red.g && red.r > red.b, "650 nm should map to a red-dominant linear color");
        require(green.g > green.r && green.g > green.b, "540 nm should map to a green-dominant linear color");
        require(blue.b > blue.r && blue.b > blue.g, "450 nm should map to a blue-dominant linear color");

        const SpectralBeamData continuous = tracePrismSpectrum(
            triangle,
            heroRay,
            1.52f,
            0.33f,
            21,
            PrismSpectrumMode::Continuous,
            8.0f,
            glm::vec3(0.88f, 0.96f, 1.0f)
        );
        require(continuous.samples.size() == 21U, "continuous mode should preserve the 21-sample quality tier");
        float normalizedEnergy = 0.0f;
        for (const PrismSpectralSample& sample : continuous.samples) {
            require(sample.path.valid, "every hero spectrum sample should cross the prism");
            require(sample.transmittance > 0.0f, "Fresnel and Beer-Lambert transmission should stay positive");
            normalizedEnergy += sample.normalizedEnergy;
        }
        require(std::abs(normalizedEnergy - 1.0f) < 1.0e-5f, "spectral energy should normalize to one");
        require(
            continuous.samples.front().indexOfRefraction
                > continuous.samples.back().indexOfRefraction,
            "continuous samples should preserve violet-to-red IOR ordering"
        );
        require(
            angularDifference(
                continuous.samples.front().path.exitDirection,
                continuous.samples.back().path.exitDirection
            ) > 1.0e-3f,
            "continuous spectrum endpoints should separate spatially"
        );

        const SpectralBeamData sevenBand = tracePrismSpectrum(
            triangle,
            heroRay,
            1.52f,
            0.33f,
            31,
            PrismSpectrumMode::SevenBand
        );
        require(sevenBand.samples.size() == 7U, "art-directed mode should emit exactly seven bands");

        for (const int qualityTier : {7, 15, 21, 31}) {
            const SpectralBeamData qualitySpectrum = tracePrismSpectrum(
                triangle,
                heroRay,
                1.52f,
                0.33f,
                qualityTier,
                PrismSpectrumMode::Continuous
            );
            require(
                qualitySpectrum.samples.size() == static_cast<std::size_t>(qualityTier),
                "continuous spectrum should preserve every documented quality tier"
            );
        }

        const SpectralBeamData nondispersive = tracePrismSpectrum(
            triangle,
            heroRay,
            1.52f,
            0.0f,
            15,
            PrismSpectrumMode::Continuous
        );
        require(
            angularDifference(
                nondispersive.samples.front().path.exitDirection,
                nondispersive.samples.back().path.exitDirection
            ) < 1.0e-4f,
            "zero dispersion should collapse all outgoing wavelengths onto one path"
        );

        const SpectralBeamMeshData continuousMesh = buildSpectralBeamMesh(
            continuous,
            glm::vec3(0.0f, 0.0f, 4.8f),
            2.4f,
            0.055f
        );
        require(continuousMesh.batches.size() == 3U, "continuous ribbon mesh should have three beam groups");
        require(
            continuousMesh.batches[0].group == SpectralBeamGroup::Incident
                && continuousMesh.batches[1].group == SpectralBeamGroup::Internal
                && continuousMesh.batches[2].group == SpectralBeamGroup::Exit,
            "beam batches should preserve incident, internal, and exit order"
        );
        std::size_t batchedVertexCount = 0U;
        for (const SpectralBeamBatch& batch : continuousMesh.batches) {
            require(batch.vertexCount > 0U && batch.vertexCount % 6U == 0U, "beam batches should contain triangle quads");
            batchedVertexCount += batch.vertexCount;
        }
        require(
            batchedVertexCount == continuousMesh.vertices.size(),
            "beam batches should cover the complete dynamic vertex buffer"
        );
        for (const SpectralBeamVertex& vertex : continuousMesh.vertices) {
            require(finite(vertex.position), "ribbon positions should remain finite");
            require(finite(vertex.linearColor), "ribbon HDR colors should remain finite");
            require(
                vertex.edgeCoordinate >= -1.0001f && vertex.edgeCoordinate <= 1.0001f,
                "ribbon edge coordinates should stay normalized"
            );
        }

        const SpectralBeamMeshData sevenBandMesh = buildSpectralBeamMesh(
            sevenBand,
            glm::vec3(0.0f, 0.0f, 4.8f),
            2.4f,
            0.055f
        );
        require(sevenBandMesh.batches.size() == 3U, "seven-band mesh should preserve the three beam groups");
        require(
            sevenBandMesh.batches[1].vertexCount == 12U,
            "the clipped internal guide should use two tapered ribbon segments"
        );
        require(
            sevenBandMesh.batches[2].vertexCount == sevenBand.samples.size() * 6U,
            "seven-band exits should use one ribbon per wavelength"
        );

        for (const PrismOpticalPreset preset : {
                 PrismOpticalPreset::CrownGlass,
                 PrismOpticalPreset::WaterLike,
                 PrismOpticalPreset::DiamondLike,
                 PrismOpticalPreset::ExaggeratedCover
             }) {
            const PrismDemoParameters parameters = prismOpticalPresetParameters(preset);
            const PrismDemoSolution solution = solvePrismDemo(parameters);
            require(solution.valid, "every Prism-4 preset should produce a traceable optical path");
            require(finite(solution.linearWhitePoint), "preset white points should remain finite");
            require(
                solution.spectrum.samples.size()
                    == (parameters.spectrumMode == PrismSpectrumMode::SevenBand
                        ? 7U
                        : static_cast<std::size_t>(parameters.spectralSampleCount)),
                "preset spectrum mode should select the expected sample count"
            );
            require(prismOpticalPresetName(preset)[0] != '\0', "every preset should have a UI label");
        }

        PrismDemoParameters angledParameters = prismOpticalPresetParameters(
            PrismOpticalPreset::CrownGlass
        );
        const PrismDemoSolution originalAngle = solvePrismDemo(angledParameters);
        angledParameters.beamAngleDegrees -= 4.0f;
        const PrismDemoSolution changedAngle = solvePrismDemo(angledParameters);
        require(originalAngle.valid && changedAngle.valid, "interactive beam angles should remain traceable");
        require(
            angularDifference(
                originalAngle.spectrum.samples.front().path.exitDirection,
                changedAngle.spectrum.samples.front().path.exitDirection
            ) > 1.0e-3f,
            "changing the beam angle should immediately change the solved optical path"
        );

        const glm::vec3 warmWhite = colorTemperatureToLinearSrgb(3000.0f);
        const glm::vec3 daylightWhite = colorTemperatureToLinearSrgb(6500.0f);
        require(finite(warmWhite) && finite(daylightWhite), "white-point conversion should stay finite");
        require(warmWhite.r > warmWhite.b, "a 3000 K white point should be warmer than blue");
        const SpectralBeamMeshData warmMesh = buildSpectralBeamMesh(
            continuous,
            glm::vec3(0.0f, 0.0f, 4.8f),
            2.4f,
            0.055f,
            warmWhite
        );
        require(
            warmMesh.vertices.front().linearColor.r > warmMesh.vertices.front().linearColor.b,
            "the incident ribbon should receive the selected white point"
        );

        glm::vec2 previousCenterDirection(0.0f);
        float separationOrientation = 0.0f;
        for (int step = 0; step <= 20; ++step) {
            PrismDemoParameters sweep = prismOpticalPresetParameters(
                PrismOpticalPreset::CrownGlass
            );
            sweep.beamAngleDegrees = 2.0f + 0.5f * static_cast<float>(step);
            const PrismDemoSolution solution = solvePrismDemo(sweep);
            require(solution.valid, "the documented beam-angle sweep should remain traceable");
            const PrismOpticalPath& violetPath = solution.spectrum.samples.front().path;
            const PrismOpticalPath& redPath = solution.spectrum.samples.back().path;
            require(violetPath.valid && redPath.valid, "spectrum endpoints should survive angle changes");
            const float orientation = violetPath.exitDirection.x * redPath.exitDirection.y
                - violetPath.exitDirection.y * redPath.exitDirection.x;
            if (std::abs(orientation) > 1.0e-5f) {
                if (separationOrientation == 0.0f) separationOrientation = orientation;
                require(
                    orientation * separationOrientation > 0.0f,
                    "red/violet separation order should not flip during the angle sweep"
                );
            }
            const glm::vec2 centerDirection = solution.spectrum.samples[
                solution.spectrum.samples.size() / 2U
            ].path.exitDirection;
            if (step > 0) {
                const float stepDifference = angularDifference(
                    previousCenterDirection,
                    centerDirection
                );
                require(
                    stepDifference < 0.08f,
                    "adjacent beam-angle steps should change continuously without path jumps"
                );
            }
            previousCenterDirection = centerDirection;
        }

        std::cout << "Prism optics tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Prism optics test failed: " << error.what() << '\n';
        return 1;
    }
}
