#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "pathtracer/ProgressiveRenderer.h"

namespace pathtracer {

struct ReferenceComparisonOptions {
    float exposure{1.0f};
    bool toneMapping{true};
    std::uint8_t changedThreshold{8U};
    std::string sceneName;
    std::uint32_t targetSamplesPerPixel{0U};
    std::uint32_t maxDepth{0U};
    std::uint32_t seed{0U};
};

struct ReferenceComparisonMetrics {
    std::uint32_t width{0U};
    std::uint32_t height{0U};
    double meanAbsoluteError{0.0};
    double rootMeanSquaredError{0.0};
    double peakSignalToNoiseRatio{0.0};
    double changedFraction{0.0};
};

// Exports the CPU result with the same exposure, ACES curve, and sRGB encoding
// used by the raster post-process pass. RenderImage pixels are top-row first.
void writePathTracedDisplayPng(
    const RenderImage& image,
    const std::filesystem::path& path,
    const ReferenceComparisonOptions& options = {}
);

// Reads the renderer-authored raster PNG and writes path-traced.png,
// difference-raw.png, a 5x5 median-filtered difference.png, triptych.png, and
// comparison.json into outputDirectory. Metrics always use unfiltered pixels.
ReferenceComparisonMetrics writeReferenceComparison(
    const RenderImage& image,
    const std::filesystem::path& rasterPng,
    const std::filesystem::path& outputDirectory,
    const ReferenceComparisonOptions& options = {}
);

} // namespace pathtracer
