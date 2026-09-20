#include "pathtracer/AovDenoiser.h"
#include "pathtracer/LightSampling.h"
#include "pathtracer/PbrBsdf.h"
#include "pathtracer/ProgressiveRenderer.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#include <glm/geometric.hpp>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

pathtracer::RenderImage noisyEdge(std::uint32_t width = 32U, std::uint32_t height = 16U) {
    pathtracer::RenderImage image;
    image.width = width;
    image.height = height;
    image.completedSamples = 4U;
    const std::size_t count = static_cast<std::size_t>(width) * height;
    image.sum.resize(count);
    image.directSum.resize(count);
    image.indirectSum.resize(count, glm::vec3(0.0f));
    image.albedoSum.resize(count);
    image.normalSum.resize(count, glm::vec3(0.0f, 0.0f, 4.0f));
    image.depthSum.resize(count, 8.0f);
    image.primaryHitCount.resize(count, 4U);
    image.sampleCounts.resize(count, 4U);
    image.luminanceSum.resize(count);
    image.luminanceSquaredSum.resize(count);
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            const glm::vec3 truth = x < width / 2U
                ? glm::vec3(0.15f, 0.2f, 0.25f)
                : glm::vec3(0.8f, 0.65f, 0.3f);
            const float noise = ((x * 17U + y * 13U) & 1U) ? 0.22f : -0.22f;
            const glm::vec3 sample = glm::max(truth + glm::vec3(noise), glm::vec3(0.0f));
            image.sum[index] = sample * 4.0f;
            image.directSum[index] = image.sum[index];
            image.albedoSum[index] = (x < width / 2U
                ? glm::vec3(0.2f, 0.3f, 0.4f) : glm::vec3(0.9f, 0.7f, 0.2f)) * 4.0f;
            const float value = glm::dot(sample, glm::vec3(0.2126f, 0.7152f, 0.0722f));
            image.luminanceSum[index] = value * 4.0f;
            image.luminanceSquaredSum[index] = value * value * 4.0f + 0.18f;
        }
    }
    return image;
}

std::vector<glm::vec3> edgeReference(std::uint32_t width, std::uint32_t height) {
    std::vector<glm::vec3> result(static_cast<std::size_t>(width) * height);
    for (std::uint32_t y = 0U; y < height; ++y)
        for (std::uint32_t x = 0U; x < width; ++x)
            result[static_cast<std::size_t>(y) * width + x] = x < width / 2U
                ? glm::vec3(0.15f, 0.2f, 0.25f)
                : glm::vec3(0.8f, 0.65f, 0.3f);
    return result;
}

void spatialDenoisingReducesErrorAndPreservesEdges() {
    const pathtracer::RenderImage image = noisyEdge();
    pathtracer::DenoiseSettings settings;
    settings.enabled = true;
    settings.atrousIterations = 3U;
    const pathtracer::DenoisedImage filtered = pathtracer::denoiseAovs(image, settings);
    const auto reference = edgeReference(image.width, image.height);
    const auto raw = pathtracer::compareImages(
        image.linearPixels(), reference, image.width, image.height);
    const auto denoised = pathtracer::compareImages(
        filtered.beauty, reference, image.width, image.height);
    std::cout << "Synthetic A-Trous RMSE raw/denoised: " << raw.rmse << " / "
              << denoised.rmse << ", gradient retention "
              << denoised.gradientRetention << "\n";
    require(denoised.rmse < raw.rmse * 0.65,
            "AOV A-Trous did not significantly reduce synthetic noise");
    glm::vec3 left(0.0f), right(0.0f);
    for (std::uint32_t y = 0U; y < image.height; ++y) {
        left += filtered.beauty[static_cast<std::size_t>(y) * image.width
            + image.width / 2U - 1U];
        right += filtered.beauty[static_cast<std::size_t>(y) * image.width
            + image.width / 2U];
    }
    left /= static_cast<float>(image.height);
    right /= static_cast<float>(image.height);
    const float retainedContrast = glm::length(right - left)
        / glm::length(glm::vec3(0.8f, 0.65f, 0.3f)
            - glm::vec3(0.15f, 0.2f, 0.25f));
    require(retainedContrast > 0.8f,
            "AOV A-Trous blurred across the Albedo edge");
    require(filtered.direct == filtered.beauty,
            "Separate Direct filtering does not reconstruct Beauty");
}

void temporalHistoryRejectsDisocclusion() {
    pathtracer::RenderImage first = noisyEdge();
    pathtracer::DenoiseSettings settings;
    settings.enabled = true;
    settings.temporalEnabled = true;
    settings.atrousIterations = 1U;
    pathtracer::SnapshotCamera camera;
    pathtracer::TemporalDenoiseState state;
    pathtracer::denoiseAovs(first, settings, &camera, &state);
    const pathtracer::DenoisedImage stable = pathtracer::denoiseAovs(
        first, settings, &camera, &state);
    require(stable.temporalAccepted == first.sum.size(),
            "Stable temporal frame did not accept its history");

    pathtracer::RenderImage changed = first;
    for (std::uint32_t y = 0U; y < changed.height; ++y) {
        for (std::uint32_t x = changed.width / 2U; x < changed.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * changed.width + x;
            changed.depthSum[index] = 40.0f;
        }
    }
    const pathtracer::DenoisedImage disoccluded = pathtracer::denoiseAovs(
        changed, settings, &camera, &state);
    require(disoccluded.temporalRejected >= first.sum.size() / 2U,
            "Depth disocclusion did not reject stale temporal history");
}

void lightAliasTableUsesPowerWeights() {
    pathtracer::SnapshotCamera camera;
    pathtracer::SceneSnapshotLighting lighting;
    lighting.localLights.push_back({
        pathtracer::SnapshotLocalLightType::Point, {-2.0f, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}, 4.0f, 0.0f});
    lighting.localLights.push_back({
        pathtracer::SnapshotLocalLightType::Point, {2.0f, 0.0f, 0.0f}, {}, {20.0f, 20.0f, 20.0f}, 4.0f, 0.0f});
    pathtracer::SceneSnapshotBuilder builder(camera, lighting);
    const pathtracer::SceneSnapshot scene = builder.finish();
    const pathtracer::SceneLights weighted(
        scene, {}, pathtracer::LightSelectionStrategy::PowerWeighted);
    std::size_t bright = 0U;
    constexpr std::size_t samples = 10000U;
    for (std::size_t i = 0U; i < samples; ++i) {
        const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(samples);
        const auto sample = weighted.sample({0.0f, 0.0f, 0.0f}, u, {0.5f, 0.5f});
        if (sample.direction.x > 0.0f) ++bright;
    }
    require(bright > samples * 9U / 10U,
            "Power-weighted alias table did not favor the bright light");
}

void visibleNormalSamplingImprovesGrazingValidity() {
    const glm::vec3 normal(0.0f, 0.0f, 1.0f);
    const glm::vec3 outgoing = glm::normalize(glm::vec3(0.995f, 0.0f, 0.1f));
    const pathtracer::PbrSurface surface{{0.9f, 0.8f, 0.6f}, 0.8f, 0.28f};
    pathtracer::Sampler random(20260917U, 3U, 9U);
    std::size_t distributionValid = 0U;
    std::size_t visibleValid = 0U;
    constexpr std::size_t count = 20000U;
    for (std::size_t i = 0U; i < count; ++i) {
        const float component = random.next() * 0.08f; // Force the specular branch.
        const glm::vec2 direction(random.next(), random.next());
        distributionValid += pathtracer::samplePbrBsdf(
            surface, normal, outgoing, component, direction,
            pathtracer::GgxSamplingStrategy::Distribution).valid;
        visibleValid += pathtracer::samplePbrBsdf(
            surface, normal, outgoing, component, direction,
            pathtracer::GgxSamplingStrategy::VisibleNormals).valid;
    }
    std::cout << "GGX grazing valid NDF/VNDF: " << distributionValid << " / "
              << visibleValid << "\n";
    require(visibleValid > distributionValid,
            "GGX VNDF did not reduce below-surface grazing samples");
}

} // namespace

int main() {
    try {
        spatialDenoisingReducesErrorAndPreservesEdges();
        temporalHistoryRejectsDisocclusion();
        lightAliasTableUsesPowerWeights();
        visibleNormalSamplingImprovesGrazingValidity();
        std::cout << "P0-D denoising and sampling tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
