#include "pathtracer/ProgressiveRenderer.h"
#include <chrono>
#include <csignal>
#include <iostream>
#include <limits>
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
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 2 && argc != 7) {
            std::cerr << "Usage: MyRendererReferenceRender output-stem [width height spp max-depth seed]\n";
            return 1;
        }
        pathtracer::RenderSettings settings{256, 256, 256, 6, 20260914};
        if (argc == 7)
            settings = {number(argv[2]), number(argv[3]), number(argv[4]), number(argv[5]), number(argv[6])};
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
                if (progress.image.completedSamples)
                    pathtracer::writeReferenceImage(progress.image, argv[1]);
                std::cout << "Completed SPP: " << progress.image.completedSamples << " / "
                          << settings.samplesPerPixel << "\n";
                return progress.status == pathtracer::RenderStatus::Completed ? 0 : 2;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
