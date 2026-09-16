#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "pathtracer/EnvironmentSampling.h"
#include "pathtracer/RayGeometry.h"
#include "pathtracer/SceneSnapshot.h"

namespace pathtracer {

struct DirectLightSample {
    glm::vec3 direction{0.0f};
    glm::vec3 radiance{0.0f};
    float distance{0.0f};
    float pdf{0.0f};
    bool delta{false};
    bool valid{false};
};

class SceneLights {
public:
    SceneLights() = default;
    SceneLights(const SceneSnapshot& snapshot, const std::vector<Triangle>& triangles);

    bool empty() const { return entries_.empty(); }
    std::size_t size() const { return entries_.size(); }
    DirectLightSample sample(const glm::vec3& position, float lightSample,
                             const glm::vec2& surfaceSample) const;
    float emissiveHitPdf(const glm::vec3& previousPosition,
                         const SurfaceInteraction& hit) const;
    glm::vec3 environmentRadiance(const glm::vec3& direction) const;
    float environmentPdf(const glm::vec3& direction) const;

private:
    enum class Kind { Directional, Local, EmissiveTriangle, Environment };
    struct Entry {
        Kind kind{Kind::Directional};
        std::uint32_t index{0U};
    };
    struct EmissiveTriangle {
        glm::vec3 positions[3]{};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        glm::vec3 radiance{0.0f};
        float area{0.0f};
        std::uint32_t primitiveIndex{0U};
        bool doubleSided{false};
    };

    SceneSnapshotLighting lighting_;
    EnvironmentLight environment_;
    std::vector<Entry> entries_;
    std::vector<EmissiveTriangle> emitters_;
    std::unordered_map<std::uint32_t, std::uint32_t> emitterByPrimitive_;
};

float powerHeuristic(float firstPdf, float secondPdf);

} // namespace pathtracer
