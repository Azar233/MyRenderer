#include "pathtracer/ProgressiveRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

namespace {

pathtracer::RenderImage render(const pathtracer::SceneSnapshot& scene,
                               pathtracer::RenderSettings settings) {
    pathtracer::ProgressiveRenderer renderer(scene, settings);
    while (renderer.renderPass()) {}
    return renderer.image();
}

double relativeRmse(const std::vector<glm::vec3>& reference,
                    const std::vector<glm::vec3>& candidate) {
    double error = 0.0;
    double energy = 0.0;
    for (std::size_t index = 0U; index < reference.size(); ++index) {
        const glm::vec3 difference = reference[index] - candidate[index];
        error += glm::dot(difference, difference);
        energy += glm::dot(reference[index], reference[index]);
    }
    return std::sqrt(error / std::max(energy, 1.0e-20));
}

} // namespace

int main() {
    try {
        const auto scene = pathtracer::makePbrAcceptanceScene();
        pathtracer::RenderSettings uniformSettings{96U, 96U, 128U, 6U, 20260915U};
        uniformSettings.workerCount = 0U;
        const pathtracer::RenderImage uniform = render(scene, uniformSettings);

        pathtracer::RenderSettings adaptiveSettings = uniformSettings;
        adaptiveSettings.adaptiveSampling = true;
        adaptiveSettings.adaptiveMinimumSamples = 16U;
        adaptiveSettings.adaptiveCheckInterval = 8U;
        adaptiveSettings.adaptiveRelativeError = .15f;
        adaptiveSettings.adaptiveAbsoluteError = .01f;
        const pathtracer::RenderImage adaptive = render(scene, adaptiveSettings);

        std::vector<std::uint32_t> counts = adaptive.sampleCounts;
        std::sort(counts.begin(), counts.end());
        const double meanSamples = std::accumulate(counts.begin(), counts.end(), 0.0)
            / static_cast<double>(counts.size());
        const auto percentile = [&](double fraction) {
            const std::size_t index = std::min(
                static_cast<std::size_t>(fraction * static_cast<double>(counts.size() - 1U)),
                counts.size() - 1U);
            return counts[index];
        };
        const double rmse = relativeRmse(uniform.linearPixels(), adaptive.linearPixels());
        const double sampleSaving = 1.0 - static_cast<double>(adaptive.statistics.cameraSamples)
            / static_cast<double>(uniform.statistics.cameraSamples);
        const double raySaving = 1.0 - static_cast<double>(adaptive.statistics.pathRays)
            / static_cast<double>(uniform.statistics.pathRays);
        const double timeSaving = 1.0 - adaptive.statistics.renderMilliseconds
            / uniform.statistics.renderMilliseconds;

        std::cout << std::fixed << std::setprecision(4)
                  << "Uniform: " << uniform.statistics.cameraSamples << " camera samples, "
                  << uniform.statistics.pathRays << " path rays, "
                  << uniform.statistics.renderMilliseconds << " ms\n"
                  << "Adaptive: " << adaptive.statistics.cameraSamples << " camera samples, "
                  << adaptive.statistics.pathRays << " path rays, "
                  << adaptive.statistics.renderMilliseconds << " ms\n"
                  << "SPP min/mean/P50/P95/max: " << counts.front() << " / " << meanSamples
                  << " / " << percentile(.5) << " / " << percentile(.95) << " / "
                  << counts.back() << "\n"
                  << "Savings samples/rays/time: " << sampleSaving * 100.0 << "% / "
                  << raySaving * 100.0 << "% / " << timeSaving * 100.0 << "%\n"
                  << "Beauty relative RMSE vs uniform 128 SPP: " << rmse << '\n';
        return sampleSaving > 0.0 && rmse <= .08 ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
