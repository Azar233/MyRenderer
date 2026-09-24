#include "app/AssetThumbnail.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void write(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << contents;
    require(static_cast<bool>(output), "fixture write failed");
}

std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
    const std::filesystem::path root = std::filesystem::temp_directory_path()
        / "MyRendererAssetThumbnailTests";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    try {
        const std::filesystem::path assets = root / "assets";
        const std::filesystem::path model = assets / "models" / "cube.obj";
        const std::filesystem::path scene = assets / "scenes" / "example.myscene";
        const std::filesystem::path job = assets / "renderjobs" / "example.renderjob";
        std::filesystem::create_directories(model.parent_path());
        std::filesystem::copy_file(std::filesystem::path(MYRENDERER_SOURCE_DIR)
            / "assets/models/cube.obj", model, std::filesystem::copy_options::overwrite_existing, error);
        require(!error, "model fixture copy failed");
        write(scene, R"({"format":"MyRendererScene","version":1,
            "entities":[{"id":1,"name":"Cube","model":"../models/cube.obj",
            "tint":[0.2,0.6,1.0]}]})");
        write(job, R"({"format":"MyRendererRenderJob","schemaVersion":1,
            "scene":"../scenes/example.myscene","renderer":"cpu-path-traced",
            "camera":"scene","resolution":[64,64],
            "frames":{"start":0,"end":0,"fps":24},
            "sampling":{"spp":1,"maxDepth":2,"seed":1},
            "aovs":["beauty"],"output":{"path":"output/frame_{frame:04}",
            "formats":["png"],"resume":false}})");

        WorkspaceAssetCatalog catalog;
        std::string catalogError;
        require(catalog.refresh(root, catalogError), catalogError.c_str());
        const auto verify = [&](const std::filesystem::path& path) {
            const WorkspaceAssetRecord* asset = catalog.find(path);
            require(asset != nullptr && isPreviewableAsset(asset->category),
                    "previewable asset was not cataloged");
            const std::uint64_t key = assetThumbnailContentKey(*asset);
            const AssetThumbnail image = loadOrGenerateAssetThumbnail(*asset, key, root / ".cache");
            require(image.error.empty() && image.rgba.size() == 128U * 80U * 4U,
                    "asset raster generation failed");
            bool geometry = false;
            for (std::size_t i = 0; i < image.rgba.size(); i += 4U) {
                if (image.rgba[i] > 90U || image.rgba[i + 1U] > 100U
                    || image.rgba[i + 2U] > 120U) { geometry = true; break; }
            }
            require(geometry, "thumbnail contains only its background");
            const AssetThumbnail cached = loadOrGenerateAssetThumbnail(*asset, key, root / ".cache");
            require(cached.rgba == image.rgba, "cached raster differs from generation");
            return key;
        };
        const std::uint64_t modelKey = verify(model);
        const std::uint64_t sceneKey = verify(scene);
        const std::uint64_t jobKey = verify(job);

        write(model, read(model) + "\n# revised geometry source\n");
        require(catalog.refresh(root, catalogError), catalogError.c_str());
        require(verify(model) != modelKey, "model edit did not invalidate its thumbnail");
        require(verify(scene) != sceneKey, "model dependency did not invalidate scene thumbnail");
        require(verify(job) != jobKey, "model dependency did not invalidate job thumbnail");
        const std::uint64_t revisedSceneKey = assetThumbnailContentKey(*catalog.find(scene));
        const std::uint64_t dependentJobKey = assetThumbnailContentKey(*catalog.find(job));
        write(scene, read(scene) + "\n ");
        require(catalog.refresh(root, catalogError), catalogError.c_str());
        require(verify(scene) != revisedSceneKey, "scene edit did not invalidate its thumbnail");
        require(verify(job) != dependentJobKey, "scene edit did not invalidate job thumbnail");
        const std::uint64_t revisedJobKey = assetThumbnailContentKey(*catalog.find(job));
        write(job, read(job) + "\n ");
        require(catalog.refresh(root, catalogError), catalogError.c_str());
        require(verify(job) != revisedJobKey, "job edit did not invalidate its thumbnail");

        std::filesystem::remove_all(root, error);
        std::cout << "Raster thumbnails and source/dependency refresh passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::filesystem::remove_all(root, error);
        std::cerr << "Asset thumbnail test failed: " << exception.what() << '\n';
        return 1;
    }
}
