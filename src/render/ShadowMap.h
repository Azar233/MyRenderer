#pragma once

#include <cstddef>

class ShadowMap {
public:
    explicit ShadowMap(int resolution = 2048);
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    void bindForWriting() const;
    void bindTransmissionForWriting() const;
    void bindTexture(unsigned int unit) const;
    void bindTransmissionTexture(unsigned int unit) const;
    int resolution() const { return resolution_; }
    std::size_t estimatedBytes() const {
        return static_cast<std::size_t>(resolution_)
            * static_cast<std::size_t>(resolution_) * (4U + 8U);
    }

private:
    unsigned int framebuffer_{0};
    unsigned int depthTexture_{0};
    unsigned int transmissionFramebuffer_{0};
    unsigned int transmissionTexture_{0};
    int resolution_{2048};
};
