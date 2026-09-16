#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include "pathtracer/Bvh.h"
#include "pathtracer/InstancedBvh.h"
#include "pathtracer/LightSampling.h"
#include "pathtracer/SceneSnapshot.h"
#include "pathtracer/TileScheduler.h"
#include "pathtracer/TextureSampling.h"
namespace pathtracer {
enum class AccelerationStructure {
    Automatic,
    WorldBvh,
    TwoLevelBvh
};
struct RenderSettings {
    std::uint32_t width{256}, height{256}, samplesPerPixel{64}, maxDepth{6}, seed{1};
    // Zero disables Russian Roulette. Otherwise survival tests start after this many reflections.
    std::uint32_t russianRouletteDepth{3};
    bool nextEventEstimation{true};
    // Zero selects available hardware concurrency, capped by the tile count.
    std::uint32_t workerCount{0U};
    std::uint32_t tileSize{16U};
    // Zero publishes after every SPP pass; final/cancelled states always publish.
    std::uint32_t progressPublishMilliseconds{100U};
    BvhSplitStrategy bvhSplitStrategy{BvhSplitStrategy::BinnedSah};
    AccelerationStructure accelerationStructure{AccelerationStructure::Automatic};
    bool adaptiveSampling{false};
    std::uint32_t adaptiveMinimumSamples{16U};
    std::uint32_t adaptiveCheckInterval{8U};
    // Stop a pixel when its 95% luminance confidence interval is below the
    // larger of these relative/absolute thresholds.
    float adaptiveRelativeError{0.05f};
    float adaptiveAbsoluteError{0.001f};
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
struct RenderStatistics {
    AccelerationStructure accelerationStructure{AccelerationStructure::WorldBvh};
    BvhBuildStats bvhBuild;
    InstancedBvhBuildStats instancedBvhBuild;
    std::size_t estimatedAccelerationBytes{0U};
    BvhTraversalStats bvhTraversal;
    std::uint64_t pathRays{0U};
    std::uint64_t shadowRays{0U};
    std::uint64_t cameraSamples{0U};
    std::uint64_t completedTiles{0U};
    std::uint64_t adaptiveChecks{0U};
    std::size_t activePixels{0U};
    std::size_t convergedPixels{0U};
    std::size_t workerCount{1U};
    std::uint32_t tileSize{16U};
    double renderMilliseconds{0.0};
};
struct RenderImage {
    std::uint32_t width{0}, height{0}, completedSamples{0};
    std::vector<glm::vec3> sum; // Linear RGB, top row first.
    // Primary-surface AOVs accumulate only samples that hit geometry.
    std::vector<glm::vec3> albedoSum;
    std::vector<glm::vec3> normalSum;
    std::vector<float> depthSum;
    std::vector<std::uint32_t> primaryHitCount;
    // Lighting AOVs and luminance moments accumulate every camera sample.
    std::vector<glm::vec3> directSum;
    std::vector<glm::vec3> indirectSum;
    std::vector<float> luminanceSum;
    std::vector<float> luminanceSquaredSum;
    // Per-pixel committed sample count. Uniform rendering keeps every entry
    // equal to completedSamples; adaptive rendering may stop entries early.
    std::vector<std::uint32_t> sampleCounts;
    RenderStatistics statistics;

    std::vector<glm::vec3> linearPixels() const;
    std::vector<glm::vec3> albedoPixels() const;
    std::vector<glm::vec3> normalPixels() const;
    std::vector<float> depthPixels() const;
    std::vector<glm::vec3> directPixels() const;
    std::vector<glm::vec3> indirectPixels() const;
    std::vector<float> sampleCountPixels() const;
    std::vector<float> variancePixels() const;
};
class ProgressiveRenderer {
  public:
    ProgressiveRenderer(SceneSnapshot snapshot, RenderSettings settings);
    // Commit only complete passes; false means cancelled or target reached.
    bool renderPass(const std::atomic<bool> *cancel = nullptr);
    bool complete() const;
    const RenderImage &image() const { return image_; }

  private:
    struct TraceResult {
        glm::vec3 radiance{0.0f};
        glm::vec3 albedo{0.0f};
        glm::vec3 normal{0.0f};
        float depth{0.0f};
        glm::vec3 direct{0.0f};
        glm::vec3 indirect{0.0f};
        BvhTraversalStats bvhTraversal;
        std::uint64_t pathRays{0U};
        std::uint64_t shadowRays{0U};
        bool primaryHit{false};
    };
    TraceResult trace(Ray, Sampler &, const std::atomic<bool> *) const;
    bool intersectScene(const Ray&, SurfaceInteraction&, BvhTraversalStats*) const;
    bool sceneOccluded(const Ray&, BvhTraversalStats*) const;
    void updateAdaptiveMask();
    SceneSnapshot snapshot_;
    RenderSettings settings_;
    std::vector<Triangle> triangles_;
    std::unique_ptr<Bvh> bvh_;
    std::unique_ptr<InstancedBvh> instancedBvh_;
    SceneLights lights_;
    SceneTextures textures_;
    std::vector<RenderTile> tiles_;
    std::vector<std::size_t> activeTileIndices_;
    TileThreadPool tilePool_;
    std::vector<std::uint8_t> activePixels_;
    std::size_t activePixelCount_{0U};
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
void writeReferenceAovs(const RenderImage &, const std::filesystem::path &stem);
SceneSnapshot makeDiffuseAcceptanceScene();
SceneSnapshot makePbrAcceptanceScene();
SceneSnapshot makeInstancingStressScene(std::uint32_t gridSize = 20U);
} // namespace pathtracer
