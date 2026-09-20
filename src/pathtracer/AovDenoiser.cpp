#include "pathtracer/AovDenoiser.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include "pathtracer/ProgressiveRenderer.h"

namespace pathtracer {
namespace {

constexpr glm::vec3 luminanceWeights(0.2126f, 0.7152f, 0.0722f);

float luminance(const glm::vec3& value) {
    return glm::dot(value, luminanceWeights);
}

bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void validate(const RenderImage& image) {
    const std::size_t count = static_cast<std::size_t>(image.width) * image.height;
    if (!image.width || !image.height || !image.completedSamples
        || image.sum.size() != count || image.directSum.size() != count
        || image.indirectSum.size() != count || image.albedoSum.size() != count
        || image.normalSum.size() != count || image.depthSum.size() != count
        || image.primaryHitCount.size() != count) {
        throw std::invalid_argument("Cannot denoise an empty or inconsistent render image");
    }
}

std::vector<glm::vec3> clampFireflies(
    const std::vector<glm::vec3>& input,
    std::uint32_t width,
    std::uint32_t height,
    float factor
) {
    std::vector<glm::vec3> result = input;
    factor = std::max(factor, 1.0f);
    std::array<float, 9> neighborhood{};
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            std::size_t size = 0U;
            for (int oy = -1; oy <= 1; ++oy) {
                const int sy = std::clamp(static_cast<int>(y) + oy, 0, static_cast<int>(height) - 1);
                for (int ox = -1; ox <= 1; ++ox) {
                    const int sx = std::clamp(static_cast<int>(x) + ox, 0, static_cast<int>(width) - 1);
                    neighborhood[size++] = luminance(input[static_cast<std::size_t>(sy) * width + sx]);
                }
            }
            std::nth_element(neighborhood.begin(), neighborhood.begin() + 4, neighborhood.end());
            const float limit = std::max(neighborhood[4] * factor, 1.0e-5f);
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            const float value = luminance(input[index]);
            if (value > limit) result[index] *= limit / value;
        }
    }
    return result;
}

struct Reprojection {
    int x{0};
    int y{0};
    float expectedDepth{0.0f};
    bool valid{false};
};

glm::vec3 cameraDirection(
    const SnapshotCamera& camera,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t x,
    std::uint32_t y
) {
    const float scale = std::tan(camera.verticalFieldOfViewRadians * 0.5f);
    const glm::vec3 local(
        (2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(width) - 1.0f)
            * camera.aspectRatio * scale,
        (1.0f - 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(height)) * scale,
        -1.0f
    );
    return glm::normalize(glm::mat3(glm::inverse(camera.view)) * local);
}

Reprojection reproject(
    const SnapshotCamera& current,
    const SnapshotCamera& previous,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t x,
    std::uint32_t y,
    float depth
) {
    Reprojection result;
    if (!(depth > 0.0f) || !std::isfinite(depth)) return result;
    const glm::vec3 world = current.position
        + cameraDirection(current, width, height, x, y) * depth;
    const glm::vec3 local = glm::vec3(previous.view * glm::vec4(world, 1.0f));
    if (!(local.z < -1.0e-5f)) return result;
    const float scale = std::tan(previous.verticalFieldOfViewRadians * 0.5f);
    const float ndcX = local.x / (-local.z * previous.aspectRatio * scale);
    const float ndcY = local.y / (-local.z * scale);
    if (std::abs(ndcX) > 1.0f || std::abs(ndcY) > 1.0f) return result;
    result.x = std::clamp(static_cast<int>((ndcX * 0.5f + 0.5f) * width), 0,
                          static_cast<int>(width) - 1);
    result.y = std::clamp(static_cast<int>((0.5f - ndcY * 0.5f) * height), 0,
                          static_cast<int>(height) - 1);
    result.expectedDepth = glm::length(world - previous.position);
    result.valid = std::isfinite(result.expectedDepth);
    return result;
}

glm::vec3 neighborhoodClamped(
    const std::vector<glm::vec3>& history,
    const std::vector<glm::vec3>& current,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t x,
    std::uint32_t y,
    std::size_t historyIndex
) {
    glm::vec3 minimum(std::numeric_limits<float>::infinity());
    glm::vec3 maximum(-std::numeric_limits<float>::infinity());
    for (int oy = -1; oy <= 1; ++oy) {
        const int sy = std::clamp(static_cast<int>(y) + oy, 0, static_cast<int>(height) - 1);
        for (int ox = -1; ox <= 1; ++ox) {
            const int sx = std::clamp(static_cast<int>(x) + ox, 0, static_cast<int>(width) - 1);
            const glm::vec3 value = current[static_cast<std::size_t>(sy) * width + sx];
            minimum = glm::min(minimum, value);
            maximum = glm::max(maximum, value);
        }
    }
    return glm::clamp(history[historyIndex], minimum, maximum);
}

void temporalAccumulate(
    std::vector<glm::vec3>& beauty,
    std::vector<glm::vec3>& direct,
    std::vector<glm::vec3>& indirect,
    std::vector<float>& variance,
    const std::vector<glm::vec3>& albedo,
    const std::vector<glm::vec3>& normal,
    const std::vector<float>& depth,
    const std::vector<std::uint8_t>& hitMask,
    const SnapshotCamera& camera,
    const DenoiseSettings& settings,
    TemporalDenoiseState& history,
    std::uint32_t width,
    std::uint32_t height,
    std::size_t& accepted,
    std::size_t& rejected
) {
    const std::size_t count = beauty.size();
    std::vector<float> firstMoment(count);
    std::vector<float> secondMoment(count);
    const bool usable = history.valid && history.width == width && history.height == height
        && history.beauty.size() == count;
    const float historyWeight = std::clamp(settings.temporalHistoryWeight, 0.0f, 0.98f);
    const std::vector<glm::vec3> currentBeauty = beauty;
    const std::vector<glm::vec3> currentDirect = direct;
    const std::vector<glm::vec3> currentIndirect = indirect;
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            const float currentLuminance = luminance(currentBeauty[index]);
            firstMoment[index] = currentLuminance;
            secondMoment[index] = currentLuminance * currentLuminance;
            if (!usable) continue;

            Reprojection mapping;
            if (hitMask[index]) {
                mapping = reproject(camera, history.camera, width, height, x, y, depth[index]);
            } else {
                mapping = {static_cast<int>(x), static_cast<int>(y), 0.0f, true};
            }
            if (!mapping.valid) {
                ++rejected;
                continue;
            }
            const std::size_t previousIndex = static_cast<std::size_t>(mapping.y) * width
                + static_cast<std::size_t>(mapping.x);
            bool compatible = hitMask[index] == history.hitMask[previousIndex];
            if (compatible && hitMask[index]) {
                const float depthTolerance = settings.disocclusionDepthThreshold
                    * std::max(mapping.expectedDepth, 1.0f);
                compatible = std::abs(history.depth[previousIndex] - mapping.expectedDepth)
                        <= depthTolerance
                    && glm::dot(normal[index], history.normal[previousIndex])
                        >= settings.disocclusionNormalThreshold
                    && glm::length(albedo[index] - history.albedo[previousIndex])
                        <= settings.disocclusionAlbedoThreshold;
            }
            if (!compatible) {
                ++rejected;
                continue;
            }

            const glm::vec3 oldBeauty = neighborhoodClamped(
                history.beauty, currentBeauty, width, height, x, y, previousIndex);
            const glm::vec3 oldDirect = neighborhoodClamped(
                history.direct, currentDirect, width, height, x, y, previousIndex);
            const glm::vec3 oldIndirect = neighborhoodClamped(
                history.indirect, currentIndirect, width, height, x, y, previousIndex);
            beauty[index] = glm::mix(currentBeauty[index], oldBeauty, historyWeight);
            direct[index] = glm::mix(currentDirect[index], oldDirect, historyWeight);
            indirect[index] = glm::mix(currentIndirect[index], oldIndirect, historyWeight);
            firstMoment[index] = glm::mix(currentLuminance,
                history.luminanceMoment[previousIndex], historyWeight);
            secondMoment[index] = glm::mix(currentLuminance * currentLuminance,
                history.luminanceSquaredMoment[previousIndex], historyWeight);
            variance[index] = std::max(secondMoment[index]
                - firstMoment[index] * firstMoment[index], variance[index]);
            ++accepted;
        }
    }

    history.width = width;
    history.height = height;
    history.camera = camera;
    history.beauty = beauty;
    history.direct = direct;
    history.indirect = indirect;
    history.albedo = albedo;
    history.normal = normal;
    history.depth = depth;
    history.luminanceMoment = std::move(firstMoment);
    history.luminanceSquaredMoment = std::move(secondMoment);
    history.hitMask = hitMask;
    history.valid = true;
}

std::vector<glm::vec3> atrous(
    std::vector<glm::vec3> input,
    const std::vector<glm::vec3>& albedo,
    const std::vector<glm::vec3>& normal,
    const std::vector<float>& depth,
    const std::vector<float>& variance,
    const std::vector<std::uint8_t>& hitMask,
    std::uint32_t width,
    std::uint32_t height,
    const DenoiseSettings& settings
) {
    static constexpr std::array<float, 5> kernel{1.0f / 16.0f, 4.0f / 16.0f,
                                                 6.0f / 16.0f, 4.0f / 16.0f,
                                                 1.0f / 16.0f};
    std::vector<glm::vec3> output(input.size());
    const float albedoDenominator = std::max(settings.albedoSigma * settings.albedoSigma, 1.0e-6f);
    const std::uint32_t iterations = std::min(settings.atrousIterations, 8U);
    for (std::uint32_t iteration = 0U; iteration < iterations; ++iteration) {
        const int step = 1 << iteration;
        for (std::uint32_t y = 0U; y < height; ++y) {
            for (std::uint32_t x = 0U; x < width; ++x) {
                const std::size_t centerIndex = static_cast<std::size_t>(y) * width + x;
                const float centerLuminance = luminance(input[centerIndex]);
                const float colorScale = std::max(settings.colorSigma
                    * std::sqrt(std::max(variance[centerIndex], 0.0f)), 0.01f);
                glm::vec3 sum(0.0f);
                float totalWeight = 0.0f;
                for (int ky = -2; ky <= 2; ++ky) {
                    const int sy = static_cast<int>(y) + ky * step;
                    if (sy < 0 || sy >= static_cast<int>(height)) continue;
                    for (int kx = -2; kx <= 2; ++kx) {
                        const int sx = static_cast<int>(x) + kx * step;
                        if (sx < 0 || sx >= static_cast<int>(width)) continue;
                        const std::size_t sampleIndex = static_cast<std::size_t>(sy) * width
                            + static_cast<std::size_t>(sx);
                        if (hitMask[sampleIndex] != hitMask[centerIndex]) continue;
                        float weight = kernel[static_cast<std::size_t>(kx + 2)]
                            * kernel[static_cast<std::size_t>(ky + 2)];
                        weight *= std::exp(-std::abs(luminance(input[sampleIndex])
                            - centerLuminance) / colorScale);
                        if (hitMask[centerIndex]) {
                            const glm::vec3 albedoDelta = albedo[sampleIndex] - albedo[centerIndex];
                            weight *= std::exp(-glm::dot(albedoDelta, albedoDelta) / albedoDenominator);
                            weight *= std::pow(std::max(glm::dot(normal[sampleIndex], normal[centerIndex]), 0.0f),
                                               std::max(settings.normalExponent, 1.0f));
                            const float relativeDepth = std::abs(depth[sampleIndex] - depth[centerIndex])
                                / std::max(depth[centerIndex], 1.0e-3f);
                            weight *= std::exp(-relativeDepth / std::max(settings.depthSigma, 1.0e-5f));
                        }
                        sum += input[sampleIndex] * weight;
                        totalWeight += weight;
                    }
                }
                output[centerIndex] = totalWeight > 0.0f
                    ? sum / totalWeight : input[centerIndex];
            }
        }
        input.swap(output);
    }
    return input;
}

double gradientEnergy(
    const std::vector<glm::vec3>& image,
    std::uint32_t width,
    std::uint32_t height
) {
    double sum = 0.0;
    for (std::uint32_t y = 0U; y + 1U < height; ++y) {
        for (std::uint32_t x = 0U; x + 1U < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            const double value = luminance(image[index]);
            const double dx = luminance(image[index + 1U]) - value;
            const double dy = luminance(image[index + width]) - value;
            sum += std::sqrt(dx * dx + dy * dy);
        }
    }
    return sum;
}

} // namespace

void TemporalDenoiseState::reset() {
    *this = TemporalDenoiseState{};
}

DenoisedImage denoiseAovs(
    const RenderImage& image,
    const DenoiseSettings& settings,
    const SnapshotCamera* camera,
    TemporalDenoiseState* temporalState
) {
    validate(image);
    const auto start = std::chrono::steady_clock::now();
    DenoisedImage result;
    result.width = image.width;
    result.height = image.height;
    result.direct = image.directPixels();
    result.indirect = image.indirectPixels();
    result.beauty = image.linearPixels();
    result.variance = image.variancePixels();
    const auto albedo = image.albedoPixels();
    const auto normal = image.normalPixels();
    const auto depth = image.depthPixels();
    std::vector<std::uint8_t> hitMask(image.primaryHitCount.size());
    std::transform(image.primaryHitCount.begin(), image.primaryHitCount.end(), hitMask.begin(),
        [](std::uint32_t hits) { return static_cast<std::uint8_t>(hits != 0U); });

    if (settings.fireflyClampEnabled) {
        result.direct = clampFireflies(result.direct, image.width, image.height,
                                       settings.fireflyClampFactor);
        result.indirect = clampFireflies(result.indirect, image.width, image.height,
                                         settings.fireflyClampFactor);
        for (std::size_t i = 0U; i < result.beauty.size(); ++i)
            result.beauty[i] = result.direct[i] + result.indirect[i];
    }

    if (settings.temporalEnabled && camera && temporalState) {
        temporalAccumulate(result.beauty, result.direct, result.indirect, result.variance,
                           albedo, normal, depth, hitMask, *camera, settings, *temporalState,
                           image.width, image.height, result.temporalAccepted,
                           result.temporalRejected);
    }

    if (settings.enabled && settings.atrousIterations > 0U) {
        result.direct = atrous(std::move(result.direct), albedo, normal, depth,
                               result.variance, hitMask, image.width, image.height, settings);
        result.indirect = atrous(std::move(result.indirect), albedo, normal, depth,
                                 result.variance, hitMask, image.width, image.height, settings);
        result.beauty.resize(result.direct.size());
        for (std::size_t i = 0U; i < result.beauty.size(); ++i)
            result.beauty[i] = result.direct[i] + result.indirect[i];
    }
    for (glm::vec3& value : result.beauty) {
        if (!finite(value)) value = glm::vec3(0.0f);
        value = glm::max(value, glm::vec3(0.0f));
    }
    result.milliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    return result;
}

ImageMetrics compareImages(
    const std::vector<glm::vec3>& candidate,
    const std::vector<glm::vec3>& reference,
    std::uint32_t width,
    std::uint32_t height
) {
    const std::size_t count = static_cast<std::size_t>(width) * height;
    if (!width || !height || candidate.size() != count || reference.size() != count)
        throw std::invalid_argument("Cannot compare inconsistent images");
    double squaredError = 0.0;
    double referencePeak = 1.0;
    for (std::size_t i = 0U; i < count; ++i) {
        const glm::dvec3 delta = glm::dvec3(candidate[i]) - glm::dvec3(reference[i]);
        squaredError += glm::dot(delta, delta);
        referencePeak = std::max(referencePeak, static_cast<double>(std::max({
            reference[i].x, reference[i].y, reference[i].z})));
    }
    ImageMetrics result;
    result.rmse = std::sqrt(squaredError / static_cast<double>(count * 3U));
    result.psnr = result.rmse > 0.0
        ? 20.0 * std::log10(referencePeak / result.rmse)
        : std::numeric_limits<double>::infinity();

    constexpr std::uint32_t window = 8U;
    double ssimSum = 0.0;
    std::size_t windowCount = 0U;
    const double c1 = std::pow(0.01 * referencePeak, 2.0);
    const double c2 = std::pow(0.03 * referencePeak, 2.0);
    for (std::uint32_t by = 0U; by < height; by += window) {
        for (std::uint32_t bx = 0U; bx < width; bx += window) {
            double meanA = 0.0, meanB = 0.0;
            std::size_t n = 0U;
            for (std::uint32_t y = by; y < std::min(by + window, height); ++y)
                for (std::uint32_t x = bx; x < std::min(bx + window, width); ++x) {
                    const std::size_t i = static_cast<std::size_t>(y) * width + x;
                    meanA += luminance(candidate[i]);
                    meanB += luminance(reference[i]);
                    ++n;
                }
            meanA /= static_cast<double>(n);
            meanB /= static_cast<double>(n);
            double varianceA = 0.0, varianceB = 0.0, covariance = 0.0;
            for (std::uint32_t y = by; y < std::min(by + window, height); ++y)
                for (std::uint32_t x = bx; x < std::min(bx + window, width); ++x) {
                    const std::size_t i = static_cast<std::size_t>(y) * width + x;
                    const double a = luminance(candidate[i]) - meanA;
                    const double b = luminance(reference[i]) - meanB;
                    varianceA += a * a;
                    varianceB += b * b;
                    covariance += a * b;
                }
            const double denominator = static_cast<double>(std::max<std::size_t>(n - 1U, 1U));
            varianceA /= denominator;
            varianceB /= denominator;
            covariance /= denominator;
            ssimSum += ((2.0 * meanA * meanB + c1) * (2.0 * covariance + c2))
                / ((meanA * meanA + meanB * meanB + c1)
                    * (varianceA + varianceB + c2));
            ++windowCount;
        }
    }
    result.ssim = ssimSum / static_cast<double>(windowCount);
    const double referenceGradient = gradientEnergy(reference, width, height);
    result.gradientRetention = referenceGradient > 0.0
        ? gradientEnergy(candidate, width, height) / referenceGradient : 1.0;
    std::vector<float> referenceLuminance;
    referenceLuminance.reserve(count);
    for (const glm::vec3& value : reference) referenceLuminance.push_back(luminance(value));
    const std::size_t percentile = std::min(count - 1U,
        static_cast<std::size_t>(static_cast<double>(count) * 0.999));
    std::nth_element(referenceLuminance.begin(), referenceLuminance.begin() + percentile,
                     referenceLuminance.end());
    const float fireflyThreshold = std::max(referenceLuminance[percentile] * 2.0f, 1.0f);
    result.fireflyPixels = static_cast<std::size_t>(std::count_if(
        candidate.begin(), candidate.end(), [&](const glm::vec3& value) {
            return luminance(value) > fireflyThreshold;
        }));
    return result;
}

} // namespace pathtracer
