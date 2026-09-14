#pragma once
#include <atomic>
#include <mutex>
#include <thread>
#include "pathtracer/Bvh.h"
#include "pathtracer/SceneSnapshot.h"
namespace pathtracer {
struct RenderSettings {
    std::uint32_t width{256}, height{256}, samplesPerPixel{64}, maxDepth{6}, seed{1};
    // Zero disables Russian Roulette. Otherwise survival tests start after this many reflections.
    std::uint32_t russianRouletteDepth{3};
};
class Sampler {
  public:
    Sampler(std::uint32_t seed, std::uint32_t pixel, std::uint32_t sample);
    float next();

  private:
    std::uint32_t state_;
};
Ray cameraRay(const SnapshotCamera &, const RenderSettings &, std::uint32_t x, std::uint32_t y,
              glm::vec2 jitter);
struct RenderImage {
    std::uint32_t width{0}, height{0}, completedSamples{0};
    std::vector<glm::vec3> sum; // Linear RGB, top row first.
    std::vector<glm::vec3> linearPixels() const;
};
class ProgressiveRenderer {
  public:
    ProgressiveRenderer(SceneSnapshot snapshot, RenderSettings settings);
    // Commit only complete passes; false means cancelled or target reached.
    bool renderPass(const std::atomic<bool> *cancel = nullptr);
    const RenderImage &image() const { return image_; }

  private:
    glm::vec3 trace(Ray, Sampler &, const std::atomic<bool> *) const;
    SceneSnapshot snapshot_;
    RenderSettings settings_;
    Bvh bvh_;
    RenderImage image_;
};
enum class RenderStatus { Idle, Running, Completed, Cancelled, Failed };
struct RenderProgress {
    RenderStatus status{RenderStatus::Idle};
    RenderImage image;
    std::string error;
};
// One worker. Lifecycle calls are owner-thread operations; progress() is thread safe.
// Starting again cancels/joins the old job and clears its accumulation.
class RenderTask {
  public:
    ~RenderTask();
    void start(SceneSnapshot snapshot, RenderSettings settings);
    void cancel();
    void wait();
    RenderProgress progress() const;

  private:
    std::atomic<bool> cancel_{false};
    mutable std::mutex mutex_;
    RenderProgress progress_;
    std::thread worker_;
};
void writeReferenceImage(const RenderImage &, const std::filesystem::path &stem);
SceneSnapshot makeDiffuseAcceptanceScene();
SceneSnapshot makePbrAcceptanceScene();
} // namespace pathtracer
