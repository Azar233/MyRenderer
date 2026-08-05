#pragma once

class ShadowMap {
public:
    explicit ShadowMap(int resolution = 2048);
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    void bindForWriting() const;
    void bindTexture(unsigned int unit) const;
    int resolution() const { return resolution_; }

private:
    unsigned int framebuffer_{0};
    unsigned int depthTexture_{0};
    int resolution_{2048};
};
