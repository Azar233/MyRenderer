#include "pathtracer/PbrBsdf.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace pathtracer {
namespace {

constexpr float pi = 3.14159265358979323846f;
constexpr float minimumRoughness = 0.045f;

PbrSurface sanitized(PbrSurface surface) {
    surface.baseColor = glm::clamp(surface.baseColor, glm::vec3(0.0f), glm::vec3(1.0f));
    surface.metallic = std::clamp(surface.metallic, 0.0f, 1.0f);
    surface.perceptualRoughness = std::clamp(surface.perceptualRoughness, minimumRoughness, 1.0f);
    return surface;
}

glm::vec3 fresnelSchlick(const glm::vec3& f0, float cosine) {
    const float factor = std::pow(1.0f - std::clamp(cosine, 0.0f, 1.0f), 5.0f);
    return f0 + (glm::vec3(1.0f) - f0) * factor;
}

float ggxDistribution(float noH, float alphaSquared) {
    const float denominator = noH * noH * (alphaSquared - 1.0f) + 1.0f;
    return alphaSquared / std::max(pi * denominator * denominator, 1.0e-12f);
}

float smithG1(float noX, float alphaSquared) {
    if (noX <= 0.0f) return 0.0f;
    return 2.0f * noX /
           std::max(noX + std::sqrt(alphaSquared + (1.0f - alphaSquared) * noX * noX), 1.0e-7f);
}

float luminance(const glm::vec3& color) {
    return glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

float specularProbability(const PbrSurface& surface) {
    const glm::vec3 f0 = glm::mix(glm::vec3(0.04f), surface.baseColor, surface.metallic);
    // Keep both lobes sampleable at endpoints. This is a variance choice, not a BRDF weight.
    return std::clamp(luminance(f0), 0.1f, 0.9f);
}

void basis(const glm::vec3& normal, glm::vec3& tangent, glm::vec3& bitangent) {
    tangent = glm::normalize(glm::cross(std::abs(normal.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                                                       : glm::vec3(0.0f, 1.0f, 0.0f),
                                        normal));
    bitangent = glm::cross(normal, tangent);
}

glm::vec3 localToWorld(const glm::vec3& local, const glm::vec3& normal) {
    glm::vec3 tangent, bitangent;
    basis(normal, tangent, bitangent);
    return glm::normalize(tangent * local.x + bitangent * local.y + normal * local.z);
}

} // namespace

glm::vec3 evaluatePbrBsdf(const PbrSurface& input, const glm::vec3& normal,
                          const glm::vec3& outgoing, const glm::vec3& incoming) {
    const PbrSurface surface = sanitized(input);
    const float noV = glm::dot(normal, outgoing);
    const float noL = glm::dot(normal, incoming);
    if (noV <= 0.0f || noL <= 0.0f) return glm::vec3(0.0f);

    const glm::vec3 halfVectorSum = outgoing + incoming;
    const float halfLengthSquared = glm::dot(halfVectorSum, halfVectorSum);
    if (halfLengthSquared <= 1.0e-12f) return glm::vec3(0.0f);
    const glm::vec3 halfVector = halfVectorSum / std::sqrt(halfLengthSquared);
    const float noH = std::max(glm::dot(normal, halfVector), 0.0f);
    const float voH = std::max(glm::dot(outgoing, halfVector), 0.0f);

    const float alpha = surface.perceptualRoughness * surface.perceptualRoughness;
    const float alphaSquared = alpha * alpha;
    const glm::vec3 f0 = glm::mix(glm::vec3(0.04f), surface.baseColor, surface.metallic);
    const glm::vec3 fresnel = fresnelSchlick(f0, voH);
    const float distribution = ggxDistribution(noH, alphaSquared);
    const float geometry = smithG1(noV, alphaSquared) * smithG1(noL, alphaSquared);
    const glm::vec3 specular = fresnel * (distribution * geometry / std::max(4.0f * noV * noL, 1.0e-7f));

    // Fresnel removes the energy that entered the microfacet reflection lobe.
    const glm::vec3 diffuseColor = surface.baseColor * (1.0f - surface.metallic);
    const glm::vec3 diffuse = (glm::vec3(1.0f) - fresnel) * diffuseColor / pi;
    return diffuse + specular;
}

float pbrBsdfPdf(const PbrSurface& input, const glm::vec3& normal,
                 const glm::vec3& outgoing, const glm::vec3& incoming) {
    const PbrSurface surface = sanitized(input);
    const float noV = glm::dot(normal, outgoing);
    const float noL = glm::dot(normal, incoming);
    if (noV <= 0.0f || noL <= 0.0f) return 0.0f;

    const glm::vec3 halfVectorSum = outgoing + incoming;
    const float halfLengthSquared = glm::dot(halfVectorSum, halfVectorSum);
    if (halfLengthSquared <= 1.0e-12f) return 0.0f;
    const glm::vec3 halfVector = halfVectorSum / std::sqrt(halfLengthSquared);
    const float noH = std::max(glm::dot(normal, halfVector), 0.0f);
    const float voH = std::max(glm::dot(outgoing, halfVector), 0.0f);
    if (noH <= 0.0f || voH <= 0.0f) return 0.0f;

    const float alpha = surface.perceptualRoughness * surface.perceptualRoughness;
    const float specularPdf = ggxDistribution(noH, alpha * alpha) * noH / (4.0f * voH);
    const float diffusePdf = noL / pi;
    const float chooseSpecular = specularProbability(surface);
    return chooseSpecular * specularPdf + (1.0f - chooseSpecular) * diffusePdf;
}

BsdfSample samplePbrBsdf(const PbrSurface& input, const glm::vec3& normal,
                         const glm::vec3& outgoing, float componentSample,
                         const glm::vec2& directionSample) {
    BsdfSample result;
    const PbrSurface surface = sanitized(input);
    if (glm::dot(normal, outgoing) <= 0.0f) return result;

    const float u = std::clamp(directionSample.x, 0.0f, 0.99999994f);
    const float v = std::clamp(directionSample.y, 0.0f, 0.99999994f);
    if (componentSample < specularProbability(surface)) {
        const float alpha = surface.perceptualRoughness * surface.perceptualRoughness;
        const float alphaSquared = alpha * alpha;
        const float phi = 2.0f * pi * u;
        const float cosine = std::sqrt((1.0f - v) / (1.0f + (alphaSquared - 1.0f) * v));
        const float sine = std::sqrt(std::max(0.0f, 1.0f - cosine * cosine));
        const glm::vec3 halfVector = localToWorld({sine * std::cos(phi), sine * std::sin(phi), cosine}, normal);
        result.direction = glm::reflect(-outgoing, halfVector);
    } else {
        const float radius = std::sqrt(u);
        const float phi = 2.0f * pi * v;
        result.direction = localToWorld({radius * std::cos(phi), radius * std::sin(phi),
                                         std::sqrt(std::max(0.0f, 1.0f - u))}, normal);
    }

    const float noL = glm::dot(normal, result.direction);
    if (noL <= 0.0f) return result;
    result.pdf = pbrBsdfPdf(surface, normal, outgoing, result.direction);
    if (!(result.pdf > 0.0f) || !std::isfinite(result.pdf)) return result;
    result.weight = evaluatePbrBsdf(surface, normal, outgoing, result.direction) * (noL / result.pdf);
    result.valid = std::isfinite(result.weight.x) && std::isfinite(result.weight.y) &&
                   std::isfinite(result.weight.z) && glm::all(glm::greaterThanEqual(result.weight, glm::vec3(0.0f)));
    return result;
}

} // namespace pathtracer
