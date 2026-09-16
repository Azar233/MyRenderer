#include "pathtracer/LightSampling.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace pathtracer {
namespace {

bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool emits(const glm::vec3& radiance) {
    return finite(radiance) && glm::any(glm::greaterThan(radiance, glm::vec3(0.0f)));
}

} // namespace

float powerHeuristic(float firstPdf, float secondPdf) {
    if (!(firstPdf > 0.0f)) return 0.0f;
    const double first = static_cast<double>(firstPdf);
    const double second = std::max(static_cast<double>(secondPdf), 0.0);
    return static_cast<float>((first * first) / (first * first + second * second));
}

SceneLights::SceneLights(const SceneSnapshot& snapshot, const std::vector<Triangle>& triangles)
    : lighting_(snapshot.lighting()), environment_(snapshot.lighting().environment) {
    if (emits(lighting_.directional.radiance) &&
        glm::dot(lighting_.directional.direction, lighting_.directional.direction) > 1.0e-12f)
        entries_.push_back({Kind::Directional, 0U});

    for (std::size_t index = 0; index < lighting_.localLights.size(); ++index) {
        const SnapshotLocalLight& light = lighting_.localLights[index];
        if (emits(light.radiance) && light.radius > 0.0f)
            entries_.push_back({Kind::Local, static_cast<std::uint32_t>(index)});
    }

    for (const Triangle& triangle : triangles) {
        if (triangle.assetIndex >= snapshot.assets().size()) continue;
        const auto& model = snapshot.assets()[triangle.assetIndex].model;
        if (!model || triangle.materialIndex < 0 ||
            static_cast<std::size_t>(triangle.materialIndex) >= model->materials.size())
            continue;
        const MaterialData& material = model->materials[triangle.materialIndex];
        if (!emits(material.emissiveFactor)) continue;
        const glm::vec3 cross = glm::cross(triangle.positions[1] - triangle.positions[0],
                                           triangle.positions[2] - triangle.positions[0]);
        const float twiceArea = glm::length(cross);
        if (!std::isfinite(twiceArea) || twiceArea <= 1.0e-8f) continue;

        EmissiveTriangle emitter;
        for (int corner = 0; corner < 3; ++corner)
            emitter.positions[corner] = triangle.positions[corner];
        emitter.normal = cross / twiceArea;
        emitter.radiance = glm::max(material.emissiveFactor, glm::vec3(0.0f));
        emitter.area = 0.5f * twiceArea;
        emitter.primitiveIndex = triangle.primitiveIndex;
        emitter.doubleSided = material.doubleSided;
        const auto index = static_cast<std::uint32_t>(emitters_.size());
        emitters_.push_back(emitter);
        emitterByPrimitive_.emplace(emitter.primitiveIndex, index);
        entries_.push_back({Kind::EmissiveTriangle, index});
    }

    if (environment_.importanceSampled()) entries_.push_back({Kind::Environment, 0U});
}

DirectLightSample SceneLights::sample(const glm::vec3& position, float lightSample,
                                      const glm::vec2& surfaceSample) const {
    DirectLightSample result;
    if (entries_.empty() || !finite(position)) return result;
    const float selection = std::clamp(lightSample, 0.0f, 0.99999994f);
    const std::size_t entryIndex = std::min(
        static_cast<std::size_t>(selection * static_cast<float>(entries_.size())), entries_.size() - 1U);
    const Entry& entry = entries_[entryIndex];
    const float selectionPdf = 1.0f / static_cast<float>(entries_.size());

    if (entry.kind == Kind::Directional) {
        result.direction = -glm::normalize(lighting_.directional.direction);
        result.radiance = glm::max(lighting_.directional.radiance, glm::vec3(0.0f));
        result.distance = std::numeric_limits<float>::infinity();
        result.pdf = selectionPdf;
        result.delta = result.valid = true;
        return result;
    }

    if (entry.kind == Kind::Local) {
        const SnapshotLocalLight& light = lighting_.localLights[entry.index];
        const glm::vec3 toLight = light.position - position;
        const float distanceSquared = glm::dot(toLight, toLight);
        if (!std::isfinite(distanceSquared) || distanceSquared <= 1.0e-12f) return result;
        result.distance = std::sqrt(distanceSquared);
        result.direction = toLight / result.distance;
        const float ratio = result.distance / std::max(light.radius, 0.001f);
        const float smoothRange = std::clamp(1.0f - ratio * ratio * ratio * ratio, 0.0f, 1.0f);
        float attenuation = smoothRange * smoothRange / std::max(1.0f + distanceSquared, 0.0001f);
        if (light.type == SnapshotLocalLightType::Spot) {
            const glm::vec3 direction = glm::dot(light.direction, light.direction) > 1.0e-12f
                                            ? glm::normalize(light.direction)
                                            : glm::vec3(0.0f, -1.0f, 0.0f);
            const float coneCosine = glm::dot(-result.direction, direction);
            const float outer = std::clamp(light.outerConeCosine, -0.99f, 0.99f);
            const float inner = std::min(outer + 0.10f, 0.999f);
            attenuation *= glm::smoothstep(outer, inner, coneCosine);
        }
        result.radiance = glm::max(light.radiance, glm::vec3(0.0f)) * attenuation;
        result.pdf = selectionPdf;
        result.delta = true;
        result.valid = emits(result.radiance);
        return result;
    }

    if (entry.kind == Kind::Environment) {
        const EnvironmentSample sample = environment_.sample(surfaceSample);
        result.direction = sample.direction;
        result.radiance = sample.radiance;
        result.distance = std::numeric_limits<float>::infinity();
        result.pdf = selectionPdf * sample.pdf;
        result.delta = false;
        result.valid = sample.valid && result.pdf > 0.0f;
        return result;
    }

    const EmissiveTriangle& emitter = emitters_[entry.index];
    const float u = std::clamp(surfaceSample.x, 0.0f, 0.99999994f);
    const float v = std::clamp(surfaceSample.y, 0.0f, 0.99999994f);
    const float root = std::sqrt(u);
    const float b0 = 1.0f - root;
    const float b1 = v * root;
    const float b2 = 1.0f - b0 - b1;
    const glm::vec3 point = b0 * emitter.positions[0] + b1 * emitter.positions[1] + b2 * emitter.positions[2];
    const glm::vec3 toLight = point - position;
    const float distanceSquared = glm::dot(toLight, toLight);
    if (!std::isfinite(distanceSquared) || distanceSquared <= 1.0e-12f) return result;
    result.distance = std::sqrt(distanceSquared);
    result.direction = toLight / result.distance;
    const float rawCosine = glm::dot(emitter.normal, -result.direction);
    const float lightCosine = emitter.doubleSided ? std::abs(rawCosine) : rawCosine;
    if (lightCosine <= 1.0e-7f) return result;
    result.radiance = emitter.radiance;
    result.pdf = selectionPdf * distanceSquared / (lightCosine * emitter.area);
    result.delta = false;
    result.valid = result.pdf > 0.0f && std::isfinite(result.pdf);
    return result;
}

float SceneLights::emissiveHitPdf(const glm::vec3& previousPosition,
                                  const SurfaceInteraction& hit) const {
    const auto found = emitterByPrimitive_.find(hit.primitiveIndex);
    if (found == emitterByPrimitive_.end() || entries_.empty()) return 0.0f;
    const EmissiveTriangle& emitter = emitters_[found->second];
    const glm::vec3 offset = hit.position - previousPosition;
    const float distanceSquared = glm::dot(offset, offset);
    if (!std::isfinite(distanceSquared) || distanceSquared <= 1.0e-12f) return 0.0f;
    const glm::vec3 direction = offset / std::sqrt(distanceSquared);
    const float rawCosine = glm::dot(emitter.normal, -direction);
    const float lightCosine = emitter.doubleSided ? std::abs(rawCosine) : rawCosine;
    if (lightCosine <= 1.0e-7f) return 0.0f;
    const float selectionPdf = 1.0f / static_cast<float>(entries_.size());
    return selectionPdf * distanceSquared / (lightCosine * emitter.area);
}

glm::vec3 SceneLights::environmentRadiance(const glm::vec3& direction) const {
    return environment_.radiance(direction);
}

float SceneLights::environmentPdf(const glm::vec3& direction) const {
    if (!environment_.importanceSampled() || entries_.empty()) return 0.0f;
    return environment_.pdf(direction) / static_cast<float>(entries_.size());
}

} // namespace pathtracer
