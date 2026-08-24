#include "optics/PrismOptics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include <glm/geometric.hpp>

namespace {

constexpr float epsilon = 1.0e-5f;

float cross2D(const glm::vec2& a, const glm::vec2& b) {
    return a.x * b.y - a.y * b.x;
}

struct SurfaceHit {
    float distance{0.0f};
    std::size_t edgeIndex{0U};
    glm::vec2 point{0.0f};
    glm::vec2 outwardNormal{0.0f};
};

std::optional<SurfaceHit> nearestSurfaceHit(
    const std::array<glm::vec2, 3>& triangle,
    const glm::vec2& origin,
    const glm::vec2& direction,
    bool entering
) {
    std::optional<SurfaceHit> nearest;
    for (std::size_t edgeIndex = 0; edgeIndex < triangle.size(); ++edgeIndex) {
        const glm::vec2& from = triangle[edgeIndex];
        const glm::vec2& to = triangle[(edgeIndex + 1U) % triangle.size()];
        const glm::vec2 edge = to - from;
        const float denominator = cross2D(direction, edge);
        if (std::abs(denominator) <= epsilon) {
            continue;
        }

        const glm::vec2 toSurface = from - origin;
        const float distance = cross2D(toSurface, edge) / denominator;
        const float edgePosition = cross2D(toSurface, direction) / denominator;
        if (distance <= epsilon || edgePosition < -epsilon || edgePosition > 1.0f + epsilon) {
            continue;
        }

        const glm::vec2 outwardNormal = glm::normalize(glm::vec2(edge.y, -edge.x));
        const float facing = glm::dot(direction, outwardNormal);
        if ((entering && facing >= -epsilon) || (!entering && facing <= epsilon)) {
            continue;
        }
        if (!nearest.has_value() || distance < nearest->distance) {
            nearest = SurfaceHit{
                distance,
                edgeIndex,
                origin + direction * distance,
                outwardNormal
            };
        }
    }
    return nearest;
}

bool refractDirection(
    const glm::vec2& incident,
    const glm::vec2& normalAgainstIncident,
    float incidentIor,
    float transmittedIor,
    glm::vec2& refracted
) {
    const float eta = incidentIor / transmittedIor;
    const float cosine = std::clamp(-glm::dot(normalAgainstIncident, incident), 0.0f, 1.0f);
    const float discriminant = 1.0f - eta * eta * (1.0f - cosine * cosine);
    if (discriminant < 0.0f) {
        refracted = glm::vec2(0.0f);
        return false;
    }
    refracted = glm::normalize(
        eta * incident
        + (eta * cosine - std::sqrt(std::max(discriminant, 0.0f))) * normalAgainstIncident
    );
    return true;
}

float fresnelTransmittance(
    const glm::vec2& incident,
    const glm::vec2& normalAgainstIncident,
    float incidentIor,
    float transmittedIor
) {
    const float cosine = std::clamp(-glm::dot(normalAgainstIncident, incident), 0.0f, 1.0f);
    const float ratio = (incidentIor - transmittedIor) / (incidentIor + transmittedIor);
    const float f0 = ratio * ratio;
    const float reflectance = f0 + (1.0f - f0) * std::pow(1.0f - cosine, 5.0f);
    return std::clamp(1.0f - reflectance, 0.0f, 1.0f);
}

} // namespace

PrismOpticalPath traceTriangularPrism(
    const std::array<glm::vec2, 3>& inputTriangle,
    const PrismRay2D& ray,
    float prismIndexOfRefraction
) {
    PrismOpticalPath path;
    const float directionLengthSquared = glm::dot(ray.direction, ray.direction);
    if (directionLengthSquared <= epsilon || prismIndexOfRefraction < 1.0f) {
        return path;
    }

    std::array<glm::vec2, 3> triangle = inputTriangle;
    const float signedDoubleArea = cross2D(triangle[1] - triangle[0], triangle[2] - triangle[0]);
    if (std::abs(signedDoubleArea) <= epsilon) {
        return path;
    }
    if (signedDoubleArea < 0.0f) {
        std::swap(triangle[1], triangle[2]);
    }

    path.incidentDirection = glm::normalize(ray.direction);
    const auto entry = nearestSurfaceHit(triangle, ray.origin, path.incidentDirection, true);
    if (!entry.has_value()) {
        return path;
    }

    path.entryPoint = entry->point;
    path.entryNormal = entry->outwardNormal;
    path.entryTransmittance = fresnelTransmittance(
        path.incidentDirection,
        path.entryNormal,
        1.0f,
        prismIndexOfRefraction
    );
    if (!refractDirection(
            path.incidentDirection,
            path.entryNormal,
            1.0f,
            prismIndexOfRefraction,
            path.internalDirection)) {
        return path;
    }

    const glm::vec2 insideOrigin = path.entryPoint + path.internalDirection * (epsilon * 8.0f);
    const auto exit = nearestSurfaceHit(triangle, insideOrigin, path.internalDirection, false);
    if (!exit.has_value()) {
        return path;
    }
    path.exitPoint = exit->point;
    path.exitNormal = exit->outwardNormal;

    const glm::vec2 exitNormalAgainstIncident = -path.exitNormal;
    path.exitTransmittance = fresnelTransmittance(
        path.internalDirection,
        exitNormalAgainstIncident,
        prismIndexOfRefraction,
        1.0f
    );
    path.totalInternalReflection = !refractDirection(
        path.internalDirection,
        exitNormalAgainstIncident,
        prismIndexOfRefraction,
        1.0f,
        path.exitDirection
    );
    if (path.totalInternalReflection) {
        path.exitDirection = glm::normalize(
            path.internalDirection
            - 2.0f * glm::dot(path.internalDirection, path.exitNormal) * path.exitNormal
        );
        path.exitTransmittance = 0.0f;
    }
    path.totalTransmittance = path.entryTransmittance * path.exitTransmittance;
    path.valid = true;
    return path;
}
