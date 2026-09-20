#include "pathtracer/AovDenoiser.h"
#include "pathtracer/ProgressiveRenderer.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::uint32_t number(const char* text) {
    const std::string input(text);
    std::size_t end = 0U;
    const unsigned long value = std::stoul(input, &end);
    if (input.empty() || end != input.size()
        || value > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Expected an unsigned integer");
    return static_cast<std::uint32_t>(value);
}

pathtracer::RenderImage render(
    const pathtracer::SceneSnapshot& scene,
    pathtracer::RenderSettings settings
) {
    pathtracer::ProgressiveRenderer renderer(scene, settings);
    while (renderer.renderPass()) {}
    return renderer.image();
}

struct SceneCase {
    const char* name;
    pathtracer::SceneSnapshot scene;
};

void writeMetric(
    std::ofstream& csv,
    const char* scene,
    std::uint32_t spp,
    const char* aov,
    const char* mode,
    const pathtracer::ImageMetrics& metrics,
    double renderMilliseconds,
    double denoiseMilliseconds
) {
    csv << scene << ',' << spp << ',' << aov << ',' << mode << ','
        << metrics.rmse << ',' << metrics.psnr << ',' << metrics.ssim << ','
        << metrics.gradientRetention << ',' << metrics.fireflyPixels << ','
        << renderMilliseconds << ',' << denoiseMilliseconds << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 4) {
            std::cerr << "Usage: MyRendererDenoisingExperiment output-directory "
                         "[resolution [reference-spp]]\n";
            return 1;
        }
        const std::filesystem::path output = argv[1];
        const std::uint32_t resolution = argc >= 3 ? number(argv[2]) : 64U;
        const std::uint32_t referenceSpp = argc >= 4 ? number(argv[3]) : 2048U;
        if (resolution < 16U || referenceSpp < 64U)
            throw std::invalid_argument("Resolution must be >= 16 and reference SPP >= 64");
        std::filesystem::create_directories(output);
        std::ofstream csv(output / "metrics.csv");
        csv << "scene,spp,aov,mode,rmse,psnr,ssim,gradient_retention,fireflies,"
               "render_ms,denoise_ms\n";
        csv << std::setprecision(9);

        std::vector<SceneCase> scenes;
        scenes.push_back({"diffuse", pathtracer::makeDiffuseAcceptanceScene()});
        scenes.push_back({"pbr-hdri", pathtracer::makePbrAcceptanceScene()});
        scenes.push_back({"instancing", pathtracer::makeInstancingStressScene(5U)});
        static constexpr std::array<std::uint32_t, 5> sampleCounts{1U, 2U, 4U, 8U, 16U};
        pathtracer::DenoiseSettings denoise;
        denoise.enabled = true;
        denoise.atrousIterations = 4U;

        std::ofstream markdown(output / "summary.md");
        markdown << "# P0-D denoising experiment\n\n"
                 << "Deterministic " << resolution << "x" << resolution
                 << " captures; unbiased reference = " << referenceSpp
                 << " SPP. Formal rows keep Firefly Clamp disabled.\n\n"
                 << "| Scene | SPP | Raw RMSE | Denoised RMSE | Raw SSIM | Denoised SSIM | "
                    "Denoise ms | Detail retention |\n"
                 << "|---|---:|---:|---:|---:|---:|---:|---:|\n";

        for (const SceneCase& sceneCase : scenes) {
            std::cout << "Reference " << sceneCase.name << " at " << referenceSpp << " SPP\n";
            pathtracer::RenderSettings referenceSettings{
                resolution, resolution, referenceSpp, 6U, 20260917U};
            referenceSettings.workerCount = 0U;
            referenceSettings.lightSelectionStrategy =
                pathtracer::LightSelectionStrategy::PowerWeighted;
            referenceSettings.ggxSamplingStrategy =
                pathtracer::GgxSamplingStrategy::VisibleNormals;
            const pathtracer::RenderImage reference = render(sceneCase.scene, referenceSettings);
            const std::filesystem::path sceneDirectory = output / sceneCase.name;
            const std::filesystem::path referenceStem = sceneDirectory
                / ("reference-" + std::to_string(referenceSpp) + "spp");
            pathtracer::writeReferenceImage(reference, referenceStem);
            pathtracer::writeReferenceAovs(reference, referenceStem);
            const auto referenceBeauty = reference.linearPixels();
            const auto referenceDirect = reference.directPixels();
            const auto referenceIndirect = reference.indirectPixels();

            for (const std::uint32_t spp : sampleCounts) {
                pathtracer::RenderSettings settings = referenceSettings;
                settings.samplesPerPixel = spp;
                const pathtracer::RenderImage raw = render(sceneCase.scene, settings);
                const pathtracer::DenoisedImage filtered = pathtracer::denoiseAovs(raw, denoise);
                const std::filesystem::path rawStem = sceneDirectory
                    / ("raw-" + std::to_string(spp) + "spp");
                pathtracer::writeReferenceImage(raw, rawStem);
                pathtracer::writeReferenceAovs(raw, rawStem);
                pathtracer::writeDenoisedImage(
                    filtered, sceneDirectory / ("denoised-" + std::to_string(spp) + "spp"));
                const pathtracer::ImageMetrics rawBeauty = pathtracer::compareImages(
                    raw.linearPixels(), referenceBeauty, resolution, resolution);
                const pathtracer::ImageMetrics filteredBeauty = pathtracer::compareImages(
                    filtered.beauty, referenceBeauty, resolution, resolution);
                writeMetric(csv, sceneCase.name, spp, "beauty", "raw", rawBeauty,
                            raw.statistics.renderMilliseconds, 0.0);
                writeMetric(csv, sceneCase.name, spp, "beauty", "denoised", filteredBeauty,
                            raw.statistics.renderMilliseconds, filtered.milliseconds);
                writeMetric(csv, sceneCase.name, spp, "direct", "raw",
                            pathtracer::compareImages(raw.directPixels(), referenceDirect,
                                resolution, resolution), raw.statistics.renderMilliseconds, 0.0);
                writeMetric(csv, sceneCase.name, spp, "direct", "denoised",
                            pathtracer::compareImages(filtered.direct, referenceDirect,
                                resolution, resolution), raw.statistics.renderMilliseconds,
                            filtered.milliseconds);
                writeMetric(csv, sceneCase.name, spp, "indirect", "raw",
                            pathtracer::compareImages(raw.indirectPixels(), referenceIndirect,
                                resolution, resolution), raw.statistics.renderMilliseconds, 0.0);
                writeMetric(csv, sceneCase.name, spp, "indirect", "denoised",
                            pathtracer::compareImages(filtered.indirect, referenceIndirect,
                                resolution, resolution), raw.statistics.renderMilliseconds,
                            filtered.milliseconds);
                markdown << '|' << sceneCase.name << '|' << spp << '|'
                         << rawBeauty.rmse << '|' << filteredBeauty.rmse << '|'
                         << rawBeauty.ssim << '|' << filteredBeauty.ssim << '|'
                         << filtered.milliseconds << '|' << filteredBeauty.gradientRetention
                         << "|\n";
                if (spp == 4U) {
                    pathtracer::writeDenoisingTriptych(
                        raw.linearPixels(), filtered.beauty, referenceBeauty,
                        resolution, resolution,
                        sceneDirectory / "comparison-4spp-raw-denoised-reference.png");
                }
            }

            pathtracer::RenderSettings legacy = referenceSettings;
            legacy.samplesPerPixel = 16U;
            legacy.lightSelectionStrategy = pathtracer::LightSelectionStrategy::Uniform;
            legacy.ggxSamplingStrategy = pathtracer::GgxSamplingStrategy::Distribution;
            const pathtracer::RenderImage legacyImage = render(sceneCase.scene, legacy);
            writeMetric(csv, sceneCase.name, 16U, "beauty", "uniform-ndf",
                        pathtracer::compareImages(legacyImage.linearPixels(), referenceBeauty,
                            resolution, resolution), legacyImage.statistics.renderMilliseconds, 0.0);
        }

        markdown << "\nEach triptych is **Raw 4 SPP | AOV A-Trous | unbiased reference**. "
                    "`metrics.csv` additionally separates Direct and Indirect, reports PSNR/SSIM, "
                    "gradient retention (detail loss), firefly counts, and timing.\n";
        std::cout << "Wrote P0-D evidence to " << output << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
