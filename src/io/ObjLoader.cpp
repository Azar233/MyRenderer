#include "io/ObjLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include <glm/geometric.hpp>
#include <tiny_obj_loader.h>

namespace {

struct VertexKey {
    int position{-1};
    int normal{-1};
    std::uint64_t split{0};

    bool operator==(const VertexKey& other) const {
        return position == other.position && normal == other.normal && split == other.split;
    }
};

struct VertexKeyHash {
    std::size_t operator()(const VertexKey& key) const {
        const auto a = static_cast<std::uint32_t>(key.position);
        const auto b = static_cast<std::uint32_t>(key.normal);
        std::size_t result = static_cast<std::size_t>(a) * 0x9e3779b1U;
        result ^= static_cast<std::size_t>(b) + 0x9e3779b9U + (result << 6U) + (result >> 2U);
        result ^= static_cast<std::size_t>(key.split) + 0x9e3779b9U + (result << 6U) + (result >> 2U);
        return result;
    }
};

glm::vec3 readVec3(const std::vector<tinyobj::real_t>& values, int index, const char* label) {
    const std::size_t base = static_cast<std::size_t>(index) * 3U;
    if (index < 0 || base + 2U >= values.size()) {
        throw std::runtime_error(std::string("OBJ contains an invalid ") + label + " index");
    }
    return {
        static_cast<float>(values[base]),
        static_cast<float>(values[base + 1U]),
        static_cast<float>(values[base + 2U])
    };
}

bool requestsFlatShading(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] != 's') {
            continue;
        }
        const auto value = line.find_first_not_of(" \t", first + 1U);
        if (value != std::string::npos
            && (line.compare(value, 3U, "off") == 0 || line[value] == '0')) {
            return true;
        }
    }
    return false;
}

} // namespace

ObjLoadResult ObjLoader::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("Model file does not exist: " + path.string());
    }

    tinyobj::attrib_t attributes;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string diagnostics;
    const std::string baseDirectory = path.parent_path().string() + "/";

    const bool loaded = tinyobj::LoadObj(
        &attributes,
        &shapes,
        &materials,
        &diagnostics,
        path.string().c_str(),
        baseDirectory.c_str(),
        true
    );
    if (!loaded) {
        throw std::runtime_error("Failed to parse OBJ: " + diagnostics);
    }
    if (shapes.empty() || attributes.vertices.empty()) {
        throw std::runtime_error("OBJ does not contain renderable mesh data");
    }

    ObjLoadResult result;
    result.warnings = diagnostics;
    auto& output = result.mesh;
    std::unordered_map<VertexKey, std::uint32_t, VertexKeyHash> uniqueVertices;
    bool normalsComplete = true;
    const bool flatShading = requestsFlatShading(path);
    std::uint64_t cornerSequence = 0;

    auto appendVertex = [&](const tinyobj::index_t& index) -> std::uint32_t {
        const std::uint64_t split = flatShading && index.normal_index < 0 ? ++cornerSequence : 0U;
        const VertexKey key{index.vertex_index, index.normal_index, split};
        const auto found = uniqueVertices.find(key);
        if (found != uniqueVertices.end()) {
            return found->second;
        }

        Vertex vertex;
        vertex.position = readVec3(attributes.vertices, index.vertex_index, "position");
        if (index.normal_index >= 0) {
            vertex.normal = readVec3(attributes.normals, index.normal_index, "normal");
            const float lengthSquared = glm::dot(vertex.normal, vertex.normal);
            if (lengthSquared > 1e-12f) {
                vertex.normal = glm::normalize(vertex.normal);
            } else {
                normalsComplete = false;
                vertex.normal = glm::vec3(0.0f);
            }
        } else {
            normalsComplete = false;
            vertex.normal = glm::vec3(0.0f);
        }

        output.boundsMin = glm::min(output.boundsMin, vertex.position);
        output.boundsMax = glm::max(output.boundsMax, vertex.position);
        const auto newIndex = static_cast<std::uint32_t>(output.vertices.size());
        output.vertices.push_back(vertex);
        uniqueVertices.emplace(key, newIndex);
        return newIndex;
    };

    for (const auto& shape : shapes) {
        std::size_t offset = 0;
        for (const unsigned char faceVertexCount : shape.mesh.num_face_vertices) {
            if (faceVertexCount < 3U) {
                offset += faceVertexCount;
                continue;
            }

            std::vector<std::uint32_t> face;
            face.reserve(faceVertexCount);
            for (unsigned char vertex = 0; vertex < faceVertexCount; ++vertex) {
                if (offset + vertex >= shape.mesh.indices.size()) {
                    throw std::runtime_error("OBJ face index data is truncated");
                }
                face.push_back(appendVertex(shape.mesh.indices[offset + vertex]));
            }
            for (std::size_t vertex = 1; vertex + 1 < face.size(); ++vertex) {
                output.indices.push_back(face[0]);
                output.indices.push_back(face[vertex]);
                output.indices.push_back(face[vertex + 1]);
            }
            offset += faceVertexCount;
        }
    }

    if (output.indices.empty()) {
        throw std::runtime_error("OBJ does not contain any triangle faces");
    }

    if (!normalsComplete) {
        for (auto& vertex : output.vertices) {
            vertex.normal = glm::vec3(0.0f);
        }
        std::size_t degenerateTriangles = 0;
        for (std::size_t i = 0; i + 2 < output.indices.size(); i += 3) {
            Vertex& a = output.vertices[output.indices[i]];
            Vertex& b = output.vertices[output.indices[i + 1]];
            Vertex& c = output.vertices[output.indices[i + 2]];
            const glm::vec3 weightedNormal = glm::cross(b.position - a.position, c.position - a.position);
            if (glm::dot(weightedNormal, weightedNormal) <= 1e-16f) {
                ++degenerateTriangles;
                continue;
            }
            a.normal += weightedNormal;
            b.normal += weightedNormal;
            c.normal += weightedNormal;
        }
        for (auto& vertex : output.vertices) {
            if (glm::dot(vertex.normal, vertex.normal) > 1e-16f) {
                vertex.normal = glm::normalize(vertex.normal);
            } else {
                vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }
        }
        if (degenerateTriangles > 0) {
            result.warnings += "Skipped " + std::to_string(degenerateTriangles)
                             + " degenerate triangle(s) while generating normals.\n";
        }
    }

    return result;
}
