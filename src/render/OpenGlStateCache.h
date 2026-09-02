#pragma once

#include <optional>

#include <glad/gl.h>

struct RenderState {
    bool depthTest{true};
    bool depthWrite{true};
    GLenum depthFunction{GL_LESS};
    bool blend{false};
    GLenum blendSourceRgb{GL_ONE};
    GLenum blendDestinationRgb{GL_ZERO};
    GLenum blendSourceAlpha{GL_ONE};
    GLenum blendDestinationAlpha{GL_ZERO};
    GLenum blendEquation{GL_FUNC_ADD};
    bool cull{false};
    GLenum cullFace{GL_BACK};
    GLenum frontFace{GL_CCW};
    GLenum polygonMode{GL_FILL};
};

class OpenGlStateCache {
public:
    void invalidate();
    void apply(const RenderState& state);

private:
    void setCapability(GLenum capability, bool enabled, std::optional<bool>& cached);

    std::optional<bool> depthTest_;
    std::optional<bool> depthWrite_;
    std::optional<GLenum> depthFunction_;
    std::optional<bool> blend_;
    std::optional<GLenum> blendSourceRgb_;
    std::optional<GLenum> blendDestinationRgb_;
    std::optional<GLenum> blendSourceAlpha_;
    std::optional<GLenum> blendDestinationAlpha_;
    std::optional<GLenum> blendEquation_;
    std::optional<bool> cull_;
    std::optional<GLenum> cullFace_;
    std::optional<GLenum> frontFace_;
    std::optional<GLenum> polygonMode_;
};
