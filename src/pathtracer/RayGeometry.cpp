#include "pathtracer/RayGeometry.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace pathtracer {
namespace {

constexpr float directionEpsilon = 1.0e-12f;
constexpr float triangleEpsilon = 1.0e-8f;

bool finiteVector(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

bool Bounds3::valid() const {
    return finiteVector(minimum) && finiteVector(maximum)
        && minimum.x <= maximum.x
        && minimum.y <= maximum.y
        && minimum.z <= maximum.z;
}

void Bounds3::expand(const glm::vec3& point) {
    if (!finiteVector(point)) return;
    minimum = glm::min(minimum, point);
    maximum = glm::max(maximum, point);
}

void Bounds3::expand(const Bounds3& bounds) {
    if (!bounds.valid()) return;
    expand(bounds.minimum);
    expand(bounds.maximum);
}

glm::vec3 Bounds3::centroid() const {
    return valid() ? 0.5f * (minimum + maximum) : glm::vec3(0.0f);
}

glm::vec3 Bounds3::extent() const {
    return valid() ? maximum - minimum : glm::vec3(0.0f);
}

int Bounds3::longestAxis() const {
    const glm::vec3 size = extent();
    if (size.y > size.x && size.y >= size.z) return 1;
    if (size.z > size.x && size.z > size.y) return 2;
    return 0;
}

bool Bounds3::intersect(const Ray& ray, float* nearT, float* farT) const {
    if (!valid() || !finiteVector(ray.origin) || !finiteVector(ray.direction)) return false;

    float nearDistance = ray.tMin;
    float farDistance = ray.tMax;
    for (int axis = 0; axis < 3; ++axis) {
        const float origin = ray.origin[axis];
        const float direction = ray.direction[axis];
        if (std::abs(direction) <= directionEpsilon) {
            if (origin < minimum[axis] || origin > maximum[axis]) return false;
            continue;
        }

        const float inverseDirection = 1.0f / direction;
        float first = (minimum[axis] - origin) * inverseDirection;
        float second = (maximum[axis] - origin) * inverseDirection;
        if (first > second) std::swap(first, second);
        nearDistance = std::max(nearDistance, first);
        farDistance = std::min(farDistance, second);
        if (farDistance < nearDistance) return false;
    }

    if (nearT != nullptr) *nearT = nearDistance;
    if (farT != nullptr) *farT = farDistance;
    return true;
}

Bounds3 Triangle::bounds() const {
    Bounds3 result;
    result.expand(positions[0]);
    result.expand(positions[1]);
    result.expand(positions[2]);
    return result;
}

glm::vec3 Triangle::centroid() const {
    return (positions[0] + positions[1] + positions[2]) / 3.0f;
}

bool intersectTriangle(
    const Ray& ray,
    const Triangle& triangle,
    SurfaceInteraction& interaction
) {
    const glm::vec3 edge01 = triangle.positions[1] - triangle.positions[0];
    const glm::vec3 edge02 = triangle.positions[2] - triangle.positions[0];
    const glm::vec3 crossDirection = glm::cross(ray.direction, edge02);
    const float determinant = glm::dot(edge01, crossDirection);
    if (!std::isfinite(determinant) || std::abs(determinant) <= triangleEpsilon) return false;

    const float inverseDeterminant = 1.0f / determinant;
    const glm::vec3 originOffset = ray.origin - triangle.positions[0];
    const float barycentricY = glm::dot(originOffset, crossDirection) * inverseDeterminant;
    if (barycentricY < 0.0f || barycentricY > 1.0f) return false;

    const glm::vec3 crossOffset = glm::cross(originOffset, edge01);
    const float barycentricZ = glm::dot(ray.direction, crossOffset) * inverseDeterminant;
    if (barycentricZ < 0.0f || barycentricY + barycentricZ > 1.0f) return false;

    const float distance = glm::dot(edge02, crossOffset) * inverseDeterminant;
    if (!std::isfinite(distance) || distance < ray.tMin || distance > ray.tMax) return false;

    glm::vec3 geometricNormal = glm::cross(edge01, edge02);
    const float geometricLengthSquared = glm::dot(geometricNormal, geometricNormal);
    if (!std::isfinite(geometricLengthSquared) || geometricLengthSquared <= triangleEpsilon) return false;
    geometricNormal *= 1.0f / std::sqrt(geometricLengthSquared);

    const float barycentricX = 1.0f - barycentricY - barycentricZ;
    glm::vec3 shadingNormal = barycentricX * triangle.normals[0]
        + barycentricY * triangle.normals[1]
        + barycentricZ * triangle.normals[2];
    const float shadingLengthSquared = glm::dot(shadingNormal, shadingNormal);
    if (!std::isfinite(shadingLengthSquared) || shadingLengthSquared <= triangleEpsilon) {
        shadingNormal = geometricNormal;
    } else {
        shadingNormal *= 1.0f / std::sqrt(shadingLengthSquared);
        if (glm::dot(shadingNormal, geometricNormal) < 0.0f) shadingNormal = -shadingNormal;
    }

    const bool frontFace = glm::dot(ray.direction, geometricNormal) < 0.0f;
    if (!frontFace) {
        geometricNormal = -geometricNormal;
        shadingNormal = -shadingNormal;
    }

    interaction.t = distance;
    interaction.position = ray.at(distance);
    interaction.geometricNormal = geometricNormal;
    interaction.shadingNormal = shadingNormal;
    interaction.barycentrics = glm::vec3(barycentricX, barycentricY, barycentricZ);
    interaction.texCoord = barycentricX * triangle.texCoords[0]
        + barycentricY * triangle.texCoords[1]
        + barycentricZ * triangle.texCoords[2];
    interaction.tint = triangle.tint;
    interaction.primitiveIndex = triangle.primitiveIndex;
    interaction.instanceIndex = triangle.instanceIndex;
    interaction.meshIndex = triangle.meshIndex;
    interaction.materialIndex = triangle.materialIndex;
    interaction.assetIndex = triangle.assetIndex;
    interaction.frontFace = frontFace;
    return true;
}

} // namespace pathtracer
