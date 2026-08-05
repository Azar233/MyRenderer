#include "render/RenderTarget.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>

#include <glad/gl.h>
#include <zlib.h>

namespace {

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

bool writePng(
    const std::filesystem::path& path,
    int width,
    int height,
    const std::vector<std::uint8_t>& bottomUpRgba,
    std::string& error
) {
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4U;
    std::vector<std::uint8_t> scanlines((rowBytes + 1U) * static_cast<std::size_t>(height));
    for (int row = 0; row < height; ++row) {
        const std::size_t destination = static_cast<std::size_t>(row) * (rowBytes + 1U);
        const std::size_t source = static_cast<std::size_t>(height - row - 1) * rowBytes;
        scanlines[destination] = 0U;
        std::copy(
            bottomUpRgba.begin() + static_cast<std::ptrdiff_t>(source),
            bottomUpRgba.begin() + static_cast<std::ptrdiff_t>(source + rowBytes),
            scanlines.begin() + static_cast<std::ptrdiff_t>(destination + 1U)
        );
    }

    uLongf compressedSize = compressBound(static_cast<uLong>(scanlines.size()));
    std::vector<std::uint8_t> compressed(compressedSize);
    const int compressionResult = compress2(
        reinterpret_cast<Bytef*>(compressed.data()),
        &compressedSize,
        reinterpret_cast<const Bytef*>(scanlines.data()),
        static_cast<uLong>(scanlines.size()),
        Z_BEST_SPEED
    );
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
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!file) {
        error = "Failed while writing screenshot: " + path.string();
        return false;
    }
    return true;
}

void requireComplete(const char* label) {
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error(std::string("Failed to create ") + label + " framebuffer");
    }
}

} // namespace

RenderTarget::~RenderTarget() {
    destroy();
}

void RenderTarget::resize(int width, int height, int samples) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    int maximumSamples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &maximumSamples);
    samples = std::clamp(samples, 1, std::max(maximumSamples, 1));
    if (resolveFramebuffer_ != 0U && width == width_ && height == height_ && samples == samples_) {
        return;
    }

    destroy();
    width_ = width;
    height_ = height;
    samples_ = samples;

    glGenFramebuffers(1, &resolveFramebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFramebuffer_);
    glGenTextures(1, &colorTexture_);
    glBindTexture(GL_TEXTURE_2D, colorTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture_, 0);

    if (samples_ > 1) {
        requireComplete("resolve");
        glGenFramebuffers(1, &multisampleFramebuffer_);
        glBindFramebuffer(GL_FRAMEBUFFER, multisampleFramebuffer_);
        glGenRenderbuffers(1, &multisampleColor_);
        glBindRenderbuffer(GL_RENDERBUFFER, multisampleColor_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA8, width_, height_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, multisampleColor_);
    }

    glGenRenderbuffers(1, &depthStencil_);
    glBindRenderbuffer(GL_RENDERBUFFER, depthStencil_);
    if (samples_ > 1) {
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_DEPTH24_STENCIL8, width_, height_);
    } else {
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width_, height_);
    }
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthStencil_);
    requireComplete(samples_ > 1 ? "multisample viewport" : "viewport");

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::bind() const {
    glBindFramebuffer(
        GL_FRAMEBUFFER,
        samples_ > 1 ? multisampleFramebuffer_ : resolveFramebuffer_
    );
}

void RenderTarget::resolveAndUnbind() const {
    if (samples_ > 1) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, multisampleFramebuffer_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFramebuffer_);
        glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool RenderTarget::savePng(const std::filesystem::path& path, std::string& error) const {
    if (resolveFramebuffer_ == 0U || width_ <= 0 || height_ <= 0) {
        error = "Viewport has not been rendered yet";
        return false;
    }
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4U
    );
    glBindFramebuffer(GL_READ_FRAMEBUFFER, resolveFramebuffer_);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    return writePng(path, width_, height_, pixels, error);
}

void RenderTarget::destroy() {
    if (depthStencil_ != 0U) glDeleteRenderbuffers(1, &depthStencil_);
    if (multisampleColor_ != 0U) glDeleteRenderbuffers(1, &multisampleColor_);
    if (colorTexture_ != 0U) glDeleteTextures(1, &colorTexture_);
    if (multisampleFramebuffer_ != 0U) glDeleteFramebuffers(1, &multisampleFramebuffer_);
    if (resolveFramebuffer_ != 0U) glDeleteFramebuffers(1, &resolveFramebuffer_);
    depthStencil_ = 0U;
    multisampleColor_ = 0U;
    colorTexture_ = 0U;
    multisampleFramebuffer_ = 0U;
    resolveFramebuffer_ = 0U;
    width_ = 0;
    height_ = 0;
    samples_ = 1;
}
