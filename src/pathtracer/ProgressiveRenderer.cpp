#include "pathtracer/ProgressiveRenderer.h"
#include "pathtracer/MaterialBsdf.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <limits>
#include <stdexcept>
#include <glm/gtc/matrix_inverse.hpp>
namespace pathtracer {
namespace {
bool benefitsFromInstancing(const SceneSnapshot& snapshot) {
    std::set<std::pair<std::uint32_t, std::uint32_t>> uniqueMeshes;
    std::size_t uniqueTriangles = 0U;
    std::size_t expandedTriangles = 0U;
    for (const SceneSnapshotInstance& instance : snapshot.instances()) {
        if (!instance.transformInvertible || instance.assetIndex >= snapshot.assets().size()) continue;
        const auto& model = snapshot.assets()[instance.assetIndex].model;
        if (!model || instance.meshIndex >= model->meshes.size()) continue;
        const std::size_t triangleCount = model->meshes[instance.meshIndex].indices.size() / 3U;
        expandedTriangles += triangleCount;
        if (uniqueMeshes.emplace(instance.assetIndex, instance.meshIndex).second)
            uniqueTriangles += triangleCount;
    }
    return uniqueTriangles > 0U && expandedTriangles >= uniqueTriangles * 2U;
}
std::uint32_t mix(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    return x ^ (x >> 16);
}
bool cancelled(const std::atomic<bool> *flag) {
    return flag && flag->load();
}
bool finite(glm::vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
std::size_t resolvedWorkerCount(std::uint32_t requested, std::size_t tileCount) {
    const std::size_t available = std::max<std::size_t>(std::thread::hardware_concurrency(), 1U);
    const std::size_t desired = requested ? requested : available;
    return std::max<std::size_t>(1U, std::min({desired, tileCount, std::size_t(256U)}));
}
glm::vec3 linearTint(glm::vec3 tint) {
    for (int c = 0; c < 3; ++c) {
        const float value = std::clamp(tint[c], 0.0f, 1.0f);
        tint[c] = value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
    }
    return tint;
}
void validate(const SnapshotCamera &c, const RenderSettings &s) {
    if (!s.width || !s.height || s.width > 8192 || s.height > 8192 || !s.samplesPerPixel || !s.maxDepth ||
        s.maxDepth > 1024 || !s.tileSize || s.tileSize > 1024 || s.workerCount > 256)
        throw std::invalid_argument("Invalid dimensions, SPP or Max Depth");
    if (s.adaptiveSampling
        && (s.adaptiveMinimumSamples < 2U
            || s.adaptiveMinimumSamples > s.samplesPerPixel || !s.adaptiveCheckInterval
            || !std::isfinite(s.adaptiveRelativeError) || s.adaptiveRelativeError < 0.0f
            || !std::isfinite(s.adaptiveAbsoluteError) || s.adaptiveAbsoluteError < 0.0f
            || (s.adaptiveRelativeError == 0.0f && s.adaptiveAbsoluteError == 0.0f))) {
        throw std::invalid_argument("Invalid adaptive sampling settings");
    }
    if (!finite(c.position) || !std::isfinite(c.aspectRatio) || c.aspectRatio <= 0 ||
        !std::isfinite(c.verticalFieldOfViewRadians) || c.verticalFieldOfViewRadians <= 0 ||
        c.verticalFieldOfViewRadians >= 3.14159265f)
        throw std::invalid_argument("Invalid perspective camera");
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (!std::isfinite(c.view[i][j]))
                throw std::invalid_argument("Nonfinite view");
    if (std::abs(glm::determinant(c.view)) < 1e-12f)
        throw std::invalid_argument("Singular view");
}
} // namespace
Sampler::Sampler(std::uint32_t seed, std::uint32_t pixel, std::uint32_t sample)
    : state_(mix(seed ^ mix(pixel) ^ mix(sample + 0x9e3779b9U))) {}
float Sampler::next() {
    state_ += 0x9e3779b9U;
    return static_cast<float>(mix(state_) >> 8) * (1.0f / 16777216.0f);
}
Ray cameraRay(const SnapshotCamera &c, const RenderSettings &s, std::uint32_t x, std::uint32_t y,
              glm::vec2 jitter) {
    validate(c, s);
    if (x >= s.width || y >= s.height || !std::isfinite(jitter.x) || !std::isfinite(jitter.y) ||
        jitter.x < 0 || jitter.x > 1 || jitter.y < 0 || jitter.y > 1)
        throw std::invalid_argument("Invalid pixel/jitter");
    const float scale = std::tan(c.verticalFieldOfViewRadians * 0.5f);
    const glm::vec3 local((2 * (static_cast<float>(x) + jitter.x) / s.width - 1) * c.aspectRatio * scale,
                          (1 - 2 * (static_cast<float>(y) + jitter.y) / s.height) * scale, -1);
    return Ray{c.position, glm::normalize(glm::mat3(glm::inverse(c.view)) * local)};
}
std::vector<glm::vec3> RenderImage::linearPixels() const {
    auto pixels = sum;
    for (std::size_t index = 0U; index < pixels.size(); ++index) {
        const std::uint32_t count = sampleCounts.size() == pixels.size()
            ? sampleCounts[index] : completedSamples;
        pixels[index] = count ? pixels[index] / static_cast<float>(count) : glm::vec3(0.0f);
    }
    return pixels;
}

std::vector<glm::vec3> RenderImage::albedoPixels() const {
    auto pixels = albedoSum;
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        const auto hits = index < primaryHitCount.size() ? primaryHitCount[index] : 0U;
        pixels[index] = hits ? pixels[index] / static_cast<float>(hits) : glm::vec3(0.0f);
    }
    return pixels;
}

std::vector<glm::vec3> RenderImage::normalPixels() const {
    auto pixels = normalSum;
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        const auto hits = index < primaryHitCount.size() ? primaryHitCount[index] : 0U;
        if (hits) {
            pixels[index] /= static_cast<float>(hits);
            const float lengthSquared = glm::dot(pixels[index], pixels[index]);
            if (lengthSquared > 1.0e-12f)
                pixels[index] *= 1.0f / std::sqrt(lengthSquared);
        } else {
            pixels[index] = glm::vec3(0.0f);
        }
    }
    return pixels;
}

std::vector<float> RenderImage::depthPixels() const {
    auto pixels = depthSum;
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        const auto hits = index < primaryHitCount.size() ? primaryHitCount[index] : 0U;
        pixels[index] = hits ? pixels[index] / static_cast<float>(hits) : 0.0f;
    }
    return pixels;
}

std::vector<glm::vec3> RenderImage::directPixels() const {
    auto pixels = directSum;
    for (std::size_t index = 0U; index < pixels.size(); ++index) {
        const std::uint32_t count = sampleCounts.size() == pixels.size()
            ? sampleCounts[index] : completedSamples;
        pixels[index] = count ? pixels[index] / static_cast<float>(count) : glm::vec3(0.0f);
    }
    return pixels;
}

std::vector<glm::vec3> RenderImage::indirectPixels() const {
    auto pixels = indirectSum;
    for (std::size_t index = 0U; index < pixels.size(); ++index) {
        const std::uint32_t count = sampleCounts.size() == pixels.size()
            ? sampleCounts[index] : completedSamples;
        pixels[index] = count ? pixels[index] / static_cast<float>(count) : glm::vec3(0.0f);
    }
    return pixels;
}

std::vector<float> RenderImage::sampleCountPixels() const {
    std::vector<float> pixels(sum.size(), static_cast<float>(completedSamples));
    if (sampleCounts.size() == sum.size()) {
        for (std::size_t index = 0U; index < pixels.size(); ++index)
            pixels[index] = static_cast<float>(sampleCounts[index]);
    }
    return pixels;
}

std::vector<float> RenderImage::variancePixels() const {
    std::vector<float> pixels(sum.size(), 0.0f);
    if (luminanceSum.size() != sum.size()
        || luminanceSquaredSum.size() != sum.size()) {
        return pixels;
    }
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        const std::uint32_t sampleCount = sampleCounts.size() == sum.size()
            ? sampleCounts[index] : completedSamples;
        if (sampleCount < 2U) continue;
        const float count = static_cast<float>(sampleCount);
        const float centered = luminanceSquaredSum[index]
            - luminanceSum[index] * luminanceSum[index] / count;
        pixels[index] = std::max(centered / static_cast<float>(sampleCount - 1U), 0.0f);
    }
    return pixels;
}
ProgressiveRenderer::ProgressiveRenderer(SceneSnapshot snapshot, RenderSettings settings)
    : snapshot_(std::move(snapshot)), settings_(settings),
      lights_(snapshot_, buildWorldLightTriangles(snapshot_)), textures_(snapshot_),
      tiles_(makeRenderTiles(settings.width, settings.height, settings.tileSize)),
      tilePool_(resolvedWorkerCount(settings.workerCount, tiles_.size())) {
    validate(snapshot_.camera(), settings_);
    const AccelerationStructure acceleration = settings_.accelerationStructure
            == AccelerationStructure::Automatic
        ? (benefitsFromInstancing(snapshot_) ? AccelerationStructure::TwoLevelBvh
                                             : AccelerationStructure::WorldBvh)
        : settings_.accelerationStructure;
    image_.statistics.accelerationStructure = acceleration;
    if (acceleration == AccelerationStructure::TwoLevelBvh) {
        instancedBvh_ = std::make_unique<InstancedBvh>(snapshot_, settings_.bvhSplitStrategy);
        image_.statistics.instancedBvhBuild = instancedBvh_->stats();
        const auto& source = instancedBvh_->stats();
        image_.statistics.bvhBuild.primitiveCount = source.expandedPrimitiveCount;
        image_.statistics.bvhBuild.nodeCount = source.blasNodeCount + source.tlasNodeCount;
        image_.statistics.bvhBuild.maximumDepth = source.maximumBlasDepth + source.maximumTlasDepth;
        image_.statistics.bvhBuild.buildMilliseconds = source.buildMilliseconds;
        image_.statistics.estimatedAccelerationBytes = source.estimatedBytes;
    } else {
        triangles_ = buildWorldTriangles(snapshot_);
        bvh_ = std::make_unique<Bvh>(triangles_, 4U, settings_.bvhSplitStrategy);
        image_.statistics.bvhBuild = bvh_->stats();
        image_.statistics.estimatedAccelerationBytes = bvh_->estimatedBytes();
    }
    image_.width = settings.width;
    image_.height = settings.height;
    const std::size_t pixelCount = static_cast<std::size_t>(settings.width) * settings.height;
    image_.sum.resize(pixelCount, glm::vec3(0));
    image_.albedoSum.resize(pixelCount, glm::vec3(0));
    image_.normalSum.resize(pixelCount, glm::vec3(0));
    image_.depthSum.resize(pixelCount, 0.0f);
    image_.primaryHitCount.resize(pixelCount, 0U);
    image_.directSum.resize(pixelCount, glm::vec3(0));
    image_.indirectSum.resize(pixelCount, glm::vec3(0));
    image_.luminanceSum.resize(pixelCount, 0.0f);
    image_.luminanceSquaredSum.resize(pixelCount, 0.0f);
    image_.sampleCounts.resize(pixelCount, 0U);
    activePixels_.resize(pixelCount, 1U);
    activeTileIndices_.resize(tiles_.size());
    for (std::size_t tile = 0U; tile < tiles_.size(); ++tile) activeTileIndices_[tile] = tile;
    activePixelCount_ = pixelCount;
    image_.statistics.activePixels = pixelCount;
    image_.statistics.workerCount = tilePool_.workerCount();
    image_.statistics.tileSize = settings_.tileSize;
}
bool ProgressiveRenderer::complete() const {
    return image_.completedSamples >= settings_.samplesPerPixel || activePixelCount_ == 0U;
}
bool ProgressiveRenderer::intersectScene(const Ray& ray, SurfaceInteraction& interaction,
                                         BvhTraversalStats* traversalStats) const {
    return instancedBvh_ ? instancedBvh_->intersect(ray, interaction, traversalStats)
                         : bvh_->intersect(ray, interaction, traversalStats);
}
bool ProgressiveRenderer::sceneOccluded(const Ray& ray, BvhTraversalStats* traversalStats) const {
    return instancedBvh_ ? instancedBvh_->occluded(ray, traversalStats)
                         : bvh_->occluded(ray, traversalStats);
}
ProgressiveRenderer::TraceResult ProgressiveRenderer::trace(
    Ray ray, Sampler &sampler, const std::atomic<bool> *cancel
) const {
    struct ActiveMedium {
        glm::vec3 attenuationColor{1.0f};
        float attenuationDistance{std::numeric_limits<float>::infinity()};
        std::uint32_t assetIndex{0U};
        std::int32_t materialIndex{-1};
        bool active{false};
    } medium;
    TraceResult result;
    glm::vec3 throughput(1);
    glm::vec3 previousSurfacePosition(0.0f);
    float previousBsdfPdf = 0.0f;
    bool hasPreviousSurface = false;
    bool previousWasDelta = false;
    for (std::uint32_t depth = 0; depth < settings_.maxDepth; ++depth) {
        if (cancelled(cancel))
            return {};
        SurfaceInteraction hit;
        ++result.pathRays;
        if (!intersectScene(ray, hit, &result.bvhTraversal)) {
            float environmentWeight = 1.0f;
            if (settings_.nextEventEstimation && hasPreviousSurface && !previousWasDelta) {
                environmentWeight = powerHeuristic(
                    previousBsdfPdf,
                    lights_.environmentPdf(ray.direction)
                );
            }
            const glm::vec3 missRadiance = depth == 0U
                    && !snapshot_.lighting().environment.visibleToCamera
                ? glm::max(snapshot_.lighting().environment.backgroundColor, glm::vec3(0.0f))
                : lights_.environmentRadiance(ray.direction);
            const glm::vec3 contribution = throughput * missRadiance * environmentWeight;
            result.radiance += contribution;
            (depth == 0 ? result.direct : result.indirect) += contribution;
            break;
        }
        if (medium.active) {
            throughput *= beerLambertTransmittance(
                medium.attenuationColor,
                medium.attenuationDistance,
                hit.t
            );
            if (glm::dot(throughput, throughput) == 0.0f) break;
        }
        MaterialData material;
        const auto &materials = snapshot_.assets().at(hit.assetIndex).model->materials;
        if (hit.materialIndex >= 0 && static_cast<std::size_t>(hit.materialIndex) < materials.size())
            material = materials[hit.materialIndex];
        if (hit.frontFace || material.doubleSided) {
            float emissionWeight = 1.0f;
            if (settings_.nextEventEstimation && hasPreviousSurface && !previousWasDelta) {
                emissionWeight = powerHeuristic(
                    previousBsdfPdf,
                    lights_.emissiveHitPdf(previousSurfacePosition, hit)
                );
            }
            const glm::vec3 contribution =
                throughput * glm::max(material.emissiveFactor, glm::vec3(0)) * emissionWeight;
            result.radiance += contribution;
            (depth == 0 ? result.direct : result.indirect) += contribution;
        }
        const EvaluatedPbrMaterial evaluated = textures_.evaluate(hit, material, linearTint(hit.tint));
        const glm::vec3 n = glm::dot(evaluated.shadingNormal, -ray.direction) > 0.0f
                                ? evaluated.shadingNormal
                                : hit.geometricNormal;
        if (depth == 0) {
            result.albedo = evaluated.surface.baseColor;
            result.normal = n;
            result.depth = hit.t;
            result.primaryHit = true;
        }
        const float epsilon =
            1e-4f * std::max(1.0f, std::max({std::abs(hit.position.x), std::abs(hit.position.y),
                                             std::abs(hit.position.z)}));
        if (settings_.nextEventEstimation && !lights_.empty()) {
            const DirectLightSample light = lights_.sample(
                hit.position,
                sampler.next(),
                {sampler.next(), sampler.next()}
            );
            const float noL = glm::dot(n, light.direction);
            if (light.valid && noL > 0.0f && glm::dot(hit.geometricNormal, light.direction) > 0.0f) {
                const float maximumDistance = std::isfinite(light.distance)
                                                  ? std::max(light.distance - epsilon, epsilon * 0.5f)
                                                  : light.distance;
                const Ray shadowRay{
                    hit.position + hit.geometricNormal * epsilon,
                    light.direction,
                    epsilon * 0.5f,
                    maximumDistance
                };
                ++result.shadowRays;
                if (!sceneOccluded(shadowRay, &result.bvhTraversal)) {
                    const glm::vec3 bsdfValue = evaluateMaterialBsdf(
                        evaluated, n, -ray.direction, light.direction
                    );
                    const float misWeight = light.delta
                                                ? 1.0f
                                                : powerHeuristic(
                                                      light.pdf,
                                                      materialBsdfPdf(
                                                          evaluated, n, -ray.direction, light.direction
                                                      )
                                                  );
                    const glm::vec3 contribution =
                        throughput * bsdfValue * light.radiance * (noL * misWeight / light.pdf);
                    result.radiance += contribution;
                    (depth == 0 ? result.direct : result.indirect) += contribution;
                }
            }
        }
        if (depth + 1 == settings_.maxDepth)
            break;
        const MaterialBsdfSample bsdf = sampleMaterialBsdf(
            evaluated,
            n,
            -ray.direction,
            hit.frontFace,
            sampler.next(),
            {sampler.next(), sampler.next()}
        );
        if (!bsdf.valid) break;
        const float geometricSide = glm::dot(hit.geometricNormal, bsdf.direction);
        if ((bsdf.transmitted && geometricSide >= 0.0f)
            || (!bsdf.transmitted && geometricSide <= 0.0f)) {
            break;
        }
        throughput *= bsdf.weight;
        if (glm::dot(throughput, throughput) == 0.0f)
            break;
        if (settings_.russianRouletteDepth > 0 && depth + 1 >= settings_.russianRouletteDepth) {
            const float survival = std::clamp(std::max({throughput.x, throughput.y, throughput.z}), 0.05f, 0.95f);
            if (sampler.next() >= survival)
                break;
            throughput /= survival;
        }
        if (bsdf.transmitted) {
            if (hit.frontFace && evaluated.thickness > 0.0f) {
                medium.attenuationColor = evaluated.attenuationColor;
                medium.attenuationDistance = evaluated.attenuationDistance;
                medium.assetIndex = hit.assetIndex;
                medium.materialIndex = hit.materialIndex;
                medium.active = true;
            } else if (!hit.frontFace && medium.active
                       && medium.assetIndex == hit.assetIndex
                       && medium.materialIndex == hit.materialIndex) {
                medium.active = false;
            }
        }
        previousSurfacePosition = hit.position;
        previousBsdfPdf = bsdf.pdf;
        hasPreviousSurface = true;
        previousWasDelta = bsdf.delta;
        const float offsetDirection = bsdf.transmitted ? -1.0f : 1.0f;
        ray = Ray{
            hit.position + hit.geometricNormal * (epsilon * offsetDirection),
            bsdf.direction,
            epsilon * 0.5f
        };
    }
    if (!finite(result.radiance) || !finite(result.direct) || !finite(result.indirect))
        throw std::runtime_error("Nonfinite radiance");
    return result;
}
bool ProgressiveRenderer::renderPass(const std::atomic<bool> *cancel) {
    if (complete() || cancelled(cancel))
        return false;
    const auto passStart = std::chrono::steady_clock::now();
    std::vector<TraceResult> pass(image_.sum.size());
    const std::size_t scheduledTileCount = activeTileIndices_.size();
    const bool completed = tilePool_.execute(scheduledTileCount, cancel, [&](std::size_t workIndex) {
        const RenderTile tile = tiles_[activeTileIndices_[workIndex]];
        for (std::uint32_t y = tile.yBegin; y < tile.yEnd; ++y) {
          for (std::uint32_t x = tile.xBegin; x < tile.xEnd; ++x) {
            if (cancelled(cancel))
                return;
            const std::size_t index = static_cast<std::size_t>(y) * settings_.width + x;
            if (!activePixels_[index]) continue;
            Sampler sampler(settings_.seed, static_cast<std::uint32_t>(index),
                            image_.sampleCounts[index]);
            const float jx = sampler.next(), jy = sampler.next();
            pass[index] = trace(cameraRay(snapshot_.camera(), settings_, x, y, {jx, jy}), sampler, cancel);
          }
        }
    });
    if (!completed || cancelled(cancel))
        return false;
    for (std::size_t i = 0; i < pass.size(); ++i) {
        if (!activePixels_[i]) continue;
        const TraceResult &sample = pass[i];
        image_.sum[i] += sample.radiance;
        image_.directSum[i] += sample.direct;
        image_.indirectSum[i] += sample.indirect;
        if (sample.primaryHit) {
            image_.albedoSum[i] += sample.albedo;
            image_.normalSum[i] += sample.normal;
            image_.depthSum[i] += sample.depth;
            ++image_.primaryHitCount[i];
        }
        const float luminance = glm::dot(sample.radiance, glm::vec3(0.2126f, 0.7152f, 0.0722f));
        image_.luminanceSum[i] += luminance;
        image_.luminanceSquaredSum[i] += luminance * luminance;
        image_.statistics.bvhTraversal += sample.bvhTraversal;
        image_.statistics.pathRays += sample.pathRays;
        image_.statistics.shadowRays += sample.shadowRays;
        ++image_.statistics.cameraSamples;
        ++image_.sampleCounts[i];
    }
    image_.statistics.completedTiles += scheduledTileCount;
    image_.statistics.renderMilliseconds += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - passStart
    ).count();
    ++image_.completedSamples;
    updateAdaptiveMask();
    return true;
}
void ProgressiveRenderer::updateAdaptiveMask() {
    image_.statistics.activePixels = activePixelCount_;
    if (!settings_.adaptiveSampling
        || image_.completedSamples < settings_.adaptiveMinimumSamples
        || (image_.completedSamples - settings_.adaptiveMinimumSamples)
            % settings_.adaptiveCheckInterval != 0U) {
        return;
    }
    ++image_.statistics.adaptiveChecks;
    constexpr float confidence95 = 1.95996398454f;
    for (std::size_t index = 0U; index < activePixels_.size(); ++index) {
        if (!activePixels_[index]) continue;
        const std::uint32_t countValue = image_.sampleCounts[index];
        if (countValue < settings_.adaptiveMinimumSamples || countValue < 2U) continue;
        const float count = static_cast<float>(countValue);
        const float mean = image_.luminanceSum[index] / count;
        const float centered = image_.luminanceSquaredSum[index]
            - image_.luminanceSum[index] * image_.luminanceSum[index] / count;
        const float variance = std::max(centered / static_cast<float>(countValue - 1U), 0.0f);
        const float confidenceInterval = confidence95 * std::sqrt(variance / count);
        const float threshold = std::max(settings_.adaptiveAbsoluteError,
            settings_.adaptiveRelativeError * std::abs(mean));
        if (std::isfinite(confidenceInterval) && confidenceInterval <= threshold) {
            activePixels_[index] = 0U;
            --activePixelCount_;
        }
    }
    image_.statistics.activePixels = activePixelCount_;
    image_.statistics.convergedPixels = activePixels_.size() - activePixelCount_;
    activeTileIndices_.clear();
    for (std::size_t tileIndex = 0U; tileIndex < tiles_.size(); ++tileIndex) {
        const RenderTile tile = tiles_[tileIndex];
        bool active = false;
        for (std::uint32_t y = tile.yBegin; y < tile.yEnd && !active; ++y) {
            for (std::uint32_t x = tile.xBegin; x < tile.xEnd; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * settings_.width + x;
                if (activePixels_[index]) {
                    active = true;
                    break;
                }
            }
        }
        if (active) activeTileIndices_.push_back(tileIndex);
    }
}
RenderTask::~RenderTask() {
    cancel();
    wait();
}
void RenderTask::cancel() {
    cancel_.store(true);
}
void RenderTask::wait() {
    if (worker_.joinable())
        worker_.join();
}
RenderProgress RenderTask::progress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return progress_;
}
void RenderTask::start(SceneSnapshot snapshot, RenderSettings settings) {
    cancel();
    wait();
    cancel_.store(false);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_ = {};
        progress_.status = RenderStatus::Running;
    }
    try {
        worker_ = std::thread([this, snapshot = std::move(snapshot), settings]() mutable {
            try {
                ProgressiveRenderer renderer(std::move(snapshot), settings);
                auto lastPublication = std::chrono::steady_clock::now();
                while (renderer.renderPass(&cancel_)) {
                    const auto now = std::chrono::steady_clock::now();
                    if (settings.progressPublishMilliseconds == 0U
                        || now - lastPublication >= std::chrono::milliseconds(
                            settings.progressPublishMilliseconds
                        )) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        progress_.image = renderer.image();
                        lastPublication = now;
                    }
                }
                std::lock_guard<std::mutex> lock(mutex_);
                progress_.image = renderer.image();
                progress_.status = renderer.complete()
                                       ? RenderStatus::Completed
                                       : RenderStatus::Cancelled;
            } catch (const std::exception &e) {
                std::lock_guard<std::mutex> lock(mutex_);
                progress_.error = e.what();
                progress_.status = RenderStatus::Failed;
            }
        });
    } catch (const std::exception &e) {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_.error = e.what();
        progress_.status = RenderStatus::Failed;
    }
}
} // namespace pathtracer
