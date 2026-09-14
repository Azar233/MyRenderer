#include "pathtracer/ProgressiveRenderer.h"
#include "pathtracer/PbrBsdf.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
using namespace pathtracer;
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(glm::vec3 a, glm::vec3 b, float tolerance = 1e-5f) {
    require(glm::length(a - b) < tolerance, "RGB/vector mismatch");
}
glm::vec3 average(const RenderImage &image) {
    glm::vec3 value(0.0f);
    for (const auto &pixel : image.linearPixels())
        value += pixel;
    return value / static_cast<float>(image.sum.size());
}
SceneSnapshot plane(glm::vec3 albedo, glm::vec3 emission, glm::vec3 environment, bool reverse = false,
                    glm::vec3 tint = glm::vec3(1)) {
    auto model = std::make_shared<ModelData>();
    MaterialData m;
    m.baseColorFactor = glm::vec4(albedo, 1);
    m.emissiveFactor = emission;
    model->materials.push_back(m);
    MeshData mesh;
    for (auto p : {glm::vec3(-100, -100, -1), glm::vec3(100, -100, -1), glm::vec3(0, 100, -1)}) {
        Vertex v;
        v.position = p;
        mesh.vertices.push_back(v);
    }
    mesh.indices = reverse ? std::vector<std::uint32_t>{0, 2, 1} : std::vector<std::uint32_t>{0, 1, 2};
    mesh.submeshes.push_back({"plane", 0, 3, 0});
    model->meshes.push_back(mesh);
    model->rootNode.meshIndices = {0};
    SceneSnapshotLighting lighting;
    lighting.environment.backgroundColor = environment;
    SceneSnapshotBuilder builder(SnapshotCamera{}, lighting);
    builder.addModel(model, 1, "plane", glm::mat4(1), tint);
    return builder.finish();
}
RenderImage render(SceneSnapshot scene, RenderSettings settings) {
    ProgressiveRenderer r(std::move(scene), settings);
    while (r.renderPass()) {
    }
    return r.image();
}
void cameraAndSampling() {
    SnapshotCamera c;
    c.aspectRatio = 2;
    c.verticalFieldOfViewRadians = glm::radians(90.0f);
    RenderSettings s{1, 1, 1, 1, 7};
    near(cameraRay(c, s, 0, 0, {.5f, .5f}).direction, {0, 0, -1});
    near(cameraRay(c, s, 0, 0, {0, 0}).direction, glm::normalize(glm::vec3(-2, 1, -1)));
    c.position = {2, 3, 4};
    c.view = glm::lookAt(c.position, c.position + glm::vec3(1, 0, 0), glm::vec3(0, 1, 0));
    auto ray = cameraRay(c, s, 0, 0, {.5f, .5f});
    near(ray.origin, c.position);
    near(ray.direction, {1, 0, 0});
    Sampler a(7, 12, 9), b(7, 12, 9), different(7, 12, 10);
    bool differs = false;
    for (int i = 0; i < 1000; ++i) {
        float x = a.next();
        require(x >= 0 && x < 1 && x == b.next(), "RNG repeat/range");
        differs |= x != different.next();
    }
    require(differs, "Sample streams must differ");
    s.width = 0;
    bool rejected = false;
    try {
        cameraRay(c, s, 0, 0, {.5f, .5f});
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    require(rejected, "Invalid settings accepted");
}
void energyAndDepth() {
    const glm::vec3 furnace = average(render(
        plane({1, 1, 1}, {0, 0, 0}, {1, 1, 1}, false, {.5f, .5f, .5f}), {4, 4, 2048, 2, 1, 0}));
    std::cout << "Tinted dielectric furnace: " << furnace.x << ", " << furnace.y << ", " << furnace.z << '\n';
    require(glm::all(glm::greaterThan(furnace, glm::vec3(0.15f))) &&
                glm::all(glm::lessThan(furnace, glm::vec3(0.19f))),
            "Dielectric white-furnace energy out of range");
    const auto scene = plane({.2f, .4f, .8f}, {2, 1, .5f}, {3, 2, 1});
    for (auto p : render(scene, {3, 3, 8, 1, 3}).linearPixels())
        near(p, {2, 1, .5f});
    const glm::vec3 reflected = average(render(scene, {4, 4, 2048, 2, 3, 0}));
    require(glm::all(glm::greaterThan(reflected, glm::vec3(2, 1, .5f))),
            "PBR reflection did not add environment radiance");
    for (auto p : render(plane({0, 0, 0}, {2, 1, .5f}, {0, 0, 0}, true), {2, 2, 1, 3, 1}).linearPixels())
        near(p, {0, 0, 0});
    for (auto p : render(plane({1, 1, 1}, {0, 0, 0}, {0, 0, 0}), {2, 2, 4, 8, 1}).linearPixels())
        near(p, {0, 0, 0});
}
void metallicRoughnessBsdf() {
    const glm::vec3 n(0, 0, 1), v(0, 0, 1), l(0, 0, 1);
    const auto smooth = evaluatePbrBsdf({{.8f, .8f, .8f}, 0, .15f}, n, v, l);
    const auto rough = evaluatePbrBsdf({{.8f, .8f, .8f}, 0, .8f}, n, v, l);
    require(smooth.x > rough.x, "Roughness did not broaden the GGX peak");
    const auto metal = evaluatePbrBsdf({{.9f, .08f, .02f}, 1, .3f}, n, v, l);
    require(metal.x > metal.y * 5 && metal.y > metal.z, "Metallic F0 did not inherit base color");
    require(glm::all(glm::equal(evaluatePbrBsdf({{1, 1, 1}, 0, .5f}, n, v, -l), glm::vec3(0))),
            "BSDF evaluated below the surface");

    Sampler random(20260914, 0, 0);
    glm::vec3 reflected(0.0f);
    constexpr int sampleCount = 100000;
    for (int i = 0; i < sampleCount; ++i) {
        const auto sample = samplePbrBsdf({{.8f, .6f, .3f}, .35f, .45f}, n, v, random.next(),
                                          {random.next(), random.next()});
        if (sample.valid) {
            require(sample.pdf > 0 && glm::all(glm::greaterThanEqual(sample.weight, glm::vec3(0))),
                    "Invalid PBR sample weight");
            reflected += sample.weight;
        }
    }
    reflected /= static_cast<float>(sampleCount);
    require(glm::all(glm::greaterThan(reflected, glm::vec3(.1f))) &&
                glm::all(glm::lessThan(reflected, glm::vec3(1.05f))),
            "PBR hemispherical reflectance is nonphysical");
}
void accumulationAndTasks() {
    auto scene = makeDiffuseAcceptanceScene();
    RenderSettings s{12, 12, 8, 5, 42};
    const auto expected = render(scene, s);
    auto noRoulette = s;
    noRoulette.russianRouletteDepth = 0;
    require(render(scene, noRoulette).sum != expected.sum, "Russian Roulette path was not exercised");
    ProgressiveRenderer r(scene, s);
    require(r.renderPass(), "First pass");
    auto saved = r.image();
    std::atomic<bool> stop{true};
    require(!r.renderPass(&stop), "Cancelled pass committed");
    require(saved.sum == r.image().sum, "Cancelled image changed");
    stop = false;
    while (r.renderPass(&stop)) {
    }
    require(r.image().sum == expected.sum, "Resume differs");
    require(render(scene, s).sum == expected.sum, "Render not deterministic");
    RenderTask task;
    task.start(scene, s);
    task.wait();
    auto progress = task.progress();
    require(progress.status == RenderStatus::Completed && progress.image.sum == expected.sum,
            "Worker differs");
    auto longSettings = s;
    longSettings.samplesPerPixel = 1000000;
    task.start(scene, longSettings);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (task.progress().image.completedSamples == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    require(task.progress().image.completedSamples > 0, "No progressive publication");
    task.cancel();
    task.wait();
    progress = task.progress();
    require(progress.status == RenderStatus::Cancelled, "Cancellation status");
    auto prefix = s;
    prefix.samplesPerPixel = progress.image.completedSamples;
    require(progress.image.sum == render(scene, prefix).sum, "Cancelled prefix contains partial pass");
    task.start(scene, longSettings);
    task.start(scene, s);
    task.wait();
    require(task.progress().image.sum == expected.sum, "Restart did not reset");
    s.maxDepth = 0;
    task.start(scene, s);
    task.wait();
    require(task.progress().status == RenderStatus::Failed && !task.progress().error.empty(),
            "Worker error not reported");
    {
        RenderTask scoped;
        scoped.start(scene, longSettings);
    } // Destructor must cancel/join.
}
void output() {
    RenderImage image;
    image.width = 2;
    image.height = 2;
    image.completedSamples = 1;
    image.sum = {{4, 0, 0}, {0, 1, 0}, {0, 0, .25f}, {0, 0, 0}};
    const auto directory = std::filesystem::temp_directory_path() / "myrenderer-progressive-test";
    const auto stem = directory / "colors";
    writeReferenceImage(image, stem);
    int w = 0, h = 0, c = 0;
    auto *hdr = stbi_loadf((stem.string() + ".hdr").c_str(), &w, &h, &c, 3);
    require(hdr && w == 2 && h == 2, "HDR decode failed");
    for (int i = 0; i < 4; ++i)
        near({hdr[i * 3], hdr[i * 3 + 1], hdr[i * 3 + 2]}, image.sum[i]);
    stbi_image_free(hdr);
    auto *png = stbi_load((stem.string() + ".png").c_str(), &w, &h, &c, 3);
    require(png && w == 2 && h == 2, "PNG decode failed");
    require(png[0] == 231 && png[1] == 0 && png[4] == 188 && png[8] == 124 && png[9] == 0,
            "Tone map / sRGB / orientation mismatch");
    stbi_image_free(png);
    require(image.sum[0].x == 4, "Export mutated HDR");
    std::filesystem::remove(stem.string() + ".hdr");
    std::filesystem::remove(stem.string() + ".png");
}
void convergence() {
    auto scene = makeDiffuseAcceptanceScene();
    RenderSettings s{12, 12, 512, 5, 42};
    const auto reference = render(scene, s).linearPixels();
    s.samplesPerPixel = 1;
    const auto low = render(scene, s).linearPixels();
    s.samplesPerPixel = 64;
    const auto high = render(scene, s).linearPixels();
    double lowError = 0, highError = 0;
    for (std::size_t i = 0; i < reference.size(); ++i) {
        auto a = low[i] - reference[i], b = high[i] - reference[i];
        lowError += glm::dot(a, a);
        highError += glm::dot(b, b);
    }
    std::cout << "MSE 1/64 SPP vs 512 SPP: " << lowError / (reference.size() * 3) << " / "
              << highError / (reference.size() * 3) << '\n';
    require(highError < lowError, "Fixed-scene error did not decrease");
}
} // namespace
void compareAcceptance(const std::string &baseline, const std::string &current) {
    for (const std::string extension : {".hdr", ".png"}) {
        int w = 0, h = 0, c = 0, rw = 0, rh = 0, rc = 0;
        float *a = stbi_loadf((baseline + extension).c_str(), &w, &h, &c, 3);
        float *b = stbi_loadf((current + extension).c_str(), &rw, &rh, &rc, 3);
        require(a && b && w == rw && h == rh, "Acceptance decode/dimensions failed");
        double error = 0, energy = 0;
        for (int i = 0; i < w * h * 3; ++i) {
            require(std::isfinite(a[i]) && std::isfinite(b[i]), "Nonfinite acceptance pixels");
            const double delta = a[i] - b[i];
            error += delta * delta;
            energy += a[i] * a[i];
        }
        stbi_image_free(a);
        stbi_image_free(b);
        const double relativeRmse = std::sqrt(error / std::max(energy, 1e-12));
        std::cout << extension << " relative RMSE: " << relativeRmse << '\n';
        require(relativeRmse <= 0.002, "Acceptance differs from committed baseline");
    }
}
int main(int argc, char **argv) {
    try {
        if (argc == 3) {
            compareAcceptance(argv[1], argv[2]);
            return 0;
        }
        require(argc == 1, "Usage: MyRendererProgressiveTests [baseline-stem current-stem]");
        cameraAndSampling();
        energyAndDepth();
        metallicRoughnessBsdf();
        accumulationAndTasks();
        output();
        convergence();
        std::cout << "Progressive CPU tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
