#include "pathtracer/ProgressiveRenderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <glm/common.hpp>
#include <glm/vector_relational.hpp>
#include <zlib.h>
namespace pathtracer {
namespace {
void appendBigEndian(std::vector<std::uint8_t> &output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void appendChunk(std::vector<std::uint8_t> &png, const std::array<char, 4> &type,
                 const std::vector<std::uint8_t> &data) {
    appendBigEndian(png, static_cast<std::uint32_t>(data.size()));
    const std::size_t crcBegin = png.size();
    png.insert(png.end(), type.begin(), type.end());
    png.insert(png.end(), data.begin(), data.end());
    const uLong checksum = crc32(0L, reinterpret_cast<const Bytef *>(png.data() + crcBegin),
                                 static_cast<uInt>(4U + data.size()));
    appendBigEndian(png, static_cast<std::uint32_t>(checksum));
}

bool writePng(const std::filesystem::path &path, int width, int height,
              const std::vector<std::uint8_t> &bottomUpRgba, std::string &error) {
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4U;
    std::vector<std::uint8_t> scanlines((rowBytes + 1U) * static_cast<std::size_t>(height));
    for (int row = 0; row < height; ++row) {
        const std::size_t destination = static_cast<std::size_t>(row) * (rowBytes + 1U);
        const std::size_t source = static_cast<std::size_t>(height - row - 1) * rowBytes;
        scanlines[destination] = 0U;
        std::copy(bottomUpRgba.begin() + static_cast<std::ptrdiff_t>(source),
                  bottomUpRgba.begin() + static_cast<std::ptrdiff_t>(source + rowBytes),
                  scanlines.begin() + static_cast<std::ptrdiff_t>(destination + 1U));
    }

    uLongf compressedSize = compressBound(static_cast<uLong>(scanlines.size()));
    std::vector<std::uint8_t> compressed(compressedSize);
    const int compressionResult = compress2(reinterpret_cast<Bytef *>(compressed.data()), &compressedSize,
                                            reinterpret_cast<const Bytef *>(scanlines.data()),
                                            static_cast<uLong>(scanlines.size()), Z_BEST_SPEED);
    if (compressionResult != Z_OK) {
        error = "PNG compression failed with zlib error " + std::to_string(compressionResult);
        return false;
    }
    compressed.resize(compressedSize);

    std::vector<std::uint8_t> png{137U, 80U, 78U, 71U, 13U, 10U, 26U, 10U};
    std::vector<std::uint8_t> header;
    appendBigEndian(header, static_cast<std::uint32_t>(width));
    appendBigEndian(header, static_cast<std::uint32_t>(height));
    header.insert(header.end(), {8U, 6U, 0U, 0U, 0U});
    appendChunk(png, {'I', 'H', 'D', 'R'}, header);
    appendChunk(png, {'I', 'D', 'A', 'T'}, compressed);
    appendChunk(png, {'I', 'E', 'N', 'D'}, {});

    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        error = "Cannot open screenshot path: " + path.string();
        return false;
    }
    file.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!file) {
        error = "Failed while writing screenshot: " + path.string();
        return false;
    }
    return true;
}

void writeHdr(const std::filesystem::path &path, std::uint32_t width, std::uint32_t height,
              const std::vector<glm::vec3> &pixels) {
    std::ofstream hdr(path, std::ios::binary);
    hdr << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y " << height << " +X " << width << "\n";
    for (const auto &p : pixels) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)
            || glm::any(glm::lessThan(p, glm::vec3(0.0f)))) {
            throw std::invalid_argument("Invalid linear AOV value");
        }
        std::array<unsigned char, 4> rgbe{};
        const float peak = std::max({p.x, p.y, p.z});
        if (peak > 1e-32f) {
            int exponent = 0;
            const float scale = std::frexp(peak, &exponent) * 256.0f / peak;
            if (exponent > 127)
                throw std::runtime_error("Radiance exceeds RGBE range");
            for (int c = 0; c < 3; ++c)
                rgbe[c] = static_cast<unsigned char>(p[c] * scale);
            rgbe[3] = static_cast<unsigned char>(exponent + 128);
        }
        hdr.write(reinterpret_cast<const char *>(rgbe.data()), 4);
    }
    if (!hdr)
        throw std::runtime_error("Cannot write HDR output: " + path.string());
}

void writeImagePair(const std::filesystem::path &stem, std::uint32_t width, std::uint32_t height,
                    const std::vector<glm::vec3> &hdrPixels,
                    const std::vector<glm::vec3> &displayLinearPixels) {
    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    if (!width || !height || hdrPixels.size() != pixelCount || displayLinearPixels.size() != pixelCount)
        throw std::invalid_argument("Cannot export inconsistent image");
    std::vector<std::uint8_t> rgba(pixelCount * 4U);
    for (std::size_t index = 0; index < pixelCount; ++index) {
        for (int channel = 0; channel < 3; ++channel) {
            const float linear = displayLinearPixels[index][channel];
            if (!std::isfinite(linear) || linear < 0.0f)
                throw std::invalid_argument("Invalid display AOV value");
            const float clamped = std::clamp(linear, 0.0f, 1.0f);
            const float srgb = clamped <= 0.0031308f
                ? 12.92f * clamped
                : 1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f;
            const auto flipped = (height - 1U - index / width) * width + index % width;
            rgba[flipped * 4U + static_cast<std::size_t>(channel)] =
                static_cast<std::uint8_t>(std::lround(srgb * 255.0f));
            rgba[flipped * 4U + 3U] = 255U;
        }
    }
    std::string error;
    if (!writePng(stem.string() + ".png", static_cast<int>(width), static_cast<int>(height), rgba, error))
        throw std::runtime_error(error);
    writeHdr(stem.string() + ".hdr", width, height, hdrPixels);
}

std::vector<glm::vec3> scalarRgb(const std::vector<float> &values) {
    std::vector<glm::vec3> pixels;
    pixels.reserve(values.size());
    for (const float value : values)
        pixels.emplace_back(value);
    return pixels;
}

} // namespace
void writeReferenceImage(const RenderImage &image, const std::filesystem::path &stem) {
    if (!image.width || !image.height || !image.completedSamples ||
        image.sum.size() != static_cast<std::size_t>(image.width) * image.height)
        throw std::invalid_argument("Cannot export empty/inconsistent accumulation");
    const auto pixels = image.linearPixels();
    auto display = pixels;
    for (auto &pixel : display)
        pixel = pixel / (glm::vec3(1.0f) + pixel); // Reinhard, exposure 1.
    writeImagePair(stem, image.width, image.height, pixels, display);
}

void writeReferenceAovs(const RenderImage &image, const std::filesystem::path &stem) {
    const std::size_t pixelCount = static_cast<std::size_t>(image.width) * image.height;
    if (!image.width || !image.height || !image.completedSamples || image.sum.size() != pixelCount
        || image.albedoSum.size() != pixelCount || image.normalSum.size() != pixelCount
        || image.depthSum.size() != pixelCount || image.primaryHitCount.size() != pixelCount
        || image.directSum.size() != pixelCount || image.indirectSum.size() != pixelCount
        || image.luminanceSum.size() != pixelCount || image.luminanceSquaredSum.size() != pixelCount) {
        throw std::invalid_argument("Cannot export empty/inconsistent AOV accumulation");
    }

    const auto albedo = image.albedoPixels();
    writeImagePair(stem.string() + "-albedo", image.width, image.height, albedo, albedo);

    auto normal = image.normalPixels();
    for (std::size_t index = 0; index < normal.size(); ++index) {
        normal[index] = image.primaryHitCount[index]
            ? glm::clamp(normal[index] * 0.5f + 0.5f, glm::vec3(0.0f), glm::vec3(1.0f))
            : glm::vec3(0.0f);
    }
    writeImagePair(stem.string() + "-normal", image.width, image.height, normal, normal);

    const auto depthValues = image.depthPixels();
    const auto depth = scalarRgb(depthValues);
    auto displayDepth = depth;
    for (auto &pixel : displayDepth)
        pixel = pixel / (glm::vec3(1.0f) + pixel);
    writeImagePair(stem.string() + "-depth", image.width, image.height, depth, displayDepth);

    const auto direct = image.directPixels();
    auto displayDirect = direct;
    for (auto &pixel : displayDirect)
        pixel = pixel / (glm::vec3(1.0f) + pixel);
    writeImagePair(stem.string() + "-direct", image.width, image.height, direct, displayDirect);

    const auto indirect = image.indirectPixels();
    auto displayIndirect = indirect;
    for (auto &pixel : displayIndirect)
        pixel = pixel / (glm::vec3(1.0f) + pixel);
    writeImagePair(stem.string() + "-indirect", image.width, image.height, indirect, displayIndirect);

    const auto sampleValues = image.sampleCountPixels();
    const auto sampleCount = scalarRgb(sampleValues);
    auto displaySampleCount = sampleCount;
    for (auto &pixel : displaySampleCount)
        pixel = pixel / (glm::vec3(1.0f) + pixel);
    writeImagePair(stem.string() + "-sample-count", image.width, image.height,
                   sampleCount, displaySampleCount);

    const auto varianceValues = image.variancePixels();
    const auto variance = scalarRgb(varianceValues);
    auto displayVariance = variance;
    for (auto &pixel : displayVariance) {
        pixel = glm::sqrt(pixel);
        pixel = pixel / (glm::vec3(1.0f) + pixel);
    }
    writeImagePair(stem.string() + "-variance", image.width, image.height,
                   variance, displayVariance);
}
} // namespace pathtracer
