#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "render/OpenGlStateCache.h"

struct RenderPassContext {
    RenderPassContext() = default;
    explicit RenderPassContext(std::string passName) : name(std::move(passName)) {}

    std::string name;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    int viewportWidth{0};
    int viewportHeight{0};
    GLbitfield clearMask{0U};
    RenderState state;
};

class RenderPassSequence {
public:
    RenderPassSequence(int viewportWidth = 0, int viewportHeight = 0)
        : defaultViewportWidth_(viewportWidth),
          defaultViewportHeight_(viewportHeight) {}

    void add(RenderPassContext context, std::function<void()> execute) {
        names_.push_back(context.name);
        contexts_.push_back(context);
        passes_.push_back({std::move(context), std::move(execute)});
    }
    void add(std::string name, std::function<void()> execute) {
        RenderPassContext context(std::move(name));
        context.inputs.push_back("Scene + RendererSettings");
        context.outputs.push_back(context.name + " output");
        context.viewportWidth = defaultViewportWidth_;
        context.viewportHeight = defaultViewportHeight_;
        add(std::move(context), std::move(execute));
    }
    using PassCallback = std::function<void(std::size_t, const RenderPassContext&)>;

    void run(
        const PassCallback& before = {},
        const PassCallback& after = {}
    ) const {
        for (std::size_t index = 0; index < passes_.size(); ++index) {
            const auto& pass = passes_[index];
            if (before) before(index, pass.context);
            pass.execute();
            if (after) after(index, pass.context);
        }
    }
    const std::vector<std::string>& names() const { return names_; }
    const std::vector<RenderPassContext>& contexts() const { return contexts_; }
    std::size_t size() const { return passes_.size(); }

private:
    struct Pass { RenderPassContext context; std::function<void()> execute; };
    std::vector<Pass> passes_;
    std::vector<std::string> names_;
    std::vector<RenderPassContext> contexts_;
    int defaultViewportWidth_{0};
    int defaultViewportHeight_{0};
};
