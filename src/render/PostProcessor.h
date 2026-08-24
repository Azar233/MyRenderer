#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>

class RenderTarget;
class Shader;

struct PostProcessSettings {
    bool toneMapping{true};
    bool bloom{true};
    float exposure{1.0f};
    float bloomThreshold{1.0f};
    float bloomIntensity{0.12f};
};

class PostProcessor {
public:
    PostProcessor(
        const std::filesystem::path& fullscreenVertex,
        const std::filesystem::path& extractFragment,
        const std::filesystem::path& blurFragment,
        const std::filesystem::path& compositeFragment
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
    unsigned int vertexArray_{0};
    unsigned int framebuffers_[2]{};
    unsigned int textures_[2]{};
    int width_{0};
    int height_{0};
};
