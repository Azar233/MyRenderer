#include "pathtracer/ReferenceComparison.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <vector>

#include <zlib.h>

namespace pathtracer {
namespace {

constexpr float differenceVisualizationGain = 4.0f;
constexpr std::uint32_t differenceMedianRadius = 2U;

struct RgbaImage {
    std::uint32_t width{0U};
    std::uint32_t height{0U};
    std::vector<std::uint8_t> pixels; // Top row first.
};

std::uint32_t readBigEndian(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U)
        | (static_cast<std::uint32_t>(bytes[1]) << 16U)
        | (static_cast<std::uint32_t>(bytes[2]) << 8U)
        | static_cast<std::uint32_t>(bytes[3]);
}

void appendBigEndian(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void appendChunk(
    std::vector<std::uint8_t>& png,
    const std::array<char, 4>& type,
    const std::vector<std::uint8_t>& data
) {
    appendBigEndian(png, static_cast<std::uint32_t>(data.size()));
    const std::size_t crcBegin = png.size();
    png.insert(png.end(), type.begin(), type.end());
    png.insert(png.end(), data.begin(), data.end());
    const uLong checksum = crc32(
        0L,
        reinterpret_cast<const Bytef*>(png.data() + crcBegin),
        static_cast<uInt>(4U + data.size())
    );
    appendBigEndian(png, static_cast<std::uint32_t>(checksum));
}

void writeTopDownPng(const std::filesystem::path& path, const RgbaImage& image) {
    const std::size_t rowBytes = static_cast<std::size_t>(image.width) * 4U;
    if (image.width == 0U || image.height == 0U
        || image.pixels.size() != rowBytes * image.height) {
        throw std::invalid_argument("Cannot write inconsistent comparison PNG");
    }
    std::vector<std::uint8_t> scanlines((rowBytes + 1U) * image.height);
    for (std::uint32_t row = 0U; row < image.height; ++row) {
        const std::size_t destination = static_cast<std::size_t>(row) * (rowBytes + 1U);
        const std::size_t source = static_cast<std::size_t>(row) * rowBytes;
        scanlines[destination] = 0U;
        std::copy_n(
            image.pixels.begin() + static_cast<std::ptrdiff_t>(source),
            static_cast<std::ptrdiff_t>(rowBytes),
            scanlines.begin() + static_cast<std::ptrdiff_t>(destination + 1U)
        );
    }

    uLongf compressedSize = compressBound(static_cast<uLong>(scanlines.size()));
    std::vector<std::uint8_t> compressed(compressedSize);
    const int result = compress2(
        reinterpret_cast<Bytef*>(compressed.data()),
        &compressedSize,
        reinterpret_cast<const Bytef*>(scanlines.data()),
        static_cast<uLong>(scanlines.size()),
        Z_BEST_SPEED
    );
    if (result != Z_OK) throw std::runtime_error("Comparison PNG compression failed");
    compressed.resize(compressedSize);

    std::vector<std::uint8_t> png{137U, 80U, 78U, 71U, 13U, 10U, 26U, 10U};
    std::vector<std::uint8_t> header;
    appendBigEndian(header, image.width);
    appendBigEndian(header, image.height);
    header.insert(header.end(), {8U, 6U, 0U, 0U, 0U});
    appendChunk(png, {'I', 'H', 'D', 'R'}, header);
    appendChunk(png, {'I', 'D', 'A', 'T'}, compressed);
    appendChunk(png, {'I', 'E', 'N', 'D'}, {});

    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("Cannot open comparison PNG: " + path.string());
    output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!output) throw std::runtime_error("Cannot write comparison PNG: " + path.string());
}

RgbaImage readRendererPng(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open renderer PNG: " + path.string());
    const std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>()
    );
    const std::array<std::uint8_t, 8> signature{137U, 80U, 78U, 71U, 13U, 10U, 26U, 10U};
    if (bytes.size() < signature.size()
        || !std::equal(signature.begin(), signature.end(), bytes.begin())) {
        throw std::runtime_error("Invalid renderer PNG signature: " + path.string());
    }

    RgbaImage image;
    std::vector<std::uint8_t> compressed;
    std::size_t cursor = signature.size();
    while (cursor + 12U <= bytes.size()) {
        const std::uint32_t length = readBigEndian(bytes.data() + cursor);
        cursor += 4U;
        if (cursor + 4U + length + 4U > bytes.size()) {
            throw std::runtime_error("Truncated renderer PNG: " + path.string());
        }
        const std::array<char, 4> type{
            static_cast<char>(bytes[cursor]), static_cast<char>(bytes[cursor + 1U]),
            static_cast<char>(bytes[cursor + 2U]), static_cast<char>(bytes[cursor + 3U])
        };
        cursor += 4U;
        if (type == std::array<char, 4>{'I', 'H', 'D', 'R'}) {
            if (length != 13U || bytes[cursor + 8U] != 8U || bytes[cursor + 9U] != 6U) {
                throw std::runtime_error("Only 8-bit RGBA renderer PNGs are supported");
            }
            image.width = readBigEndian(bytes.data() + cursor);
            image.height = readBigEndian(bytes.data() + cursor + 4U);
        } else if (type == std::array<char, 4>{'I', 'D', 'A', 'T'}) {
            compressed.insert(
                compressed.end(),
                bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                bytes.begin() + static_cast<std::ptrdiff_t>(cursor + length)
            );
        } else if (type == std::array<char, 4>{'I', 'E', 'N', 'D'}) {
            break;
        }
        cursor += length + 4U; // Skip data and CRC.
    }
    if (image.width == 0U || image.height == 0U || compressed.empty()) {
        throw std::runtime_error("Renderer PNG is missing image data: " + path.string());
    }

    const std::size_t rowBytes = static_cast<std::size_t>(image.width) * 4U;
    std::vector<std::uint8_t> scanlines((rowBytes + 1U) * image.height);
    uLongf decodedSize = static_cast<uLongf>(scanlines.size());
    const int result = uncompress(
        reinterpret_cast<Bytef*>(scanlines.data()),
        &decodedSize,
        reinterpret_cast<const Bytef*>(compressed.data()),
        static_cast<uLong>(compressed.size())
    );
    if (result != Z_OK || decodedSize != scanlines.size()) {
        throw std::runtime_error("Cannot decompress renderer PNG: " + path.string());
    }
    image.pixels.resize(rowBytes * image.height);
    for (std::uint32_t row = 0U; row < image.height; ++row) {
        const std::size_t source = static_cast<std::size_t>(row) * (rowBytes + 1U);
        if (scanlines[source] != 0U) {
            throw std::runtime_error("Renderer PNG uses an unsupported row filter");
        }
        std::copy_n(
            scanlines.begin() + static_cast<std::ptrdiff_t>(source + 1U),
            static_cast<std::ptrdiff_t>(rowBytes),
            image.pixels.begin() + static_cast<std::ptrdiff_t>(row * rowBytes)
        );
    }
    return image;
}

float aces(float value) {
    constexpr float a = 2.51f;
    constexpr float b = 0.03f;
    constexpr float c = 2.43f;
    constexpr float d = 0.59f;
    constexpr float e = 0.14f;
    const float numerator = value * (a * value + b);
    const float denominator = value * (c * value + d) + e;
    return std::clamp(denominator > 0.0f ? numerator / denominator : 0.0f, 0.0f, 1.0f);
}

std::uint8_t encode(float linear) {
    linear = std::clamp(linear, 0.0f, 1.0f);
    const float srgb = linear <= 0.0031308f
        ? 12.92f * linear
        : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return static_cast<std::uint8_t>(std::lround(std::clamp(srgb, 0.0f, 1.0f) * 255.0f));
}

RgbaImage pathDisplay(const RenderImage& image, const ReferenceComparisonOptions& options) {
    const std::size_t pixelCount = static_cast<std::size_t>(image.width) * image.height;
    if (image.width == 0U || image.height == 0U || image.sum.size() != pixelCount) {
        throw std::invalid_argument("Cannot compare an empty or inconsistent path-traced image");
    }
    const auto linearPixels = image.linearPixels();
    RgbaImage display{image.width, image.height, std::vector<std::uint8_t>(pixelCount * 4U)};
    for (std::size_t pixel = 0U; pixel < pixelCount; ++pixel) {
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
            float value = linearPixels[pixel][static_cast<int>(channel)];
            value = std::isfinite(value) ? std::max(value, 0.0f) * std::max(options.exposure, 0.0f) : 0.0f;
            value = options.toneMapping ? aces(value) : std::clamp(value, 0.0f, 1.0f);
            display.pixels[pixel * 4U + channel] = encode(value);
        }
        display.pixels[pixel * 4U + 3U] = 255U;
    }
    return display;
}

std::string jsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        if (character == '\\' || character == '"') escaped.push_back('\\');
        escaped.push_back(character);
    }
    return escaped;
}

std::array<std::uint8_t, 3> heatColor(float value) {
    // Black -> violet -> magenta -> red -> warm white. Avoid the old
    // red+green yellow cast while keeping large errors visually dominant.
    struct Stop { float position; std::array<float, 3> color; };
    static constexpr std::array<Stop, 5> stops{{
        {0.00f, {0.008f, 0.004f, 0.025f}},
        {0.25f, {0.110f, 0.025f, 0.360f}},
        {0.50f, {0.620f, 0.020f, 0.420f}},
        {0.75f, {0.950f, 0.090f, 0.080f}},
        {1.00f, {1.000f, 0.900f, 0.820f}}
    }};
    value = std::clamp(value, 0.0f, 1.0f);
    std::size_t upper = 1U;
    while (upper + 1U < stops.size() && value > stops[upper].position) ++upper;
    const Stop& left = stops[upper - 1U];
    const Stop& right = stops[upper];
    const float amount = (value - left.position) / (right.position - left.position);
    std::array<std::uint8_t, 3> result{};
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
        const float mixed = left.color[channel]
            + amount * (right.color[channel] - left.color[channel]);
        result[channel] = static_cast<std::uint8_t>(std::lround(mixed * 255.0f));
    }
    return result;
}

std::vector<float> medianFiltered(
    const std::vector<float>& values,
    std::uint32_t width,
    std::uint32_t height
) {
    std::vector<float> filtered(values.size());
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            std::array<float, 25> neighborhood{};
            std::size_t count = 0U;
            const std::uint32_t y0 = y > differenceMedianRadius
                ? y - differenceMedianRadius : 0U;
            const std::uint32_t y1 = std::min(y + differenceMedianRadius, height - 1U);
            const std::uint32_t x0 = x > differenceMedianRadius
                ? x - differenceMedianRadius : 0U;
            const std::uint32_t x1 = std::min(x + differenceMedianRadius, width - 1U);
            for (std::uint32_t sampleY = y0; sampleY <= y1; ++sampleY) {
                for (std::uint32_t sampleX = x0; sampleX <= x1; ++sampleX) {
                    neighborhood[count++] = values[static_cast<std::size_t>(sampleY) * width + sampleX];
                }
            }
            std::sort(neighborhood.begin(), neighborhood.begin() + static_cast<std::ptrdiff_t>(count));
            filtered[static_cast<std::size_t>(y) * width + x] = neighborhood[count / 2U];
        }
    }
    return filtered;
}

RgbaImage heatImage(
    const std::vector<float>& values,
    std::uint32_t width,
    std::uint32_t height
) {
    RgbaImage image{width, height, std::vector<std::uint8_t>(values.size() * 4U)};
    for (std::size_t pixel = 0U; pixel < values.size(); ++pixel) {
        const auto color = heatColor(std::clamp(
            values[pixel] * differenceVisualizationGain, 0.0f, 1.0f
        ));
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
            image.pixels[pixel * 4U + channel] = color[channel];
        }
        image.pixels[pixel * 4U + 3U] = 255U;
    }
    return image;
}

} // namespace

void writePathTracedDisplayPng(
    const RenderImage& image,
    const std::filesystem::path& path,
    const ReferenceComparisonOptions& options
) {
    writeTopDownPng(path, pathDisplay(image, options));
}

ReferenceComparisonMetrics writeReferenceComparison(
    const RenderImage& image,
    const std::filesystem::path& rasterPng,
    const std::filesystem::path& outputDirectory,
    const ReferenceComparisonOptions& options
) {
    std::filesystem::create_directories(outputDirectory);
    const RgbaImage raster = readRendererPng(rasterPng);
    const RgbaImage path = pathDisplay(image, options);
    if (raster.width != path.width || raster.height != path.height) {
        throw std::runtime_error("Raster and path-traced image dimensions differ");
    }
    writeTopDownPng(outputDirectory / "path-traced.png", path);

    const std::size_t pixelCount = static_cast<std::size_t>(path.width) * path.height;
    std::vector<float> rawDifference(pixelCount);
    double absoluteError = 0.0;
    double squaredError = 0.0;
    std::size_t changedPixels = 0U;
    for (std::size_t pixel = 0U; pixel < pixelCount; ++pixel) {
        int maximumDifference = 0;
        float averageDifference = 0.0f;
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
            const int delta = std::abs(
                static_cast<int>(raster.pixels[pixel * 4U + channel])
                - static_cast<int>(path.pixels[pixel * 4U + channel])
            );
            maximumDifference = std::max(maximumDifference, delta);
            averageDifference += static_cast<float>(delta) / (3.0f * 255.0f);
            const double normalized = static_cast<double>(delta) / 255.0;
            absoluteError += normalized;
            squaredError += normalized * normalized;
        }
        changedPixels += maximumDifference > options.changedThreshold ? 1U : 0U;
        rawDifference[pixel] = averageDifference;
    }
    const RgbaImage rawDifferenceImage = heatImage(rawDifference, path.width, path.height);
    const RgbaImage difference = heatImage(
        medianFiltered(rawDifference, path.width, path.height), path.width, path.height
    );
    writeTopDownPng(outputDirectory / "difference-raw.png", rawDifferenceImage);
    writeTopDownPng(outputDirectory / "difference.png", difference);

    RgbaImage triptych{path.width * 3U, path.height,
        std::vector<std::uint8_t>(pixelCount * 12U)};
    for (std::size_t pixel = 0U; pixel < pixelCount; ++pixel) {
        const std::size_t row = pixel / path.width;
        const std::size_t column = pixel % path.width;
        const std::size_t triptychRow = row * static_cast<std::size_t>(triptych.width) * 4U;
        for (std::size_t panel = 0U; panel < 3U; ++panel) {
            const RgbaImage& source = panel == 0U ? raster : (panel == 1U ? path : difference);
            const std::size_t destination = triptychRow
                + (panel * path.width + column) * 4U;
            std::copy_n(source.pixels.begin() + static_cast<std::ptrdiff_t>(pixel * 4U), 4,
                        triptych.pixels.begin() + static_cast<std::ptrdiff_t>(destination));
        }
    }
    writeTopDownPng(outputDirectory / "triptych.png", triptych);

    const double channelCount = static_cast<double>(pixelCount * 3U);
    ReferenceComparisonMetrics metrics;
    metrics.width = path.width;
    metrics.height = path.height;
    metrics.meanAbsoluteError = absoluteError / channelCount;
    metrics.rootMeanSquaredError = std::sqrt(squaredError / channelCount);
    metrics.peakSignalToNoiseRatio = metrics.rootMeanSquaredError > 0.0
        ? 20.0 * std::log10(1.0 / metrics.rootMeanSquaredError)
        : std::numeric_limits<double>::infinity();
    metrics.changedFraction = static_cast<double>(changedPixels) / static_cast<double>(pixelCount);

    std::uint32_t minimumSamples = image.completedSamples;
    std::uint32_t maximumSamples = image.completedSamples;
    double meanSamples = image.completedSamples;
    if (image.sampleCounts.size() == pixelCount && !image.sampleCounts.empty()) {
        const auto range = std::minmax_element(image.sampleCounts.begin(), image.sampleCounts.end());
        minimumSamples = *range.first;
        maximumSamples = *range.second;
        double total = 0.0;
        for (const std::uint32_t count : image.sampleCounts) total += count;
        meanSamples = total / static_cast<double>(pixelCount);
    }

    std::ofstream report(outputDirectory / "comparison.json", std::ios::binary);
    if (!report) throw std::runtime_error("Cannot open comparison report");
    report << std::fixed << std::setprecision(8)
           << "{\n"
           << "  \"format\": \"MyRendererReferenceComparison\",\n"
           << "  \"version\": 2,\n"
           << "  \"scene\": \"" << jsonEscape(options.sceneName) << "\",\n"
           << "  \"width\": " << metrics.width << ",\n"
           << "  \"height\": " << metrics.height << ",\n"
           << "  \"exposure\": " << options.exposure << ",\n"
           << "  \"toneMapping\": " << (options.toneMapping ? "true" : "false") << ",\n"
           << "  \"changedThreshold8Bit\": " << static_cast<unsigned int>(options.changedThreshold) << ",\n"
           << "  \"displayTransform\": {\n"
           << "    \"exposure\": " << options.exposure << ",\n"
           << "    \"toneMapping\": \"" << (options.toneMapping ? "aces-fitted" : "clamp") << "\",\n"
           << "    \"encoding\": \"srgb\"\n"
           << "  },\n"
           << "  \"differenceVisualization\": {\n"
           << "    \"metric\": \"mean-absolute-display-rgb\",\n"
           << "    \"gain\": " << differenceVisualizationGain << ",\n"
           << "    \"rawFilter\": \"none\",\n"
           << "    \"displayFilter\": \"5x5-median\",\n"
           << "    \"colorMap\": \"black-violet-magenta-red-warm-white\",\n"
           << "    \"changedThreshold8Bit\": "
           << static_cast<unsigned int>(options.changedThreshold) << "\n"
           << "  },\n"
           << "  \"meanAbsoluteError\": " << metrics.meanAbsoluteError << ",\n"
           << "  \"rootMeanSquaredError\": " << metrics.rootMeanSquaredError << ",\n"
           << "  \"peakSignalToNoiseRatioDb\": ";
    if (std::isfinite(metrics.peakSignalToNoiseRatio)) report << metrics.peakSignalToNoiseRatio;
    else report << "null";
    report << ",\n"
           << "  \"changedFraction\": " << metrics.changedFraction << ",\n"
           << "  \"pathTracing\": {\n"
           << "    \"targetSamplesPerPixel\": " << options.targetSamplesPerPixel << ",\n"
           << "    \"maxDepth\": " << options.maxDepth << ",\n"
           << "    \"seed\": " << options.seed << ",\n"
           << "    \"completedSamples\": " << image.completedSamples << ",\n"
           << "    \"minimumSamples\": " << minimumSamples << ",\n"
           << "    \"meanSamples\": " << meanSamples << ",\n"
           << "    \"maximumSamples\": " << maximumSamples << ",\n"
           << "    \"pathRays\": " << image.statistics.pathRays << ",\n"
           << "    \"shadowRays\": " << image.statistics.shadowRays << ",\n"
           << "    \"renderMilliseconds\": " << image.statistics.renderMilliseconds << "\n"
           << "  },\n"
           << "  \"artifacts\": {\n"
           << "    \"raster\": \"raster.png\",\n"
           << "    \"pathTraced\": \"path-traced.png\",\n"
           << "    \"rawDifference\": \"difference-raw.png\",\n"
           << "    \"difference\": \"difference.png\",\n"
           << "    \"triptych\": \"triptych.png\"\n"
           << "  }\n"
           << "}\n";
    if (!report) throw std::runtime_error("Cannot write comparison report");
    return metrics;
}

} // namespace pathtracer
