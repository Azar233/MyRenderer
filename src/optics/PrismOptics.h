#pragma once

#include <array>

#include <glm/vec2.hpp>

struct PrismRay2D {
    glm::vec2 origin{0.0f};
    glm::vec2 direction{1.0f, 0.0f};
};

struct PrismOpticalPath {
    bool valid{false};
    bool totalInternalReflection{false};
    glm::vec2 entryPoint{0.0f};
    glm::vec2 exitPoint{0.0f};
    glm::vec2 entryNormal{0.0f};
    glm::vec2 exitNormal{0.0f};
    glm::vec2 incidentDirection{1.0f, 0.0f};
    glm::vec2 internalDirection{1.0f, 0.0f};
    glm::vec2 exitDirection{1.0f, 0.0f};
    float entryTransmittance{0.0f};
    float exitTransmittance{0.0f};
    float totalTransmittance{0.0f};
};

// Traces one monochromatic center ray through a convex triangular prism
// cross-section. Triangle vertices may be clockwise or counter-clockwise.
PrismOpticalPath traceTriangularPrism(
    const std::array<glm::vec2, 3>& triangle,
    const PrismRay2D& ray,
    float prismIndexOfRefraction
);

