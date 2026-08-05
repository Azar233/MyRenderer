#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 texCoord0{0.0f};
    // xyz stores the tangent; w stores the bitangent handedness.
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
};

struct MaterialData {
    std::string name{"Default"};
    glm::vec4 baseColorFactor{1.0f};
    std::filesystem::path baseColorTexture;
    std::filesystem::path normalTexture;
    float metallicFactor{0.0f};
    float roughnessFactor{1.0f};
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
    glm::vec3 boundsMin{std::numeric_limits<float>::max()};
    glm::vec3 boundsMax{std::numeric_limits<float>::lowest()};
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
    ModelNodeData rootNode;
    glm::vec3 boundsMin{std::numeric_limits<float>::max()};
    glm::vec3 boundsMax{std::numeric_limits<float>::lowest()};
};
