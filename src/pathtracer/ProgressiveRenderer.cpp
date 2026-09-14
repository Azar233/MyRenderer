#include "pathtracer/ProgressiveRenderer.h"
#include "pathtracer/PbrBsdf.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <glm/gtc/matrix_inverse.hpp>
namespace pathtracer {
namespace {
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
glm::vec3 linearTint(glm::vec3 tint) {
    for (int c = 0; c < 3; ++c) {
        const float value = std::clamp(tint[c], 0.0f, 1.0f);
        tint[c] = value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
    }
    return tint;
}
void validate(const SnapshotCamera &c, const RenderSettings &s) {
    if (!s.width || !s.height || s.width > 8192 || s.height > 8192 || !s.samplesPerPixel || !s.maxDepth ||
        s.maxDepth > 1024)
        throw std::invalid_argument("Invalid dimensions, SPP or Max Depth");
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
    for (auto &p : pixels)
        p = completedSamples ? p / static_cast<float>(completedSamples) : glm::vec3(0);
    return pixels;
}
ProgressiveRenderer::ProgressiveRenderer(SceneSnapshot snapshot, RenderSettings settings)
    : snapshot_(std::move(snapshot)), settings_(settings) {
    validate(snapshot_.camera(), settings_);
    bvh_.rebuild(buildWorldTriangles(snapshot_));
    image_.width = settings.width;
    image_.height = settings.height;
    image_.sum.resize(static_cast<std::size_t>(settings.width) * settings.height, glm::vec3(0));
}
glm::vec3 ProgressiveRenderer::trace(Ray ray, Sampler &sampler, const std::atomic<bool> *cancel) const {
    glm::vec3 radiance(0), throughput(1);
    for (std::uint32_t depth = 0; depth < settings_.maxDepth; ++depth) {
        if (cancelled(cancel))
            return glm::vec3(0);
        SurfaceInteraction hit;
        if (!bvh_.intersect(ray, hit)) {
            const auto &env = snapshot_.lighting().environment;
            radiance +=
                throughput * glm::max(env.backgroundColor, glm::vec3(0)) * std::max(env.intensity, 0.0f);
            break;
        }
        MaterialData material;
        const auto &materials = snapshot_.assets().at(hit.assetIndex).model->materials;
        if (hit.materialIndex >= 0 && static_cast<std::size_t>(hit.materialIndex) < materials.size())
            material = materials[hit.materialIndex];
        if (hit.frontFace || material.doubleSided)
            radiance += throughput * glm::max(material.emissiveFactor, glm::vec3(0));
        const PbrSurface surface{
            glm::clamp(glm::vec3(material.baseColorFactor) * linearTint(hit.tint), glm::vec3(0), glm::vec3(1)),
            material.metallicFactor,
            material.roughnessFactor
        };
        if (depth + 1 == settings_.maxDepth)
            break;
        const glm::vec3 n = glm::dot(hit.shadingNormal, -ray.direction) > 0.0f
                                ? hit.shadingNormal
                                : hit.geometricNormal;
        const BsdfSample bsdf = samplePbrBsdf(surface, n, -ray.direction, sampler.next(),
                                              {sampler.next(), sampler.next()});
        if (!bsdf.valid || glm::dot(hit.geometricNormal, bsdf.direction) <= 0.0f)
            break;
        throughput *= bsdf.weight;
        if (glm::dot(throughput, throughput) == 0.0f)
            break;
        if (settings_.russianRouletteDepth > 0 && depth + 1 >= settings_.russianRouletteDepth) {
            const float survival = std::clamp(std::max({throughput.x, throughput.y, throughput.z}), 0.05f, 0.95f);
            if (sampler.next() >= survival)
                break;
            throughput /= survival;
        }
        const float epsilon =
            1e-4f * std::max(1.0f, std::max({std::abs(hit.position.x), std::abs(hit.position.y),
                                             std::abs(hit.position.z)}));
        ray = Ray{hit.position + hit.geometricNormal * epsilon, bsdf.direction, epsilon * 0.5f};
    }
    if (!finite(radiance))
        throw std::runtime_error("Nonfinite radiance");
    return radiance;
}
bool ProgressiveRenderer::renderPass(const std::atomic<bool> *cancel) {
    if (image_.completedSamples >= settings_.samplesPerPixel || cancelled(cancel))
        return false;
    std::vector<glm::vec3> pass(image_.sum.size());
    for (std::uint32_t y = 0; y < settings_.height; ++y)
        for (std::uint32_t x = 0; x < settings_.width; ++x) {
            if (cancelled(cancel))
                return false;
            const auto index = y * settings_.width + x;
            Sampler sampler(settings_.seed, index, image_.completedSamples);
            const float jx = sampler.next(), jy = sampler.next();
            pass[index] = trace(cameraRay(snapshot_.camera(), settings_, x, y, {jx, jy}), sampler, cancel);
        }
    if (cancelled(cancel))
        return false;
    for (std::size_t i = 0; i < pass.size(); ++i)
        image_.sum[i] += pass[i];
    ++image_.completedSamples;
    return true;
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
                while (renderer.renderPass(&cancel_)) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    progress_.image = renderer.image();
                }
                std::lock_guard<std::mutex> lock(mutex_);
                progress_.image = renderer.image();
                progress_.status = renderer.image().completedSamples == settings.samplesPerPixel
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
