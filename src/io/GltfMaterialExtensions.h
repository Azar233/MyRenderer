#pragma once

#include <filesystem>
#include <vector>

// Assimp 6.0 does not expose KHR_materials_dispersion yet. This narrow adapter
// reads only that extension while Assimp remains responsible for the asset.
std::vector<float> loadGltfMaterialDispersion(const std::filesystem::path& path);
