#include "render/OpenGlStateCache.h"

void OpenGlStateCache::invalidate() {
    depthTest_.reset();
    depthWrite_.reset();
    depthFunction_.reset();
    blend_.reset();
    blendSourceRgb_.reset();
    blendDestinationRgb_.reset();
    blendSourceAlpha_.reset();
    blendDestinationAlpha_.reset();
    blendEquation_.reset();
    cull_.reset();
    cullFace_.reset();
    frontFace_.reset();
    polygonMode_.reset();
}

void OpenGlStateCache::apply(const RenderState& state) {
    setCapability(GL_DEPTH_TEST, state.depthTest, depthTest_);
    if (!depthWrite_.has_value() || *depthWrite_ != state.depthWrite) {
        glDepthMask(state.depthWrite ? GL_TRUE : GL_FALSE);
        depthWrite_ = state.depthWrite;
    }
    if (!depthFunction_.has_value() || *depthFunction_ != state.depthFunction) {
        glDepthFunc(state.depthFunction);
        depthFunction_ = state.depthFunction;
    }
    setCapability(GL_BLEND, state.blend, blend_);
    if (!blendEquation_.has_value() || *blendEquation_ != state.blendEquation) {
        glBlendEquation(state.blendEquation);
        blendEquation_ = state.blendEquation;
    }
    if (!blendSourceRgb_.has_value()
        || *blendSourceRgb_ != state.blendSourceRgb
        || !blendDestinationRgb_.has_value()
        || *blendDestinationRgb_ != state.blendDestinationRgb
        || !blendSourceAlpha_.has_value()
        || *blendSourceAlpha_ != state.blendSourceAlpha
        || !blendDestinationAlpha_.has_value()
        || *blendDestinationAlpha_ != state.blendDestinationAlpha) {
        glBlendFuncSeparate(
            state.blendSourceRgb,
            state.blendDestinationRgb,
            state.blendSourceAlpha,
            state.blendDestinationAlpha
        );
        blendSourceRgb_ = state.blendSourceRgb;
        blendDestinationRgb_ = state.blendDestinationRgb;
        blendSourceAlpha_ = state.blendSourceAlpha;
        blendDestinationAlpha_ = state.blendDestinationAlpha;
    }
    setCapability(GL_CULL_FACE, state.cull, cull_);
    if (!cullFace_.has_value() || *cullFace_ != state.cullFace) {
        glCullFace(state.cullFace);
        cullFace_ = state.cullFace;
    }
    if (!frontFace_.has_value() || *frontFace_ != state.frontFace) {
        glFrontFace(state.frontFace);
        frontFace_ = state.frontFace;
    }
    if (!polygonMode_.has_value() || *polygonMode_ != state.polygonMode) {
        glPolygonMode(GL_FRONT_AND_BACK, state.polygonMode);
        polygonMode_ = state.polygonMode;
    }
}

void OpenGlStateCache::setCapability(
    GLenum capability,
    bool enabled,
    std::optional<bool>& cached
) {
    if (cached.has_value() && *cached == enabled) return;
    if (enabled) glEnable(capability);
    else glDisable(capability);
    cached = enabled;
}
