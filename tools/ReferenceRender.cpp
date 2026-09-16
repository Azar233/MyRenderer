#include "pathtracer/ProgressiveRenderer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <iostream>
#include <limits>
#include <numeric>
namespace {
volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) {
    interrupted = 1;
}
std::uint32_t number(const char *text) {
    std::string s(text);
    std::size_t end = 0;
    const auto value = std::stoull(s, &end);
    if (s.empty() || s[0] == '-' || end != s.size() || value > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Invalid unsigned argument");
    return static_cast<std::uint32_t>(value);
}
float decimal(const char* text) {
    const std::string value(text);
    std::size_t end = 0U;
    const float parsed = std::stof(value, &end);
    if (value.empty() || end != value.size() || !std::isfinite(parsed) || parsed < 0.0f)
        throw std::invalid_argument("Invalid non-negative decimal argument");
    return parsed;
}
pathtracer::BvhSplitStrategy splitStrategy(const char* text) {
    const std::string value(text);
    if (value == "median") return pathtracer::BvhSplitStrategy::Median;
    if (value == "sah") return pathtracer::BvhSplitStrategy::BinnedSah;
    throw std::invalid_argument("BVH strategy must be 'median' or 'sah'");
}
pathtracer::AccelerationStructure accelerationStructure(const char* text) {
    const std::string value(text);
    if (value == "auto") return pathtracer::AccelerationStructure::Automatic;
    if (value == "world") return pathtracer::AccelerationStructure::WorldBvh;
    if (value == "tlas") return pathtracer::AccelerationStructure::TwoLevelBvh;
    throw std::invalid_argument("Acceleration structure must be 'auto', 'world', or 'tlas'");
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 2 && argc != 7 && argc != 9 && argc != 10 && argc != 11 && argc != 16) {
            std::cerr << "Usage: MyRendererReferenceRender output-stem "
                         "[width height spp max-depth seed [workers tile-size "
                         "[median|sah [auto|world|tlas "
                         "[adaptive min-spp interval relative-error absolute-error]]]]\n";
            return 1;
        }
        pathtracer::RenderSettings settings{256, 256, 256, 6, 20260914};
        if (argc >= 7)
            settings = {number(argv[2]), number(argv[3]), number(argv[4]), number(argv[5]), number(argv[6])};
        if (argc >= 9) {
            settings.workerCount = number(argv[7]);
            settings.tileSize = number(argv[8]);
        }
        if (argc >= 10)
            settings.bvhSplitStrategy = splitStrategy(argv[9]);
        if (argc >= 11)
            settings.accelerationStructure = accelerationStructure(argv[10]);
        if (argc == 16) {
            if (std::string(argv[11]) != "adaptive")
                throw std::invalid_argument("Expected 'adaptive' sampling mode");
            settings.adaptiveSampling = true;
            settings.adaptiveMinimumSamples = number(argv[12]);
            settings.adaptiveCheckInterval = number(argv[13]);
            settings.adaptiveRelativeError = decimal(argv[14]);
            settings.adaptiveAbsoluteError = decimal(argv[15]);
        }
        if (settings.width != settings.height)
            throw std::invalid_argument("Acceptance camera requires square output");
        std::signal(SIGINT, interrupt);
        pathtracer::RenderTask task;
        task.start(pathtracer::makePbrAcceptanceScene(), settings);
        for (;;) {
            if (interrupted)
                task.cancel();
            auto progress = task.progress();
            if (progress.status != pathtracer::RenderStatus::Running) {
                task.wait();
                if (progress.status == pathtracer::RenderStatus::Failed)
                    throw std::runtime_error(progress.error);
                if (progress.image.completedSamples) {
                    pathtracer::writeReferenceImage(progress.image, argv[1]);
                    pathtracer::writeReferenceAovs(progress.image, argv[1]);
                }
                std::cout << "Completed SPP: " << progress.image.completedSamples << " / "
                          << settings.samplesPerPixel << "\n";
                const auto& statistics = progress.image.statistics;
                const char* acceleration = statistics.accelerationStructure
                        == pathtracer::AccelerationStructure::TwoLevelBvh
                    ? "BLAS/TLAS" : "world BVH";
                std::cout << "Workers/Tiles: " << statistics.workerCount << " / "
                          << statistics.completedTiles << " (" << statistics.tileSize << "x"
                          << statistics.tileSize << ")\n"
                          << "Acceleration: " << acceleration << ", estimated "
                          << statistics.estimatedAccelerationBytes << " bytes\n"
                          << "BVH build: " << statistics.bvhBuild.buildMilliseconds << " ms, "
                          << statistics.bvhBuild.nodeCount << " nodes, "
                          << statistics.bvhBuild.maximumDepth << " levels, "
                          << statistics.bvhBuild.sahSplitCount << " SAH / "
                          << statistics.bvhBuild.medianSplitCount << " median splits\n"
                          << "Traversal: " << statistics.pathRays << " path rays, "
                          << statistics.shadowRays << " shadow rays, "
                          << statistics.bvhTraversal.boundsTests << " bounds tests, "
                          << statistics.bvhTraversal.instanceTests << " instance tests, "
                          << statistics.bvhTraversal.triangleTests << " triangle tests\n"
                          << "Sampling: " << statistics.cameraSamples << " camera samples, "
                          << statistics.convergedPixels << " converged / "
                          << statistics.activePixels << " active pixels, "
                          << statistics.adaptiveChecks << " adaptive checks\n"
                          << "Render: " << statistics.renderMilliseconds << " ms\n";
                if (!progress.image.sampleCounts.empty()) {
                    const auto [minimum, maximum] = std::minmax_element(
                        progress.image.sampleCounts.begin(), progress.image.sampleCounts.end());
                    const double mean = std::accumulate(progress.image.sampleCounts.begin(),
                        progress.image.sampleCounts.end(), 0.0)
                        / static_cast<double>(progress.image.sampleCounts.size());
                    std::cout << "SPP min/mean/max: " << *minimum << " / " << mean << " / "
                              << *maximum << "\n";
                }
                return progress.status == pathtracer::RenderStatus::Completed ? 0 : 2;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
