#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "asset/ModelData.h"

class Mesh;
class Shader;
class Texture2D;
class TextureCache;

struct TextureUploadWarning {
    std::string textureName;
    std::string message;
};

class GpuModel {
public:
    GpuModel(
        const ModelData& data,
        TextureCache& textureCache,
        std::vector<TextureUploadWarning>& warnings
    );
    ~GpuModel();

    GpuModel(const GpuModel&) = delete;
    GpuModel& operator=(const GpuModel&) = delete;

    void draw(const Shader& shader, const glm::vec3& tint) const;
    void drawDepth() const;

    std::size_t meshCount() const { return meshes_.size(); }
    std::size_t submeshCount() const { return submeshCount_; }
    std::size_t vertexCount() const { return vertexCount_; }
    std::size_t triangleCount() const { return triangleCount_; }
    std::size_t materialCount() const { return materials_.size(); }
    std::size_t textureCount() const { return textures_.size(); }
    std::size_t loadedTextureCount() const { return loadedTextureCount_; }
    std::size_t fallbackTextureCount() const { return fallbackTextureCount_; }
    std::size_t textureMemoryBytes() const { return textureMemoryBytes_; }

private:
    struct GpuMaterial {
        glm::vec4 baseColorFactor{1.0f};
        std::shared_ptr<Texture2D> baseColorTexture;
        std::shared_ptr<Texture2D> normalTexture;
        std::shared_ptr<Texture2D> metallicRoughnessTexture;
        bool hasNormalTexture{false};
        bool hasMetallicRoughnessTexture{false};
        float metallicFactor{0.0f};
        float roughnessFactor{1.0f};
    };

    std::vector<std::unique_ptr<Mesh>> meshes_;
    std::vector<GpuMaterial> materials_;
    std::vector<std::shared_ptr<Texture2D>> textures_;
    std::vector<bool> textureFallbacks_;
    std::shared_ptr<Texture2D> whiteTexture_;
    std::shared_ptr<Texture2D> flatNormalTexture_;
    std::shared_ptr<Texture2D> linearWhiteTexture_;
    std::size_t submeshCount_{0};
    std::size_t vertexCount_{0};
    std::size_t triangleCount_{0};
    std::size_t loadedTextureCount_{0};
    std::size_t fallbackTextureCount_{0};
    std::size_t textureMemoryBytes_{0};
};
