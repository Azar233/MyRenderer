#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "pathtracer/TextureSampling.h"

namespace pathtracer {

struct MaterialBsdfSample {
    glm::vec3 direction{0.0f};
    glm::vec3 weight{0.0f};
    float pdf{0.0f};
    bool delta{false};
    bool transmitted{false};
    bool valid{false};
};

float dielectricFresnel(float cosineIncident, float etaIncident, float etaTransmitted);
float materialOpaqueProbability(const EvaluatedPbrMaterial& material);
glm::vec3 evaluateMaterialBsdf(
    const EvaluatedPbrMaterial& material,
    const glm::vec3& normal,
    const glm::vec3& outgoing,
    const glm::vec3& incoming
);
float materialBsdfPdf(
    const EvaluatedPbrMaterial& material,
    const glm::vec3& normal,
    const glm::vec3& outgoing,
    const glm::vec3& incoming
);
MaterialBsdfSample sampleMaterialBsdf(
    const EvaluatedPbrMaterial& material,
    const glm::vec3& normal,
    const glm::vec3& outgoing,
    bool frontFace,
    float componentSample,
    const glm::vec2& directionSample
);
glm::vec3 beerLambertTransmittance(
    const glm::vec3& attenuationColor,
    float attenuationDistance,
    float distance
);

} // namespace pathtracer
