#pragma once

#include <filesystem>
#include <string>

class RenderTarget {
public:
    RenderTarget() = default;
    ~RenderTarget();

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    void resize(int width, int height, int samples);
    void bind() const;
    void resolveAndUnbind() const;
    bool savePng(const std::filesystem::path& path, std::string& error) const;
    void destroy();

    unsigned int colorTexture() const { return colorTexture_; }
    int width() const { return width_; }
    int height() const { return height_; }
    int samples() const { return samples_; }

private:
    unsigned int resolveFramebuffer_{0};
    unsigned int multisampleFramebuffer_{0};
    unsigned int multisampleColor_{0};
    unsigned int colorTexture_{0};
    unsigned int depthStencil_{0};
    int width_{0};
    int height_{0};
    int samples_{1};
};
