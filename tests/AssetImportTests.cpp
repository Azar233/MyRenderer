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

const MaterialData* findMaterial(const ModelImportResult& result, const std::string& name) {
    for (const MaterialData& material : result.model.materials) {
        if (material.name == name) {
            return &material;
        }
    }
    return nullptr;
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

        const ModelImportResult alpha = assimp.load(asset("alpha_material_test.gltf"));
        require(alpha.model.meshes.size() == 4U, "alpha fixture should import four meshes");
        const MaterialData* opaque = findMaterial(alpha, "OpaqueAlphaIgnored");
        const MaterialData* masked = findMaterial(alpha, "MaskedVisible");
        const MaterialData* blended = findMaterial(alpha, "BlendedFar");
        const MaterialData* blendedNear = findMaterial(alpha, "BlendedNear");
        require(opaque != nullptr, "alpha fixture should preserve its opaque material");
        require(masked != nullptr, "alpha fixture should preserve its masked material");
        require(blended != nullptr, "alpha fixture should preserve its blended material");
        require(blendedNear != nullptr, "alpha fixture should preserve its second blended material");
        require(opaque->alphaMode == MaterialAlphaMode::Opaque, "OPAQUE mode should survive glTF import");
        require(masked->alphaMode == MaterialAlphaMode::Mask, "MASK mode should survive glTF import");
        require(masked->alphaCutoff == 0.5f, "MASK cutoff should survive glTF import");
        require(blended->alphaMode == MaterialAlphaMode::Blend, "BLEND mode should survive glTF import");
        require(blended->doubleSided, "doubleSided should survive glTF import");
        require(blended->baseColorFactor.a < 0.5f, "BLEND alpha should survive glTF import");
        require(blendedNear->alphaMode == MaterialAlphaMode::Blend, "second BLEND mode should survive import");

        const ModelImportResult glass = assimp.load(asset("glass_material_test.gltf"));
        const MaterialData* dielectricGlass = findMaterial(glass, "DielectricGlass");
        require(dielectricGlass != nullptr, "glass fixture should preserve its material");
        require(
            dielectricGlass->alphaMode == MaterialAlphaMode::Opaque,
            "optical transmission should remain independent from alpha coverage"
        );
        require(
            dielectricGlass->transmissionFactor > 0.9f,
            "KHR_materials_transmission factor should survive import"
        );
        require(
            dielectricGlass->indexOfRefraction > 1.5f
                && dielectricGlass->indexOfRefraction < 1.53f,
            "KHR_materials_ior should survive import"
        );
        const MaterialData* roughGlass = findMaterial(glass, "RoughDielectricGlass");
        require(roughGlass != nullptr, "glass fixture should preserve its rough material");
        require(roughGlass->roughnessFactor > 0.5f, "rough glass should preserve roughness");
        require(roughGlass->transmissionFactor > 0.8f, "rough glass should preserve transmission");

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
