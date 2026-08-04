#pragma once

class RenderTarget {
public:
    RenderTarget() = default;
    ~RenderTarget();

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    void resize(int width, int height);
    void bind() const;
    static void unbind();
    void destroy();

    unsigned int colorTexture() const { return colorTexture_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    unsigned int framebuffer_{0};
    unsigned int colorTexture_{0};
    unsigned int depthStencil_{0};
    int width_{0};
    int height_{0};
};
