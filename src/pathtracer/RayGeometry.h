#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace pathtracer {

struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, 1.0f};
    float tMin{1.0e-4f};
    float tMax{std::numeric_limits<float>::infinity()};

    glm::vec3 at(float t) const { return origin + t * direction; }
};

struct Bounds3 {
    glm::vec3 minimum{std::numeric_limits<float>::infinity()};
    glm::vec3 maximum{-std::numeric_limits<float>::infinity()};

    bool valid() const;
    void expand(const glm::vec3& point);
    void expand(const Bounds3& bounds);
    glm::vec3 centroid() const;
    glm::vec3 extent() const;
    int longestAxis() const;
    bool intersect(const Ray& ray, float* nearT = nullptr, float* farT = nullptr) const;
};

struct Triangle {
    glm::vec3 positions[3]{};
    glm::vec3 normals[3]{};
    glm::vec2 texCoords[3]{};
    glm::vec3 tint{1.0f};
    std::uint32_t primitiveIndex{0U};
    std::uint32_t instanceIndex{0U};
    std::uint32_t meshIndex{0U};
    std::int32_t materialIndex{-1};
    std::uint32_t assetIndex{0U};

    Bounds3 bounds() const;
    glm::vec3 centroid() const;
};

struct SurfaceInteraction {
    float t{std::numeric_limits<float>::infinity()};
    glm::vec3 position{0.0f};
    glm::vec3 geometricNormal{0.0f, 1.0f, 0.0f};
    glm::vec3 shadingNormal{0.0f, 1.0f, 0.0f};
    glm::vec3 barycentrics{0.0f};
    glm::vec2 texCoord{0.0f};
    glm::vec3 tint{1.0f};
    std::uint32_t primitiveIndex{0U};
    std::uint32_t instanceIndex{0U};
    std::uint32_t meshIndex{0U};
    std::int32_t materialIndex{-1};
    std::uint32_t assetIndex{0U};
    bool frontFace{true};
};

bool intersectTriangle(
    const Ray& ray,
    const Triangle& triangle,
    SurfaceInteraction& interaction
);

} // namespace pathtracer
