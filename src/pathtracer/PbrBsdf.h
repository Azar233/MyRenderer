#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace pathtracer {

struct PbrSurface {
    glm::vec3 baseColor{1.0f};
    float metallic{0.0f};
    float perceptualRoughness{1.0f};
};

struct BsdfSample {
    glm::vec3 direction{0.0f};
    glm::vec3 weight{0.0f};
    float pdf{0.0f};
    bool valid{false};
};

// Directions point away from the surface. The normal must face the outgoing direction.
glm::vec3 evaluatePbrBsdf(const PbrSurface& surface, const glm::vec3& normal,
                          const glm::vec3& outgoing, const glm::vec3& incoming);
float pbrBsdfPdf(const PbrSurface& surface, const glm::vec3& normal,
                 const glm::vec3& outgoing, const glm::vec3& incoming);
BsdfSample samplePbrBsdf(const PbrSurface& surface, const glm::vec3& normal,
                         const glm::vec3& outgoing, float componentSample,
                         const glm::vec2& directionSample);

} // namespace pathtracer
