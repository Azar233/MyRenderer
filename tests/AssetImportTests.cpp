#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "io/AssimpImporter.h"
#include "io/ModelImporter.h"
#include "io/ObjLoader.h"

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path asset(const char* name) {
    return std::filesystem::path(MYRENDERER_SOURCE_DIR) / "assets" / "models" / name;
}

bool hasScope(const ModelImportResult& result, ModelDiagnosticScope scope) {
    for (const ModelDiagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.scope == scope) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    try {
        ObjLoader obj;
        AssimpImporter assimp;

        const ModelImportResult cube = obj.load(asset("cube.obj"));
        require(cube.model.meshes.size() == 1U, "cube.obj should import one mesh");
        require(!cube.model.meshes.front().indices.empty(), "cube.obj should contain indices");

        const ModelImportResult materials = obj.load(asset("material_regression.obj"));
        require(materials.model.materials.size() == 3U, "material regression should import three materials");
        require(materials.model.textures.size() == 3U, "material regression should import three texture references");

        const ModelImportResult degenerateUv = obj.load(asset("degenerate_uv.obj"));
        require(hasScope(degenerateUv, ModelDiagnosticScope::Mesh), "degenerate UV should produce a mesh diagnostic");

        const ModelImportResult gltf = assimp.load(asset("textured_triangle.gltf"));
        require(!gltf.model.meshes.empty(), "glTF fixture should import a mesh");
        require(!gltf.model.textures.empty(), "glTF fixture should import its embedded texture");

        const ModelImportResult pbr = assimp.load(asset("pbr_material_test.gltf"));
        require(pbr.model.materials.size() >= 5U, "PBR fixture should import its five materials");
        require(pbr.model.meshes.size() == 5U, "PBR fixture should import five transformed meshes");
        bool hasPackedPbrTexture = false;
        bool hasRoughMaterial = false;
        bool hasMetalMaterial = false;
        for (const MaterialData& material : pbr.model.materials) {
            hasPackedPbrTexture |= material.metallicRoughnessTextureIndex >= 0;
            hasRoughMaterial |= material.roughnessFactor > 0.85f;
            hasMetalMaterial |= material.metallicFactor > 0.95f;
        }
        require(hasPackedPbrTexture, "PBR fixture should import the packed metallic-roughness texture");
        require(hasRoughMaterial, "PBR roughness factor should survive glTF import");
        require(hasMetalMaterial, "PBR metallic factor should survive glTF import");

        const ModelImportResult dae = assimp.load(asset("textured_quad.dae"));
        require(!dae.model.meshes.empty(), "DAE fixture should import a mesh");
        require(!dae.model.textures.empty(), "DAE fixture should import its external texture reference");

        bool missingFileRejected = false;
        try {
            obj.load(asset("does_not_exist.obj"));
        } catch (const std::exception&) {
            missingFileRejected = true;
        }
        require(missingFileRejected, "missing model files must be rejected");

        std::cout << "Asset import tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Asset import test failed: " << error.what() << '\n';
        return 1;
    }
}
