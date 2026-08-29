#include "render/GpuModel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <glad/gl.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

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
    boundsCenter_ = 0.5f * (data.boundsMin + data.boundsMax);
    boundsRadius_ = glm::length(0.5f * (data.boundsMax - data.boundsMin));
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
        material.transmissionFactor = std::clamp(materialData.transmissionFactor, 0.0f, 1.0f);
        material.indexOfRefraction = std::max(materialData.indexOfRefraction, 1.0f);
        material.dispersion = std::max(materialData.dispersion, 0.0f);
        material.thicknessFactor = std::max(materialData.thicknessFactor, 0.0f);
        material.attenuationColor = glm::clamp(
            materialData.attenuationColor,
            glm::vec3(0.0f),
            glm::vec3(1.0f)
        );
        material.attenuationDistance = materialData.attenuationDistance > 0.0f
            ? materialData.attenuationDistance
            : std::numeric_limits<float>::infinity();
        material.alphaMode = materialData.alphaMode;
        material.alphaCutoff = std::max(materialData.alphaCutoff, 0.0f);
        material.doubleSided = materialData.doubleSided;
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
        if (materialData.thicknessTextureIndex >= 0
            && static_cast<std::size_t>(materialData.thicknessTextureIndex) < textures_.size()
            && !textureFallbacks_[static_cast<std::size_t>(materialData.thicknessTextureIndex)]) {
            material.thicknessTexture = textures_[static_cast<std::size_t>(materialData.thicknessTextureIndex)];
            material.hasThicknessTexture = true;
        } else {
            material.thicknessTexture = linearWhiteTexture_;
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

    for (std::size_t meshIndex = 0; meshIndex < meshes_.size(); ++meshIndex) {
        const Mesh& mesh = *meshes_[meshIndex];
        for (std::size_t submeshIndex = 0; submeshIndex < mesh.submeshCount(); ++submeshIndex) {
            const std::int32_t materialIndex = mesh.submeshMaterialIndex(submeshIndex);
            const bool isBlended = materialIndex >= 0
                && static_cast<std::size_t>(materialIndex) < materials_.size()
                && (materials_[static_cast<std::size_t>(materialIndex)].alphaMode == MaterialAlphaMode::Blend
                    || materials_[static_cast<std::size_t>(materialIndex)].transmissionFactor > 0.0f);
            DrawCommand command{
                meshIndex,
                submeshIndex,
                materialIndex,
                mesh.submeshCenter(submeshIndex)
            };
            (isBlended ? transparentDrawCommands_ : opaqueDrawCommands_).push_back(command);
            if (materialIndex >= 0
                && static_cast<std::size_t>(materialIndex) < materials_.size()
                && materials_[static_cast<std::size_t>(materialIndex)].transmissionFactor > 0.0f) {
                transmissiveDrawCommands_.push_back(command);
            }
        }
    }
}

GpuModel::~GpuModel() = default;

const GpuModel::GpuMaterial* GpuModel::bindMaterial(
    const Shader& shader,
    const glm::vec3& linearTint,
    std::int32_t materialIndex
) const {
    shader.setInt("uBaseColorTexture", 0);
    shader.setInt("uNormalTexture", 1);
    shader.setInt("uMetallicRoughnessTexture", 2);
    shader.setInt("uThicknessTexture", 7);
    const GpuMaterial* material = nullptr;
    if (materialIndex >= 0 && static_cast<std::size_t>(materialIndex) < materials_.size()) {
        material = &materials_[static_cast<std::size_t>(materialIndex)];
    }
    const glm::vec4 baseColor = material == nullptr
        ? glm::vec4(linearTint, 1.0f)
        : glm::vec4(linearTint * glm::vec3(material->baseColorFactor), material->baseColorFactor.a);
    shader.setVec4("uBaseColor", baseColor);
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
    const auto& thicknessTexture = material == nullptr
        ? linearWhiteTexture_
        : material->thicknessTexture;
    thicknessTexture->bind(7U);
    shader.setBool("uHasThicknessTexture", material != nullptr && material->hasThicknessTexture);
    shader.setFloat("uMetallicFactor", material == nullptr ? 0.0f : material->metallicFactor);
    shader.setFloat("uRoughnessFactor", material == nullptr ? 1.0f : material->roughnessFactor);
    shader.setFloat("uTransmissionFactor", material == nullptr ? 0.0f : material->transmissionFactor);
    shader.setFloat("uIndexOfRefraction", material == nullptr ? 1.5f : material->indexOfRefraction);
    shader.setFloat("uMaterialDispersion", material == nullptr ? 0.0f : material->dispersion);
    shader.setFloat("uThicknessFactor", material == nullptr ? 0.0f : material->thicknessFactor);
    shader.setVec3(
        "uAttenuationColor",
        material == nullptr ? glm::vec3(1.0f) : material->attenuationColor
    );
    shader.setFloat(
        "uAttenuationDistance",
        material == nullptr || !std::isfinite(material->attenuationDistance)
            ? 1.0e20f
            : material->attenuationDistance
    );
    shader.setInt(
        "uAlphaMode",
        material == nullptr ? 0 : static_cast<int>(material->alphaMode)
    );
    shader.setFloat("uAlphaCutoff", material == nullptr ? 0.5f : material->alphaCutoff);
    shader.setBool("uDoubleSided", material != nullptr && material->doubleSided);
    return material;
}

void GpuModel::applyCullState(const GpuMaterial* material, bool cullBackFaces) const {
    const bool isVolumeBoundary = material != nullptr
        && material->transmissionFactor > 0.0f
        && material->thicknessFactor > 0.0f;
    if (isVolumeBoundary) {
        // Glass-2B selects the nearest volume surface from the front-depth
        // texture in the fragment shader, independent of mesh winding.
        glDisable(GL_CULL_FACE);
    } else if (cullBackFaces && (material == nullptr || !material->doubleSided)) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
    } else {
        glDisable(GL_CULL_FACE);
    }
}

void GpuModel::drawOpaque(const Shader& shader, const glm::vec3& tint, bool cullBackFaces) const {
    shader.setBool("uInstanced", false);
    const glm::vec3 linearTint = srgbToLinear(tint);
    for (const DrawCommand& command : opaqueDrawCommands_) {
        const GpuMaterial* material = bindMaterial(shader, linearTint, command.materialIndex);
        applyCullState(material, cullBackFaces);
        meshes_[command.meshIndex]->drawSubmesh(command.submeshIndex);
    }
}

void GpuModel::drawOpaqueInstanced(
    const Shader& shader,
    const glm::vec3& tint,
    const std::vector<glm::mat4>& modelMatrices,
    std::size_t lodLevel,
    bool cullBackFaces
) const {
    if (modelMatrices.empty()) return;
    shader.setBool("uInstanced", true);
    const glm::vec3 linearTint = srgbToLinear(tint);
    for (const DrawCommand& command : opaqueDrawCommands_) {
        const GpuMaterial* material = bindMaterial(shader, linearTint, command.materialIndex);
        applyCullState(material, cullBackFaces);
        meshes_[command.meshIndex]->drawSubmeshInstanced(
            command.submeshIndex,
            lodLevel,
            modelMatrices
        );
    }
    shader.setBool("uInstanced", false);
}

std::size_t GpuModel::lodTriangleCount(std::size_t lodLevel) const {
    std::size_t count = 0U;
    for (const auto& mesh : meshes_) count += mesh->lodTriangleCount(lodLevel);
    return count;
}

void GpuModel::drawTransparentSubmesh(
    const Shader& shader,
    const glm::vec3& tint,
    std::size_t transparentSubmeshIndex,
    bool cullBackFaces
) const {
    if (transparentSubmeshIndex >= transparentDrawCommands_.size()) {
        throw std::out_of_range("Transparent submesh index is out of range");
    }
    const DrawCommand& command = transparentDrawCommands_[transparentSubmeshIndex];
    const glm::vec3 linearTint = srgbToLinear(tint);
    const GpuMaterial* material = bindMaterial(shader, linearTint, command.materialIndex);
    applyCullState(material, cullBackFaces);
    meshes_[command.meshIndex]->drawSubmesh(command.submeshIndex);
}

const glm::vec3& GpuModel::transparentSubmeshCenter(std::size_t transparentSubmeshIndex) const {
    if (transparentSubmeshIndex >= transparentDrawCommands_.size()) {
        throw std::out_of_range("Transparent submesh index is out of range");
    }
    return transparentDrawCommands_[transparentSubmeshIndex].localCenter;
}

bool GpuModel::transparentSubmeshIsTransmissive(
    std::size_t transparentSubmeshIndex
) const {
    if (transparentSubmeshIndex >= transparentDrawCommands_.size()) {
        throw std::out_of_range("Transparent submesh index is out of range");
    }
    const std::int32_t materialIndex =
        transparentDrawCommands_[transparentSubmeshIndex].materialIndex;
    return materialIndex >= 0
        && static_cast<std::size_t>(materialIndex) < materials_.size()
        && materials_[static_cast<std::size_t>(materialIndex)].transmissionFactor > 0.0f;
}

void GpuModel::drawDepth() const {
    for (const DrawCommand& command : opaqueDrawCommands_) {
        meshes_[command.meshIndex]->drawSubmesh(command.submeshIndex);
    }
}

void GpuModel::drawTransmissiveDepth() const {
    for (const DrawCommand& command : transmissiveDrawCommands_) {
        meshes_[command.meshIndex]->drawSubmesh(command.submeshIndex);
    }
}

void GpuModel::drawTransmissive(const Shader& shader, const glm::vec3& tint) const {
    const glm::vec3 linearTint = srgbToLinear(tint);
    for (const DrawCommand& command : transmissiveDrawCommands_) {
        bindMaterial(shader, linearTint, command.materialIndex);
        glDisable(GL_CULL_FACE);
        meshes_[command.meshIndex]->drawSubmesh(command.submeshIndex);
    }
}
