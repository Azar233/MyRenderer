#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

#include "pathtracer/SceneSnapshot.h"

namespace pathtracer {

struct RenderImage;

struct DenoiseSettings {
    bool enabled{false};
    std::uint32_t atrousIterations{4U};
    float colorSigma{4.0f};
    float albedoSigma{0.25f};
    float normalExponent{64.0f};
    float depthSigma{0.02f};
    bool temporalEnabled{false};
    float temporalHistoryWeight{0.8f};
    float disocclusionDepthThreshold{0.03f};
    float disocclusionNormalThreshold{0.8f};
    float disocclusionAlbedoThreshold{0.25f};
    // Optional and deliberately biased. It is never enabled for reference images.
    bool fireflyClampEnabled{false};
    float fireflyClampFactor{8.0f};
};

struct DenoisedImage {
    std::uint32_t width{0U};
    std::uint32_t height{0U};
    std::vector<glm::vec3> beauty;
    std::vector<glm::vec3> direct;
    std::vector<glm::vec3> indirect;
    std::vector<float> variance;
    double milliseconds{0.0};
    std::size_t temporalAccepted{0U};
    std::size_t temporalRejected{0U};

    bool empty() const { return beauty.empty(); }
};

struct TemporalDenoiseState {
    std::uint32_t width{0U};
    std::uint32_t height{0U};
    SnapshotCamera camera;
    std::vector<glm::vec3> beauty;
    std::vector<glm::vec3> direct;
    std::vector<glm::vec3> indirect;
    std::vector<glm::vec3> albedo;
    std::vector<glm::vec3> normal;
    std::vector<float> depth;
    std::vector<float> luminanceMoment;
    std::vector<float> luminanceSquaredMoment;
    std::vector<std::uint8_t> hitMask;
    bool valid{false};

    void reset();
};

DenoisedImage denoiseAovs(
    const RenderImage& image,
    const DenoiseSettings& settings,
    const SnapshotCamera* camera = nullptr,
    TemporalDenoiseState* temporalState = nullptr
);

struct ImageMetrics {
    double rmse{0.0};
    double psnr{0.0};
    double ssim{0.0};
    // Ratio of denoised/reference luminance gradient energy. Values below one
    // indicate detail loss; values well above one usually indicate residual noise.
    double gradientRetention{0.0};
    std::size_t fireflyPixels{0U};
};

ImageMetrics compareImages(
    const std::vector<glm::vec3>& candidate,
    const std::vector<glm::vec3>& reference,
    std::uint32_t width,
    std::uint32_t height
);

} // namespace pathtracer
