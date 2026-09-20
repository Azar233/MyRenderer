#include "asset/BuiltinModels.h"

#include <utility>
#include <vector>

ModelData makeGroundPlaneData() {
    constexpr float halfExtent = 4.0f;
    MeshData mesh;
    mesh.name = "Ground receiver";
    mesh.vertices = {
        Vertex{glm::vec3(-halfExtent, 0.0f, -halfExtent), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.0f, 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)},
        Vertex{glm::vec3( halfExtent, 0.0f, -halfExtent), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(4.0f, 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)},
        Vertex{glm::vec3( halfExtent, 0.0f,  halfExtent), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(4.0f, 4.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)},
        Vertex{glm::vec3(-halfExtent, 0.0f,  halfExtent), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.0f, 4.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)}
    };
    mesh.indices = {0U, 2U, 1U, 0U, 3U, 2U};
    mesh.submeshes.push_back(SubmeshData{"Ground receiver", 0U, 6U, 0});
    mesh.boundsMin = glm::vec3(-halfExtent, 0.0f, -halfExtent);
    mesh.boundsMax = glm::vec3(halfExtent, 0.0f, halfExtent);

    MaterialData material;
    material.name = "Ground matte";
    material.roughnessFactor = 0.82f;
    material.metallicFactor = 0.0f;

    ModelData model;
    model.name = "Procedural ground receiver";
    model.meshes.push_back(std::move(mesh));
    model.materials.push_back(std::move(material));
    model.rootNode.name = "Ground root";
    model.rootNode.meshIndices.push_back(0U);
    model.boundsMin = glm::vec3(-halfExtent, 0.0f, -halfExtent);
    model.boundsMax = glm::vec3(halfExtent, 0.0f, halfExtent);
    return model;
}

ModelData makeGlassCheckerboardData() {
    constexpr int columns = 10;
    constexpr int rows = 7;
    constexpr float cellSize = 0.34f;
    MeshData mesh;
    mesh.name = "Glass-2C checkerboard backdrop";
    std::vector<std::uint32_t> darkIndices;
    std::vector<std::uint32_t> lightIndices;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const float left = (static_cast<float>(column) - columns * 0.5f) * cellSize;
            const float right = left + cellSize;
            const float bottom = (static_cast<float>(row) - rows * 0.5f) * cellSize;
            const float top = bottom + cellSize;
            const std::uint32_t first = static_cast<std::uint32_t>(mesh.vertices.size());
            const glm::vec3 normal(0.0f, 0.0f, 1.0f);
            const glm::vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);
            mesh.vertices.push_back(Vertex{glm::vec3(left, bottom, -1.05f), normal, glm::vec2(0.0f), tangent});
            mesh.vertices.push_back(Vertex{glm::vec3(right, bottom, -1.05f), normal, glm::vec2(1.0f, 0.0f), tangent});
            mesh.vertices.push_back(Vertex{glm::vec3(right, top, -1.05f), normal, glm::vec2(1.0f), tangent});
            mesh.vertices.push_back(Vertex{glm::vec3(left, top, -1.05f), normal, glm::vec2(0.0f, 1.0f), tangent});
            std::vector<std::uint32_t>& target = ((row + column) % 2 == 0) ? lightIndices : darkIndices;
            target.insert(target.end(), {first, first + 1U, first + 2U, first, first + 2U, first + 3U});
        }
    }
    mesh.indices = darkIndices;
    mesh.indices.insert(mesh.indices.end(), lightIndices.begin(), lightIndices.end());
    mesh.submeshes.push_back(SubmeshData{"Dark checks", 0U, static_cast<std::uint32_t>(darkIndices.size()), 0});
    mesh.submeshes.push_back(SubmeshData{"Light checks", static_cast<std::uint32_t>(darkIndices.size()), static_cast<std::uint32_t>(lightIndices.size()), 1});
    mesh.boundsMin = glm::vec3(-columns * cellSize * 0.5f, -rows * cellSize * 0.5f, -1.05f);
    mesh.boundsMax = glm::vec3(columns * cellSize * 0.5f, rows * cellSize * 0.5f, -1.05f);

    MaterialData dark;
    dark.name = "Checker charcoal";
    dark.baseColorFactor = glm::vec4(0.035f, 0.045f, 0.055f, 1.0f);
    dark.roughnessFactor = 0.78f;
    MaterialData light;
    light.name = "Checker ivory";
    light.baseColorFactor = glm::vec4(0.82f, 0.78f, 0.66f, 1.0f);
    light.roughnessFactor = 0.72f;

    ModelData model;
    model.name = "Procedural Glass-2C checkerboard";
    model.meshes.push_back(std::move(mesh));
    model.materials.push_back(std::move(dark));
    model.materials.push_back(std::move(light));
    model.rootNode.name = "Checkerboard root";
    model.rootNode.meshIndices.push_back(0U);
    model.boundsMin = model.meshes.front().boundsMin;
    model.boundsMax = model.meshes.front().boundsMax;
    return model;
}
