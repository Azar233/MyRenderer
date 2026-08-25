#pragma once

#include <cstddef>

class GBuffer {
public:
    GBuffer() = default;
    ~GBuffer();

    GBuffer(const GBuffer&) = delete;
    GBuffer& operator=(const GBuffer&) = delete;

    void resize(int width, int height, int samples);
    void bindForGeometry() const;
    void resolve() const;
    void bindTextures(
        unsigned int albedoUnit,
        unsigned int normalUnit,
        unsigned int materialUnit,
        unsigned int depthUnit
    ) const;
    void destroy();

    unsigned int framebuffer() const { return framebuffer_; }
    unsigned int albedoTexture() const { return albedoTexture_; }
    unsigned int normalTexture() const { return normalTexture_; }
    unsigned int materialTexture() const { return materialTexture_; }
    unsigned int depthTexture() const { return depthTexture_; }
    std::size_t estimatedBytes() const;

private:
    unsigned int framebuffer_{0};
    unsigned int multisampleFramebuffer_{0};
    unsigned int albedoTexture_{0};
    unsigned int normalTexture_{0};
    unsigned int materialTexture_{0};
    unsigned int depthTexture_{0};
    unsigned int multisampleAlbedo_{0};
    unsigned int multisampleNormal_{0};
    unsigned int multisampleMaterial_{0};
    unsigned int multisampleDepthStencil_{0};
    int width_{0};
    int height_{0};
    int samples_{1};
};
