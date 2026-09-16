#include "pathtracer/ProgressiveRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

namespace {

std::uint32_t number(const char* text) {
    const std::string value(text);
    std::size_t end = 0U;
    const auto parsed = std::stoull(value, &end);
    if (value.empty() || value.front() == '-' || end != value.size()
        || parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("Invalid unsigned argument");
    }
    return static_cast<std::uint32_t>(parsed);
}

pathtracer::RenderImage render(const pathtracer::SceneSnapshot& scene,
                               pathtracer::RenderSettings settings) {
    pathtracer::ProgressiveRenderer renderer(scene, settings);
    while (renderer.renderPass()) {}
    return renderer.image();
}

void printProfile(const char* label, const pathtracer::RenderImage& image) {
    const auto& stats = image.statistics;
    std::cout << label << ": build " << stats.bvhBuild.buildMilliseconds << " ms, render "
              << stats.renderMilliseconds << " ms, " << stats.bvhBuild.nodeCount << " nodes, "
              << stats.bvhTraversal.boundsTests << " bounds / "
              << stats.bvhTraversal.instanceTests << " instances / "
              << stats.bvhTraversal.triangleTests << " triangles, estimated "
              << stats.estimatedAccelerationBytes << " bytes\n";
    if (stats.accelerationStructure == pathtracer::AccelerationStructure::TwoLevelBvh) {
        const auto& twoLevel = stats.instancedBvhBuild;
        std::cout << "  BLAS/TLAS: " << twoLevel.blasCount << " / "
                  << twoLevel.tlasNodeCount << ", unique/expanded triangles "
                  << twoLevel.uniquePrimitiveCount << " / " << twoLevel.expandedPrimitiveCount
                  << ", estimated " << twoLevel.estimatedBytes << " bytes\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 1 && argc != 6) {
            std::cerr << "Usage: MyRendererInstancingBenchmark [grid width height spp workers]\n";
            return 1;
        }
        std::uint32_t grid = 20U;
        pathtracer::RenderSettings settings{96U, 64U, 16U, 4U, 20260915U};
        settings.workerCount = 0U;
        if (argc == 6) {
            grid = number(argv[1]);
            settings.width = number(argv[2]);
            settings.height = number(argv[3]);
            settings.samplesPerPixel = number(argv[4]);
            settings.workerCount = number(argv[5]);
        }
        const auto scene = pathtracer::makeInstancingStressScene(grid);
        settings.accelerationStructure = pathtracer::AccelerationStructure::WorldBvh;
        const auto world = render(scene, settings);
        settings.accelerationStructure = pathtracer::AccelerationStructure::TwoLevelBvh;
        const auto twoLevel = render(scene, settings);
        printProfile("World BVH", world);
        printProfile("BLAS/TLAS", twoLevel);

        double squaredError = 0.0;
        double referenceEnergy = 0.0;
        float maximumError = 0.0f;
        for (std::size_t i = 0; i < world.sum.size(); ++i) {
            const glm::vec3 delta = world.sum[i] - twoLevel.sum[i];
            squaredError += glm::dot(delta, delta);
            referenceEnergy += glm::dot(world.sum[i], world.sum[i]);
            maximumError = std::max(maximumError,
                std::max({std::abs(delta.x), std::abs(delta.y), std::abs(delta.z)}));
        }
        const double relativeRmse = std::sqrt(squaredError / std::max(referenceEnergy, 1.0e-20));
        std::cout << "Beauty relative RMSE / max abs: " << relativeRmse << " / "
                  << maximumError << '\n';
        return relativeRmse <= 1.0e-4 ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
