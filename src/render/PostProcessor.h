#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>

#include <glm/mat4x4.hpp>

class RenderTarget;
class Shader;

struct PostProcessSettings {
    bool toneMapping{true};
    bool bloom{true};
    bool encodeSrgb{true};
    float exposure{1.0f};
    float bloomThreshold{1.0f};
    float bloomIntensity{0.12f};
    bool temporalAa{false};
    int temporalDebugView{0};
    bool resetTemporalHistory{false};
    float temporalHistoryWeight{0.9f};
    unsigned int depthTexture{0};
    unsigned int objectMotionTexture{0};
    glm::mat4 inverseCurrentViewProjection{1.0f};
    glm::mat4 previousViewProjection{1.0f};
};

class PostProcessor {
public:
    PostProcessor(
        const std::filesystem::path& fullscreenVertex,
        const std::filesystem::path& extractFragment,
        const std::filesystem::path& blurFragment,
        const std::filesystem::path& compositeFragment,
        const std::filesystem::path& temporalFragment
    );
    ~PostProcessor();

    PostProcessor(const PostProcessor&) = delete;
    PostProcessor& operator=(const PostProcessor&) = delete;

    void process(RenderTarget& target, const PostProcessSettings& settings);
    std::size_t estimatedBytes() const;

private:
    void resize(int width, int height);
    void drawFullscreen() const;

    std::unique_ptr<Shader> extractShader_;
    std::unique_ptr<Shader> blurShader_;
    std::unique_ptr<Shader> compositeShader_;
    std::unique_ptr<Shader> temporalShader_;
    unsigned int vertexArray_{0};
    unsigned int framebuffers_[2]{};
    unsigned int textures_[2]{};
    unsigned int temporalFramebuffers_[2]{};
    unsigned int historyColorTextures_[2]{};
    unsigned int historyDepthTextures_[2]{};
    unsigned int motionTextures_[2]{};
    int width_{0};
    int height_{0};
    int historyIndex_{0};
    bool historyValid_{false};
};
