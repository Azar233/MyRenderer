#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <zlib.h>

namespace {

std::uint32_t readBigEndian(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U)
        | (static_cast<std::uint32_t>(bytes[1]) << 16U)
        | (static_cast<std::uint32_t>(bytes[2]) << 8U)
        | static_cast<std::uint32_t>(bytes[3]);
}

struct RgbaImage {
    std::uint32_t width{0U};
    std::uint32_t height{0U};
    std::vector<std::uint8_t> pixels;
};

RgbaImage readRendererPng(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open PNG: " + path);
    const std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );
    const std::array<std::uint8_t, 8> signature{137U, 80U, 78U, 71U, 13U, 10U, 26U, 10U};
    if (bytes.size() < signature.size()
        || !std::equal(signature.begin(), signature.end(), bytes.begin())) {
        throw std::runtime_error("Invalid PNG signature: " + path);
    }

    RgbaImage image;
    std::vector<std::uint8_t> compressed;
    std::size_t cursor = signature.size();
    while (cursor + 12U <= bytes.size()) {
        const std::uint32_t length = readBigEndian(bytes.data() + cursor);
        cursor += 4U;
        if (cursor + 4U + length + 4U > bytes.size()) {
            throw std::runtime_error("Truncated PNG chunk: " + path);
        }
        const std::string type(
            reinterpret_cast<const char*>(bytes.data() + cursor),
            4U
        );
        cursor += 4U;
        if (type == "IHDR") {
            if (length != 13U) throw std::runtime_error("Invalid IHDR: " + path);
            image.width = readBigEndian(bytes.data() + cursor);
            image.height = readBigEndian(bytes.data() + cursor + 4U);
            if (bytes[cursor + 8U] != 8U || bytes[cursor + 9U] != 6U) {
                throw std::runtime_error("Only 8-bit RGBA renderer PNGs are supported");
            }
        } else if (type == "IDAT") {
            compressed.insert(
                compressed.end(),
                bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                bytes.begin() + static_cast<std::ptrdiff_t>(cursor + length)
            );
        } else if (type == "IEND") {
            break;
        }
        cursor += length + 4U;
    }
    if (image.width == 0U || image.height == 0U || compressed.empty()) {
        throw std::runtime_error("PNG is missing image data: " + path);
    }

    const std::size_t rowBytes = static_cast<std::size_t>(image.width) * 4U;
    const std::size_t scanlineBytes = (rowBytes + 1U) * static_cast<std::size_t>(image.height);
    std::vector<std::uint8_t> scanlines(scanlineBytes);
    uLongf decodedSize = static_cast<uLongf>(scanlines.size());
    const int result = uncompress(
        reinterpret_cast<Bytef*>(scanlines.data()),
        &decodedSize,
        reinterpret_cast<const Bytef*>(compressed.data()),
        static_cast<uLong>(compressed.size())
    );
    if (result != Z_OK || decodedSize != scanlines.size()) {
        throw std::runtime_error("Cannot decompress PNG: " + path);
    }

    image.pixels.resize(rowBytes * static_cast<std::size_t>(image.height));
    for (std::uint32_t row = 0U; row < image.height; ++row) {
        const std::size_t source = static_cast<std::size_t>(row) * (rowBytes + 1U);
        if (scanlines[source] != 0U) {
            throw std::runtime_error("Renderer baseline uses an unsupported PNG row filter");
        }
        std::copy_n(
            scanlines.begin() + static_cast<std::ptrdiff_t>(source + 1U),
            static_cast<std::ptrdiff_t>(rowBytes),
            image.pixels.begin() + static_cast<std::ptrdiff_t>(row * rowBytes)
        );
    }
    return image;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 5) {
            std::cerr << "Usage: ImageComparison baseline.png current.png [max-mae] [max-changed-fraction]\n";
            return 2;
        }
        const double maximumMae = argc >= 4 ? std::stod(argv[3]) : 0.015;
        const double maximumChangedFraction = argc >= 5 ? std::stod(argv[4]) : 0.08;
        const RgbaImage baseline = readRendererPng(argv[1]);
        const RgbaImage current = readRendererPng(argv[2]);
        if (baseline.width != current.width || baseline.height != current.height) {
            throw std::runtime_error("Image dimensions differ");
        }

        std::uint64_t absoluteError = 0U;
        std::size_t changedPixels = 0U;
        const std::size_t pixelCount = static_cast<std::size_t>(baseline.width)
            * static_cast<std::size_t>(baseline.height);
        for (std::size_t pixel = 0U; pixel < pixelCount; ++pixel) {
            int maximumPixelDifference = 0;
            for (std::size_t channel = 0U; channel < 4U; ++channel) {
                const int difference = std::abs(
                    static_cast<int>(baseline.pixels[pixel * 4U + channel])
                    - static_cast<int>(current.pixels[pixel * 4U + channel])
                );
                absoluteError += static_cast<std::uint64_t>(difference);
                maximumPixelDifference = std::max(maximumPixelDifference, difference);
            }
            changedPixels += maximumPixelDifference > 8 ? 1U : 0U;
        }
        const double mae = static_cast<double>(absoluteError)
            / static_cast<double>(pixelCount * 4U * 255U);
        const double changedFraction = static_cast<double>(changedPixels)
            / static_cast<double>(pixelCount);
        std::cout << argv[2] << ": MAE=" << mae
                  << ", changed=" << changedFraction * 100.0 << "%\n";
        return mae <= maximumMae && changedFraction <= maximumChangedFraction ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Image comparison failed: " << error.what() << '\n';
        return 2;
    }
}
