#include "app/AppIcon.h"

#include <array>
#include <cstddef>
#include <string_view>

#include <GLFW/glfw3.h>

namespace {

constexpr int kIconSize = 16;

// A raster triangle on a dark tile. Each character is one source pixel.
constexpr std::array<std::string_view, kIconSize> kIconPattern{
    "...NNNNNNNNNN...",
    ".NNNNNNNNNNNNNN.",
    "NNNNNNNNNNNNNNNN",
    "NNNNNNNNENNNNNNN",
    "NNNNNNNECENNNNNN",
    "NNNNNNECHBENNNNN",
    "NNNNNNECHBENNNNN",
    "NNNNNECCHBBENNNN",
    "NNNNNECCHBBENNNN",
    "NNNNECCCHBBBENNN",
    "NNNNECCCHBBBENNN",
    "NNNECCCCHBBBBENN",
    "NNNEEEEEEEEEEENN",
    "NNNNNNNNNNNNNNNN",
    ".NNNNNNNNNNNNNN.",
    "...NNNNNNNNNN..."
};

struct Color {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char alpha;
};

constexpr Color colorFor(char pixel) {
    switch (pixel) {
    case 'N': return {0x10, 0x15, 0x1f, 0xff}; // Background tile.
    case 'E': return {0xea, 0xf8, 0xff, 0xff}; // Triangle edge.
    case 'C': return {0x45, 0xd5, 0xe8, 0xff}; // Cyan face.
    case 'B': return {0x3b, 0x82, 0xf6, 0xff}; // Blue face.
    case 'H': return {0xa7, 0xf3, 0xd0, 0xff}; // Raster seam.
    default: return {0x00, 0x00, 0x00, 0x00};
    }
}

template <int Scale>
using IconPixels = std::array<unsigned char,
    static_cast<std::size_t>(kIconSize * Scale * kIconSize * Scale * 4)>;

template <int Scale>
IconPixels<Scale> buildIconPixels() {
    constexpr int scaledSize = kIconSize * Scale;
    IconPixels<Scale> pixels{};
    for (int sourceY = 0; sourceY < kIconSize; ++sourceY) {
        for (int sourceX = 0; sourceX < kIconSize; ++sourceX) {
            const Color color = colorFor(kIconPattern[sourceY][sourceX]);
            for (int offsetY = 0; offsetY < Scale; ++offsetY) {
                for (int offsetX = 0; offsetX < Scale; ++offsetX) {
                    const int targetX = sourceX * Scale + offsetX;
                    const int targetY = sourceY * Scale + offsetY;
                    const std::size_t destination =
                        static_cast<std::size_t>((targetY * scaledSize + targetX) * 4);
                    pixels[destination] = color.red;
                    pixels[destination + 1U] = color.green;
                    pixels[destination + 2U] = color.blue;
                    pixels[destination + 3U] = color.alpha;
                }
            }
        }
    }
    return pixels;
}

} // namespace

void setMyRendererWindowIcon(GLFWwindow* window) {
    if (window == nullptr) {
        return;
    }

    static auto pixels16 = buildIconPixels<1>();
    static auto pixels32 = buildIconPixels<2>();
    static auto pixels64 = buildIconPixels<4>();
    GLFWimage images[] = {
        {16, 16, pixels16.data()},
        {32, 32, pixels32.data()},
        {64, 64, pixels64.data()}
    };
    glfwSetWindowIcon(window, 3, images);
}
