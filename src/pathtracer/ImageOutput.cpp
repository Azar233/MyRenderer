#include "pathtracer/ProgressiveRenderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>
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

} // namespace
void writeReferenceImage(const RenderImage &image, const std::filesystem::path &stem) {
    if (!image.width || !image.height || !image.completedSamples ||
        image.sum.size() != static_cast<std::size_t>(image.width) * image.height)
        throw std::invalid_argument("Cannot export empty/inconsistent accumulation");
    auto pixels = image.linearPixels();
    std::vector<std::uint8_t> rgba(pixels.size() * 4);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        for (int c = 0; c < 3; ++c) {
            const float linear = pixels[i][c];
            if (!std::isfinite(linear) || linear < 0)
                throw std::invalid_argument("Invalid linear radiance");
            const float mapped = linear / (1 + linear); // Reinhard, exposure 1
            const float srgb =
                mapped <= 0.0031308f ? 12.92f * mapped : 1.055f * std::pow(mapped, 1.0f / 2.4f) - 0.055f;
            const auto flipped = (image.height - 1 - i / image.width) * image.width + i % image.width;
            rgba[flipped * 4 + c] =
                static_cast<std::uint8_t>(std::lround(std::clamp(srgb, 0.0f, 1.0f) * 255));
            rgba[flipped * 4 + 3] = 255;
        }
    }
    std::string error;
    if (!writePng(stem.string() + ".png", static_cast<int>(image.width), static_cast<int>(image.height), rgba,
                  error))
        throw std::runtime_error(error);
    // Radiance RGBE, linear RGB. Legacy flat scanlines are valid for every width.
    std::ofstream hdr(stem.string() + ".hdr", std::ios::binary);
    hdr << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y " << image.height << " +X " << image.width << "\n";
    for (const auto &p : pixels) {
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
        throw std::runtime_error("Cannot write HDR output");
}
} // namespace pathtracer
