#include "render/WaterWaves.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace water {
namespace {
constexpr float pi = 3.14159265358979323846f;
}

void applyPreset(WaterSettings& settings, WaterPreset preset) {
    settings.preset = preset;
    switch (preset) {
    case WaterPreset::Custom: break;
    case WaterPreset::Calm:
        settings.amplitude = 0.08f;
        settings.speed = 0.55f;
        settings.steepness = 0.30f;
        settings.foamStrength = 0.20f;
        settings.windDirection = glm::vec2(1.0f, 0.0f);
        break;
    case WaterPreset::Windy:
        settings.amplitude = 0.30f;
        settings.speed = 1.25f;
        settings.steepness = 0.70f;
        settings.foamStrength = 0.65f;
        settings.windDirection = glm::vec2(0.9f, 0.3f);
        break;
    case WaterPreset::Storm:
        settings.amplitude = 0.65f;
        settings.speed = 2.0f;
        settings.steepness = 0.85f;
        settings.foamStrength = 1.0f;
        settings.windDirection = glm::vec2(0.8f, 0.6f);
        break;
    }
}

int activeComponentCount(const WaterSettings& settings) {
    return settings.quality == WaterQuality::Low ? 2 : componentCount;
}

std::array<glm::vec4, componentCount> components(const WaterSettings& settings) {
    glm::vec2 wind = settings.windDirection;
    if (glm::dot(wind, wind) < 1.0e-6f) wind = glm::vec2(1.0f, 0.0f);
    wind = glm::normalize(wind);
    const glm::vec2 crossWind(-wind.y, wind.x);
    return {{
        glm::vec4(wind, settings.amplitude, 15.0f),
        glm::vec4(glm::normalize(wind * 0.86f + crossWind * 0.51f), settings.amplitude * 0.55f, 7.5f),
        glm::vec4(glm::normalize(wind * 0.71f - crossWind * 0.70f), settings.amplitude * 0.28f, 3.4f),
        glm::vec4(glm::normalize(wind * 0.54f + crossWind * 0.84f), settings.amplitude * 0.12f, 1.6f)
    }};
}

WaterSample evaluate(const WaterSettings& settings, const glm::vec2& position) {
    WaterSample sample;
    sample.position = glm::vec3(position.x, settings.level, position.y);
    glm::vec3 tangentX(1.0f, 0.0f, 0.0f);
    glm::vec3 tangentZ(0.0f, 0.0f, 1.0f);
    const auto waves = components(settings);
    for (int index = 0; index < activeComponentCount(settings); ++index) {
        const glm::vec4& wave = waves[static_cast<std::size_t>(index)];
        const glm::vec2 direction(wave.x, wave.y);
        const float amplitude = wave.z;
        const float k = 2.0f * pi / wave.w;
        const float phaseSpeed = std::sqrt(9.81f / k) * settings.speed;
        const float phase = k * (glm::dot(direction, position)
            - phaseSpeed * settings.timeSeconds);
        const float sine = std::sin(phase);
        const float cosine = std::cos(phase);
        const float horizontal = settings.steepness * amplitude
            / (k * std::max(settings.amplitude, 1.0e-4f) * componentCount);
        sample.position.x += horizontal * direction.x * cosine;
        sample.position.y += amplitude * sine;
        sample.position.z += horizontal * direction.y * cosine;
        tangentX += glm::vec3(-horizontal * k * direction.x * direction.x * sine,
                              amplitude * k * direction.x * cosine,
                              -horizontal * k * direction.x * direction.y * sine);
        tangentZ += glm::vec3(-horizontal * k * direction.x * direction.y * sine,
                              amplitude * k * direction.y * cosine,
                              -horizontal * k * direction.y * direction.y * sine);
        sample.velocity += glm::vec3(
            horizontal * direction.x * k * phaseSpeed * sine,
            -amplitude * k * phaseSpeed * cosine,
            horizontal * direction.y * k * phaseSpeed * sine);
    }
    sample.normal = glm::normalize(glm::cross(tangentZ, tangentX));
    sample.tangent = glm::normalize(tangentX);
    return sample;
}

float gridCoordinate(float logicalCoordinate, float extent) {
    const float coordinate = std::clamp(logicalCoordinate, -1.0f, 1.0f);
    return std::copysign(std::pow(std::abs(coordinate), 2.0f), coordinate)
        * std::max(extent, 0.0f);
}

} // namespace water
