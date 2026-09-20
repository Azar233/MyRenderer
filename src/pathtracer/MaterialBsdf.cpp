#include "pathtracer/MaterialBsdf.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace pathtracer {

float dielectricFresnel(float cosineIncident, float etaIncident, float etaTransmitted) {
    const float cosineI = std::clamp(cosineIncident, 0.0f, 1.0f);
    const float etaI = std::max(etaIncident, 1.0e-4f);
    const float etaT = std::max(etaTransmitted, 1.0e-4f);
    const float sineTransmitted = etaI / etaT
        * std::sqrt(std::max(1.0f - cosineI * cosineI, 0.0f));
    if (sineTransmitted >= 1.0f) return 1.0f;
    const float cosineTransmitted = std::sqrt(std::max(
        1.0f - sineTransmitted * sineTransmitted, 0.0f
    ));
    const float parallelNumerator = etaT * cosineI - etaI * cosineTransmitted;
    const float parallelDenominator = etaT * cosineI + etaI * cosineTransmitted;
    const float perpendicularNumerator = etaI * cosineI - etaT * cosineTransmitted;
    const float perpendicularDenominator = etaI * cosineI + etaT * cosineTransmitted;
    const float parallel = parallelNumerator / std::max(parallelDenominator, 1.0e-7f);
    const float perpendicular = perpendicularNumerator / std::max(perpendicularDenominator, 1.0e-7f);
    return std::clamp(0.5f * (parallel * parallel + perpendicular * perpendicular), 0.0f, 1.0f);
}

float materialOpaqueProbability(const EvaluatedPbrMaterial& material) {
    const float dielectricTransmission = std::clamp(material.transmission, 0.0f, 1.0f)
        * (1.0f - std::clamp(material.surface.metallic, 0.0f, 1.0f));
    return 1.0f - dielectricTransmission;
}

glm::vec3 evaluateMaterialBsdf(
    const EvaluatedPbrMaterial& material,
    const glm::vec3& normal,
    const glm::vec3& outgoing,
    const glm::vec3& incoming
) {
    return evaluatePbrBsdf(material.surface, normal, outgoing, incoming)
        * materialOpaqueProbability(material);
}

float materialBsdfPdf(
    const EvaluatedPbrMaterial& material,
    const glm::vec3& normal,
    const glm::vec3& outgoing,
    const glm::vec3& incoming,
    GgxSamplingStrategy strategy
) {
    return pbrBsdfPdf(material.surface, normal, outgoing, incoming, strategy)
        * materialOpaqueProbability(material);
}

MaterialBsdfSample sampleMaterialBsdf(
    const EvaluatedPbrMaterial& material,
    const glm::vec3& normal,
    const glm::vec3& outgoing,
    bool frontFace,
    float componentSample,
    const glm::vec2& directionSample,
    GgxSamplingStrategy strategy
) {
    MaterialBsdfSample result;
    if (glm::dot(normal, outgoing) <= 0.0f) return result;

    const float opaqueProbability = materialOpaqueProbability(material);
    const float transmissionProbability = 1.0f - opaqueProbability;
    const float component = std::clamp(componentSample, 0.0f, 0.99999994f);
    if (transmissionProbability > 0.0f && component < transmissionProbability) {
        const float etaIncident = frontFace ? 1.0f : std::max(material.indexOfRefraction, 1.0f);
        const float etaTransmitted = frontFace ? std::max(material.indexOfRefraction, 1.0f) : 1.0f;
        const float eta = etaIncident / etaTransmitted;
        const float fresnel = dielectricFresnel(
            glm::dot(normal, outgoing), etaIncident, etaTransmitted
        );
        const float branch = component / transmissionProbability;
        if (branch < fresnel) {
            result.direction = glm::reflect(-outgoing, normal);
            result.weight = glm::vec3(1.0f);
            result.pdf = transmissionProbability * fresnel;
        } else {
            result.direction = glm::refract(-outgoing, normal, eta);
            const float directionLengthSquared = glm::dot(result.direction, result.direction);
            if (directionLengthSquared <= 1.0e-12f) {
                result.direction = glm::reflect(-outgoing, normal);
                result.weight = glm::vec3(1.0f);
                result.pdf = transmissionProbability;
            } else {
                result.direction *= 1.0f / std::sqrt(directionLengthSquared);
                result.weight = glm::vec3(eta * eta);
                result.pdf = transmissionProbability * (1.0f - fresnel);
                result.transmitted = true;
            }
        }
        result.delta = true;
        result.valid = result.pdf > 0.0f
            && std::isfinite(result.direction.x) && std::isfinite(result.direction.y)
            && std::isfinite(result.direction.z);
        return result;
    }

    if (opaqueProbability <= 0.0f) return result;
    const float remappedComponent = (component - transmissionProbability) / opaqueProbability;
    const BsdfSample opaque = samplePbrBsdf(
        material.surface,
        normal,
        outgoing,
        std::clamp(remappedComponent, 0.0f, 0.99999994f),
        directionSample,
        strategy
    );
    result.direction = opaque.direction;
    result.weight = opaque.weight;
    result.pdf = opaque.pdf * opaqueProbability;
    result.valid = opaque.valid;
    return result;
}

glm::vec3 beerLambertTransmittance(
    const glm::vec3& attenuationColor,
    float attenuationDistance,
    float distance
) {
    if (!(attenuationDistance > 0.0f) || !std::isfinite(attenuationDistance)
        || !(distance > 0.0f) || !std::isfinite(distance)) {
        return glm::vec3(1.0f);
    }
    const glm::vec3 color = glm::clamp(
        attenuationColor, glm::vec3(1.0e-6f), glm::vec3(1.0f)
    );
    const float opticalDistance = distance / attenuationDistance;
    return {
        std::pow(color.r, opticalDistance),
        std::pow(color.g, opticalDistance),
        std::pow(color.b, opticalDistance)
    };
}

} // namespace pathtracer
