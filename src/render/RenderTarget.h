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
    void bindOpaqueScene() const;
    void resolveOpaqueScene() const;
    void bindRefractiveScene() const;
    void bindHdrSceneForOverlay() const;
    void bindFinal() const;
    void unbind() const;
    bool savePng(const std::filesystem::path& path, std::string& error) const;
    void destroy();

    unsigned int colorTexture() const { return finalColorTexture_; }
    unsigned int opaqueColorTexture() const { return opaqueColorTexture_; }
    unsigned int hdrColorTexture() const { return hdrColorTexture_; }
    unsigned int sceneDepthTexture() const { return sceneDepthTexture_; }
    int width() const { return width_; }
    int height() const { return height_; }
    int samples() const { return samples_; }
    int opaqueColorMaximumMipLevel() const { return opaqueColorMipLevels_ - 1; }

private:
    unsigned int opaqueFramebuffer_{0};
    unsigned int sceneFramebuffer_{0};
    unsigned int finalFramebuffer_{0};
    unsigned int multisampleFramebuffer_{0};
    unsigned int multisampleColor_{0};
    unsigned int multisampleDepthStencil_{0};
    unsigned int refractiveDepthStencil_{0};
    unsigned int opaqueColorTexture_{0};
    unsigned int hdrColorTexture_{0};
    unsigned int sceneDepthTexture_{0};
    unsigned int finalColorTexture_{0};
    int width_{0};
    int height_{0};
    int samples_{1};
    int opaqueColorMipLevels_{1};
};
