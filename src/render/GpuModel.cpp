#include "render/GpuModel.h"

#include <algorithm>
#include <cmath>

#include <glm/vec3.hpp>

#include "render/Mesh.h"
#include "render/Shader.h"
#include "render/Texture2D.h"

namespace {

glm::vec3 srgbToLinear(const glm::vec3& color) {
    glm::vec3 linear(0.0f);
    for (int channel = 0; channel < 3; ++channel) {
        const float value = std::clamp(color[channel], 0.0f, 1.0f);
        linear[channel] = value <= 0.04045f
            ? value / 12.92f
            : std::pow((value + 0.055f) / 1.055f, 2.4f);
    }
    return linear;
}

} // namespace

GpuModel::GpuModel(
    const ModelData& data,
    TextureCache& textureCache,
    std::vector<TextureUploadWarning>& warnings
)
    : whiteTexture_(textureCache.whiteTexture()),
      flatNormalTexture_(textureCache.flatNormalTexture()),
      linearWhiteTexture_(textureCache.linearWhiteTexture()) {
    textures_.reserve(data.textures.size());
    textureFallbacks_.reserve(data.textures.size());
    for (const auto& textureData : data.textures) {
        TextureLoadResult loaded = textureCache.load(textureData);
        if (loaded.usedFallback) {
            ++fallbackTextureCount_;
        } else {
            ++loadedTextureCount_;
            textureMemoryBytes_ += loaded.texture->estimatedBytes();
        }
        if (!loaded.warning.empty()) {
            warnings.push_back(TextureUploadWarning{
                textureData.name.empty() ? textureData.cacheKey : textureData.name,
                std::move(loaded.warning)
            });
        }
        textures_.push_back(std::move(loaded.texture));
        textureFallbacks_.push_back(loaded.usedFallback);
    }

    materials_.reserve(data.materials.size());
    for (const auto& materialData : data.materials) {
        GpuMaterial material;
        material.baseColorFactor = materialData.baseColorFactor;
        material.metallicFactor = std::clamp(materialData.metallicFactor, 0.0f, 1.0f);
        material.roughnessFactor = std::clamp(materialData.roughnessFactor, 0.04f, 1.0f);
        if (materialData.baseColorTextureIndex >= 0
            && static_cast<std::size_t>(materialData.baseColorTextureIndex) < textures_.size()) {
            material.baseColorTexture = textures_[static_cast<std::size_t>(materialData.baseColorTextureIndex)];
        } else {
            material.baseColorTexture = whiteTexture_;
        }
        if (materialData.normalTextureIndex >= 0
            && static_cast<std::size_t>(materialData.normalTextureIndex) < textures_.size()
            && !textureFallbacks_[static_cast<std::size_t>(materialData.normalTextureIndex)]) {
            material.normalTexture = textures_[static_cast<std::size_t>(materialData.normalTextureIndex)];
            material.hasNormalTexture = true;
        } else {
            material.normalTexture = flatNormalTexture_;
        }
        if (materialData.metallicRoughnessTextureIndex >= 0
            && static_cast<std::size_t>(materialData.metallicRoughnessTextureIndex) < textures_.size()
            && !textureFallbacks_[static_cast<std::size_t>(materialData.metallicRoughnessTextureIndex)]) {
            material.metallicRoughnessTexture = textures_[static_cast<std::size_t>(materialData.metallicRoughnessTextureIndex)];
            material.hasMetallicRoughnessTexture = true;
        } else {
            material.metallicRoughnessTexture = linearWhiteTexture_;
        }
        materials_.push_back(std::move(material));
    }

    meshes_.reserve(data.meshes.size());
    for (const auto& meshData : data.meshes) {
        auto mesh = std::make_unique<Mesh>(meshData);
        submeshCount_ += mesh->submeshCount();
        vertexCount_ += mesh->vertexCount();
        triangleCount_ += mesh->triangleCount();
        meshes_.push_back(std::move(mesh));
    }
}

GpuModel::~GpuModel() = default;

void GpuModel::draw(const Shader& shader, const glm::vec3& tint) const {
    shader.setInt("uBaseColorTexture", 0);
    shader.setInt("uNormalTexture", 1);
    shader.setInt("uMetallicRoughnessTexture", 2);
    const glm::vec3 linearTint = srgbToLinear(tint);
    for (const auto& mesh : meshes_) {
        mesh->draw([&](std::int32_t materialIndex) {
            const GpuMaterial* material = nullptr;
            if (materialIndex >= 0 && static_cast<std::size_t>(materialIndex) < materials_.size()) {
                material = &materials_[static_cast<std::size_t>(materialIndex)];
            }
            const glm::vec3 baseColor = material == nullptr
                ? linearTint
                : linearTint * glm::vec3(material->baseColorFactor);
            shader.setVec3("uBaseColor", baseColor);
            const auto& texture = material == nullptr ? whiteTexture_ : material->baseColorTexture;
            texture->bind(0U);
            const auto& normalTexture = material == nullptr ? flatNormalTexture_ : material->normalTexture;
            normalTexture->bind(1U);
            shader.setBool("uHasNormalTexture", material != nullptr && material->hasNormalTexture);
            const auto& metallicRoughnessTexture = material == nullptr
                ? linearWhiteTexture_
                : material->metallicRoughnessTexture;
            metallicRoughnessTexture->bind(2U);
            shader.setBool(
                "uHasMetallicRoughnessTexture",
                material != nullptr && material->hasMetallicRoughnessTexture
            );
            shader.setFloat("uMetallicFactor", material == nullptr ? 0.0f : material->metallicFactor);
            shader.setFloat("uRoughnessFactor", material == nullptr ? 1.0f : material->roughnessFactor);
        });
    }
}

void GpuModel::drawDepth() const {
    for (const auto& mesh : meshes_) {
        mesh->draw([](std::int32_t) {});
    }
}
