#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 texCoord0{0.0f};
    // xyz stores the tangent; w stores the bitangent handedness.
    // w is zero when no valid tangent basis is available, otherwise +/-1 handedness.
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 0.0f};
    glm::uvec4 jointIndices{0U};
    glm::vec4 jointWeights{0.0f};
};

struct SkinJointData {
    std::uint32_t nodeIndex{0U};
    glm::mat4 inverseBindMatrix{1.0f};
};

// A format-independent texture source. Importers either provide an external
// path, compressed image bytes, or uncompressed RGBA8 pixels. The render layer
// never needs to know whether the original asset was OBJ, DAE, glTF, or GLB.
struct TextureData {
    std::string name;
    std::string cacheKey;
    std::filesystem::path sourcePath;
    std::vector<std::uint8_t> encodedData;
    std::vector<std::uint8_t> rgbaPixels;
    std::uint32_t width{0};
    std::uint32_t height{0};
    // Color textures use sRGB sampling; data textures such as normal maps stay linear.
    bool srgb{false};
};

enum class MaterialAlphaMode {
    Opaque,
    Mask,
    Blend
};

struct MaterialData {
    std::string name{"Default"};
    glm::vec4 baseColorFactor{1.0f};
    std::int32_t baseColorTextureIndex{-1};
    std::int32_t normalTextureIndex{-1};
    // glTF packs roughness in G and metallic in B.
    std::int32_t metallicRoughnessTextureIndex{-1};
    float metallicFactor{0.0f};
    float roughnessFactor{1.0f};
    float transmissionFactor{0.0f};
    float indexOfRefraction{1.5f};
    // KHR_materials_dispersion. Zero disables wavelength-dependent IOR.
    float dispersion{0.0f};
    // KHR_materials_volume. A zero thickness keeps the material thin-walled.
    float thicknessFactor{0.0f};
    // KHR_materials_volume stores its scalar thickness texture in the G channel.
    std::int32_t thicknessTextureIndex{-1};
    glm::vec3 attenuationColor{1.0f};
    float attenuationDistance{std::numeric_limits<float>::infinity()};
    MaterialAlphaMode alphaMode{MaterialAlphaMode::Opaque};
    float alphaCutoff{0.5f};
    bool doubleSided{false};
};

struct SubmeshData {
    std::string name;
    std::uint32_t firstIndex{0};
    std::uint32_t indexCount{0};
    std::int32_t materialIndex{-1};
};

struct MeshData {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<SubmeshData> submeshes;
    std::vector<SkinJointData> skinJoints;
    glm::vec3 boundsMin{std::numeric_limits<float>::max()};
    glm::vec3 boundsMax{std::numeric_limits<float>::lowest()};
};

struct SkeletonNodeData {
    std::string name;
    std::int32_t parentIndex{-1};
    glm::vec3 bindTranslation{0.0f};
    glm::quat bindRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 bindScale{1.0f};
};

struct AnimationVectorKey {
    float timeSeconds{0.0f};
    glm::vec3 value{0.0f};
};

struct AnimationRotationKey {
    float timeSeconds{0.0f};
    glm::quat value{1.0f, 0.0f, 0.0f, 0.0f};
};

struct AnimationChannelData {
    std::uint32_t nodeIndex{0U};
    std::vector<AnimationVectorKey> translations;
    std::vector<AnimationRotationKey> rotations;
    std::vector<AnimationVectorKey> scales;
};

struct AnimationClipData {
    std::string name;
    float durationSeconds{0.0f};
    std::vector<AnimationChannelData> channels;
};

struct ModelNodeData {
    std::string name;
    glm::mat4 localTransform{1.0f};
    std::vector<std::uint32_t> meshIndices;
    std::vector<ModelNodeData> children;
};

struct ModelData {
    std::string name;
    std::filesystem::path sourcePath;
    std::vector<MeshData> meshes;
    std::vector<MaterialData> materials;
    std::vector<TextureData> textures;
    std::vector<SkeletonNodeData> skeletonNodes;
    std::vector<AnimationClipData> animations;
    ModelNodeData rootNode;
    glm::vec3 boundsMin{std::numeric_limits<float>::max()};
    glm::vec3 boundsMax{std::numeric_limits<float>::lowest()};
};
