#include "io/AssimpImporter.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/matrix3x3.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/geometric.hpp>

namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

glm::vec3 toVec3(const aiVector3D& value) {
    return {value.x, value.y, value.z};
}

glm::mat4 toMat4(const aiMatrix4x4& value) {
    return {
        value.a1, value.b1, value.c1, value.d1,
        value.a2, value.b2, value.c2, value.d2,
        value.a3, value.b3, value.c3, value.d3,
        value.a4, value.b4, value.c4, value.d4
    };
}

std::filesystem::path resolveTextureReference(
    const std::filesystem::path& sourceDirectory,
    const aiString& reference
) {
    const std::string value = reference.C_Str();
    if (value.empty()) {
        return {};
    }
    if (value.front() == '*') {
        // Assimp uses *N for a texture embedded in the source asset.
        return std::filesystem::path(value);
    }
    return (sourceDirectory / std::filesystem::path(value)).lexically_normal();
}

std::filesystem::path materialTexture(
    const aiMaterial& material,
    aiTextureType preferredType,
    aiTextureType fallbackType,
    const std::filesystem::path& sourceDirectory
) {
    aiString reference;
    if (material.GetTexture(preferredType, 0, &reference) == AI_SUCCESS
        || material.GetTexture(fallbackType, 0, &reference) == AI_SUCCESS) {
        return resolveTextureReference(sourceDirectory, reference);
    }
    return {};
}

struct ImportContext {
    const aiScene& scene;
    ModelData& model;
    std::string& warnings;

    std::optional<std::uint32_t> appendMesh(
        const aiMesh& source,
        const aiMatrix4x4& globalTransform,
        const std::string& nodeName
    ) {
        if (!source.HasPositions()) {
            warnings += "Skipped mesh without positions: " + nodeName + "\n";
            return std::nullopt;
        }

        MeshData mesh;
        mesh.name = source.mName.length > 0U ? source.mName.C_Str() : nodeName;
        mesh.vertices.reserve(source.mNumVertices);

        aiMatrix3x3 normalMatrix(globalTransform);
        normalMatrix.Inverse().Transpose();
        const bool mirrored = aiMatrix3x3(globalTransform).Determinant() < 0.0f;

        for (unsigned int vertexIndex = 0; vertexIndex < source.mNumVertices; ++vertexIndex) {
            Vertex vertex;
            const aiVector3D transformedPosition = globalTransform * source.mVertices[vertexIndex];
            vertex.position = toVec3(transformedPosition);

            if (source.HasNormals()) {
                const aiVector3D transformedNormal = normalMatrix * source.mNormals[vertexIndex];
                if (transformedNormal.SquareLength() > 1e-16f) {
                    vertex.normal = glm::normalize(toVec3(transformedNormal));
                }
            }
            if (source.HasTextureCoords(0)) {
                vertex.texCoord0 = {
                    source.mTextureCoords[0][vertexIndex].x,
                    source.mTextureCoords[0][vertexIndex].y
                };
            }
            if (source.HasTangentsAndBitangents()) {
                const aiVector3D transformedTangent = normalMatrix * source.mTangents[vertexIndex];
                const aiVector3D transformedBitangent = normalMatrix * source.mBitangents[vertexIndex];
                if (transformedTangent.SquareLength() > 1e-16f
                    && transformedBitangent.SquareLength() > 1e-16f) {
                    const glm::vec3 tangent = glm::normalize(toVec3(transformedTangent));
                    const glm::vec3 bitangent = glm::normalize(toVec3(transformedBitangent));
                    const float handedness = glm::dot(glm::cross(vertex.normal, tangent), bitangent) < 0.0f
                        ? -1.0f
                        : 1.0f;
                    vertex.tangent = glm::vec4(tangent, handedness);
                }
            }

            mesh.boundsMin = glm::min(mesh.boundsMin, vertex.position);
            mesh.boundsMax = glm::max(mesh.boundsMax, vertex.position);
            model.boundsMin = glm::min(model.boundsMin, vertex.position);
            model.boundsMax = glm::max(model.boundsMax, vertex.position);
            mesh.vertices.push_back(vertex);
        }

        for (unsigned int faceIndex = 0; faceIndex < source.mNumFaces; ++faceIndex) {
            const aiFace& face = source.mFaces[faceIndex];
            if (face.mNumIndices != 3U) {
                continue;
            }
            mesh.indices.push_back(face.mIndices[0]);
            mesh.indices.push_back(face.mIndices[mirrored ? 2U : 1U]);
            mesh.indices.push_back(face.mIndices[mirrored ? 1U : 2U]);
        }
        if (mesh.indices.empty()) {
            warnings += "Skipped mesh without triangle faces: " + mesh.name + "\n";
            return std::nullopt;
        }

        mesh.submeshes.push_back(SubmeshData{
            mesh.name,
            0U,
            static_cast<std::uint32_t>(mesh.indices.size()),
            source.mMaterialIndex < model.materials.size()
                ? static_cast<std::int32_t>(source.mMaterialIndex)
                : -1
        });
        const auto modelMeshIndex = static_cast<std::uint32_t>(model.meshes.size());
        model.meshes.push_back(std::move(mesh));
        return modelMeshIndex;
    }

    ModelNodeData appendNode(const aiNode& source, const aiMatrix4x4& parentTransform) {
        ModelNodeData node;
        node.name = source.mName.length > 0U ? source.mName.C_Str() : "Node";
        node.localTransform = toMat4(source.mTransformation);
        const aiMatrix4x4 globalTransform = parentTransform * source.mTransformation;

        for (unsigned int meshIndex = 0; meshIndex < source.mNumMeshes; ++meshIndex) {
            const unsigned int sceneMeshIndex = source.mMeshes[meshIndex];
            if (sceneMeshIndex >= scene.mNumMeshes) {
                warnings += "Skipped invalid mesh reference in node: " + node.name + "\n";
                continue;
            }
            const auto modelMeshIndex = appendMesh(*scene.mMeshes[sceneMeshIndex], globalTransform, node.name);
            if (modelMeshIndex.has_value()) {
                node.meshIndices.push_back(*modelMeshIndex);
            }
        }

        node.children.reserve(source.mNumChildren);
        for (unsigned int childIndex = 0; childIndex < source.mNumChildren; ++childIndex) {
            node.children.push_back(appendNode(*source.mChildren[childIndex], globalTransform));
        }
        return node;
    }
};

} // namespace

bool AssimpImporter::supports(const std::filesystem::path& path) const {
    const std::string extension = lowercase(path.extension().string());
    return extension == ".dae" || extension == ".gltf" || extension == ".glb";
}

ModelImportResult AssimpImporter::load(const std::filesystem::path& path) const {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("Model file does not exist: " + path.string());
    }

    Assimp::Importer importer;
    constexpr unsigned int flags =
        aiProcess_CalcTangentSpace
        | aiProcess_JoinIdenticalVertices
        | aiProcess_Triangulate
        | aiProcess_GenSmoothNormals
        | aiProcess_ImproveCacheLocality
        | aiProcess_SortByPType
        | aiProcess_ValidateDataStructure;
    const aiScene* scene = importer.ReadFile(path.string(), flags);
    if (scene == nullptr || scene->mRootNode == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0U) {
        throw std::runtime_error("Failed to import model with Assimp: " + std::string(importer.GetErrorString()));
    }

    ModelImportResult result;
    result.model.name = path.stem().string();
    result.model.sourcePath = std::filesystem::absolute(path).lexically_normal();
    const std::filesystem::path sourceDirectory = result.model.sourcePath.parent_path();

    result.model.materials.reserve(scene->mNumMaterials);
    for (unsigned int materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
        const aiMaterial& source = *scene->mMaterials[materialIndex];
        MaterialData material;
        aiString name;
        if (source.Get(AI_MATKEY_NAME, name) == AI_SUCCESS && name.length > 0U) {
            material.name = name.C_Str();
        }
        aiColor4D baseColor;
        if (aiGetMaterialColor(&source, AI_MATKEY_BASE_COLOR, &baseColor) == AI_SUCCESS
            || aiGetMaterialColor(&source, AI_MATKEY_COLOR_DIFFUSE, &baseColor) == AI_SUCCESS) {
            material.baseColorFactor = {baseColor.r, baseColor.g, baseColor.b, baseColor.a};
        }
        material.baseColorTexture = materialTexture(
            source,
            aiTextureType_BASE_COLOR,
            aiTextureType_DIFFUSE,
            sourceDirectory
        );
        material.normalTexture = materialTexture(
            source,
            aiTextureType_NORMALS,
            aiTextureType_HEIGHT,
            sourceDirectory
        );
        result.model.materials.push_back(std::move(material));
    }

    ImportContext context{*scene, result.model, result.warnings};
    result.model.rootNode = context.appendNode(*scene->mRootNode, aiMatrix4x4{});
    if (result.model.meshes.empty()) {
        throw std::runtime_error("Imported model does not contain triangle meshes");
    }
    return result;
}
