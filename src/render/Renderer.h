#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class Camera;
class GpuModel;
class RenderTarget;
class Shader;
class TextureCache;

struct RendererSettings {
    glm::vec3 backgroundColor{0.055f, 0.065f, 0.085f};
    glm::vec3 baseColor{1.0f};
    glm::vec3 lightDirection{-0.45f, -0.8f, -0.35f};
    float ambientStrength{0.18f};
    float diffuseStrength{0.92f};
    float specularStrength{0.28f};
    float shininess{48.0f};
    int msaaSamples{4};
    bool wireframe{false};
    bool cullBackFaces{false};
    bool normalMapping{true};
};

class Renderer {
public:
    Renderer(const std::filesystem::path& vertexShaderPath, const std::filesystem::path& fragmentShaderPath);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void render(
        const GpuModel* model,
        const Camera& camera,
        const glm::mat4& modelMatrix,
        const RendererSettings& settings,
        int width,
        int height
    );

    unsigned int colorTexture() const;
    bool saveScreenshot(const std::filesystem::path& path, std::string& error) const;
    int activeMsaaSamples() const;
    TextureCache& textureCache();

private:
    std::unique_ptr<Shader> shader_;
    std::unique_ptr<RenderTarget> renderTarget_;
    std::unique_ptr<TextureCache> textureCache_;
};
