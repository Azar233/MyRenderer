#pragma once

#include <cstddef>

// Directional shadow map.
//
// The depth attachment is a `GL_TEXTURE_2D_ARRAY` with one layer per shadow cascade, which is how the
// cascaded path (P1-A slice 3) will index a cascade from a single sampler binding. Both
// `GL_TEXTURE_2D_ARRAY` and `glFramebufferTextureLayer` are core since OpenGL 3.0.
//
// The array type and the shaders' sampler type are one contract: binding a 2D-array texture to a
// `sampler2D` is not an error but undefined sampling, which shows up only as drifting baselines. So
// `basic.frag` and `deferred_lighting.frag` declare `sampler2DArray uShadowMap` and always sample with
// an explicit layer, even while only layer 0 is written.
class ShadowMap {
public:
    explicit ShadowMap(int resolution = 2048, int layers = 4);
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    // Binds the framebuffer and attaches `layer` as the depth attachment. `attachLayer` is separate so
    // a caller can render several cascades without rebinding the framebuffer per layer.
    void bindForWriting(std::size_t layer = 0U) const;
    void bindTransmissionForWriting() const;
    void bindTexture(unsigned int unit) const;
    void bindTransmissionTexture(unsigned int unit) const;

    int resolution() const { return resolution_; }
    int layerCount() const { return layers_; }

    std::size_t estimatedBytes() const {
        return static_cast<std::size_t>(resolution_) * static_cast<std::size_t>(resolution_)
            * (static_cast<std::size_t>(layers_) * 4U + 8U);
    }

private:
    unsigned int framebuffer_{0};
    unsigned int depthTexture_{0};
    unsigned int transmissionFramebuffer_{0};
    unsigned int transmissionTexture_{0};
    int resolution_{2048};
    int layers_{4};
    // The layer currently attached, so re-binding the same layer does not re-attach it. Mutable because
    // binding is a const operation on the map's own state, not a change to the resource.
    mutable int attachedLayer_{-1};
};
