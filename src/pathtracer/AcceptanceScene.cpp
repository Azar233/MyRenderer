#include "pathtracer/ProgressiveRenderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
namespace pathtracer {
namespace {
SceneSnapshot makeAcceptanceScene(bool pbrMaterials) {
    auto model = std::make_shared<ModelData>();
    std::int32_t boxBaseColorTexture = -1;
    std::int32_t boxMetallicRoughnessTexture = -1;
    std::int32_t boxNormalTexture = -1;
    if (pbrMaterials) {
        TextureData baseColor;
        baseColor.name = "SR-P1E checker base color";
        baseColor.cacheKey = "acceptance://base-color";
        baseColor.width = 4;
        baseColor.height = 4;
        baseColor.srgb = true;
        for (std::uint32_t y = 0; y < baseColor.height; ++y) {
            for (std::uint32_t x = 0; x < baseColor.width; ++x) {
                const bool bright = ((x + y) & 1U) == 0U;
                const std::array<std::uint8_t, 4> texel = bright
                    ? std::array<std::uint8_t, 4>{255, 242, 214, 255}
                    : std::array<std::uint8_t, 4>{92, 178, 255, 255};
                baseColor.rgbaPixels.insert(baseColor.rgbaPixels.end(), texel.begin(), texel.end());
            }
        }
        boxBaseColorTexture = static_cast<std::int32_t>(model->textures.size());
        model->textures.push_back(std::move(baseColor));

        TextureData metallicRoughness;
        metallicRoughness.name = "SR-P1E packed metallic roughness";
        metallicRoughness.cacheKey = "acceptance://metallic-roughness";
        metallicRoughness.width = 4;
        metallicRoughness.height = 4;
        for (std::uint32_t y = 0; y < metallicRoughness.height; ++y) {
            for (std::uint32_t x = 0; x < metallicRoughness.width; ++x) {
                const bool polishedMetal = ((x + y) & 1U) == 0U;
                const std::array<std::uint8_t, 4> texel = polishedMetal
                    ? std::array<std::uint8_t, 4>{0, 72, 255, 255}
                    : std::array<std::uint8_t, 4>{0, 224, 48, 255};
                metallicRoughness.rgbaPixels.insert(
                    metallicRoughness.rgbaPixels.end(), texel.begin(), texel.end()
                );
            }
        }
        boxMetallicRoughnessTexture = static_cast<std::int32_t>(model->textures.size());
        model->textures.push_back(std::move(metallicRoughness));

        TextureData normal;
        normal.name = "SR-P1E tangent-space normal";
        normal.cacheKey = "acceptance://normal";
        normal.width = 4;
        normal.height = 4;
        for (std::uint32_t y = 0; y < normal.height; ++y) {
            for (std::uint32_t x = 0; x < normal.width; ++x) {
                const std::array<std::uint8_t, 4> texel = ((x + y) & 1U) == 0U
                    ? std::array<std::uint8_t, 4>{174, 128, 246, 255}
                    : std::array<std::uint8_t, 4>{82, 128, 246, 255};
                normal.rgbaPixels.insert(normal.rgbaPixels.end(), texel.begin(), texel.end());
            }
        }
        boxNormalTexture = static_cast<std::int32_t>(model->textures.size());
        model->textures.push_back(std::move(normal));
    }
    auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 color,
                    glm::vec3 emission = glm::vec3(0), float metallic = 0.0f, float roughness = 1.0f,
                    std::int32_t baseColorTexture = -1, std::int32_t metallicRoughnessTexture = -1,
                    std::int32_t normalTexture = -1) {
        MaterialData material;
        material.baseColorFactor = glm::vec4(color, 1);
        material.emissiveFactor = emission;
        material.metallicFactor = metallic;
        material.roughnessFactor = roughness;
        material.baseColorTextureIndex = baseColorTexture;
        material.metallicRoughnessTextureIndex = metallicRoughnessTexture;
        material.normalTextureIndex = normalTexture;
        model->materials.push_back(material);
        MeshData mesh;
        const std::array<glm::vec3, 4> positions{a, b, c, d};
        const std::array<glm::vec2, 4> texCoords{
            glm::vec2(0, 0), glm::vec2(2, 0), glm::vec2(2, 2), glm::vec2(0, 2)
        };
        const glm::vec3 surfaceNormal = glm::normalize(glm::cross(b - a, c - a));
        const glm::vec3 surfaceTangent = glm::normalize(b - a);
        for (std::size_t vertexIndex = 0; vertexIndex < positions.size(); ++vertexIndex) {
            Vertex v;
            v.position = positions[vertexIndex];
            v.normal = surfaceNormal;
            v.texCoord0 = texCoords[vertexIndex];
            v.tangent = glm::vec4(surfaceTangent, 1.0f);
            mesh.vertices.push_back(v);
        }
        mesh.indices = {0, 1, 2, 0, 2, 3};
        mesh.submeshes.push_back({"Quad", 0, 6, static_cast<std::int32_t>(model->materials.size() - 1)});
        model->rootNode.meshIndices.push_back(static_cast<std::uint32_t>(model->meshes.size()));
        model->meshes.push_back(std::move(mesh));
    };
    // Original open-front Cornell-style room; large ceiling emitter keeps pure BSDF sampling readable.
    quad({-1, 0, 1}, {1, 0, 1}, {1, 0, -1}, {-1, 0, -1}, {.72f, .72f, .72f});
    quad({-1, 0, -1}, {1, 0, -1}, {1, 2, -1}, {-1, 2, -1}, {.72f, .72f, .72f});
    quad({-1, 0, 1}, {-1, 0, -1}, {-1, 2, -1}, {-1, 2, 1}, {.72f, .08f, .05f});
    quad({1, 0, -1}, {1, 0, 1}, {1, 2, 1}, {1, 2, -1}, {.08f, .55f, .12f});
    quad({-1, 2, -1}, {1, 2, -1}, {1, 2, 1}, {-1, 2, 1}, {.72f, .72f, .72f});
    quad({-.7f, 1.99f, -.7f}, {.7f, 1.99f, -.7f}, {.7f, 1.99f, .7f}, {-.7f, 1.99f, .7f}, {0, 0, 0},
         {5, 5, 5});
    const float l = -.65f, r = .15f, b = -.45f, f = .4f, h = .7f;
    const glm::vec3 boxColor = pbrMaterials ? glm::vec3(.85f, .32f, .08f) : glm::vec3(.65f);
    const float boxMetallic = pbrMaterials ? 1.0f : 0.0f;
    const float boxRoughness = pbrMaterials ? 0.32f : 1.0f;
    quad({l, 0, f}, {r, 0, f}, {r, h, f}, {l, h, f}, boxColor, {}, boxMetallic, boxRoughness,
         boxBaseColorTexture, boxMetallicRoughnessTexture, boxNormalTexture);
    quad({r, 0, f}, {r, 0, b}, {r, h, b}, {r, h, f}, boxColor, {}, boxMetallic, boxRoughness,
         boxBaseColorTexture, boxMetallicRoughnessTexture, boxNormalTexture);
    quad({l, 0, b}, {l, 0, f}, {l, h, f}, {l, h, b}, boxColor, {}, boxMetallic, boxRoughness,
         boxBaseColorTexture, boxMetallicRoughnessTexture, boxNormalTexture);
    quad({r, 0, b}, {l, 0, b}, {l, h, b}, {r, h, b}, boxColor, {}, boxMetallic, boxRoughness,
         boxBaseColorTexture, boxMetallicRoughnessTexture, boxNormalTexture);
    quad({l, h, f}, {r, h, f}, {r, h, b}, {l, h, b}, boxColor, {}, boxMetallic, boxRoughness,
         boxBaseColorTexture, boxMetallicRoughnessTexture, boxNormalTexture);
    if (pbrMaterials) {
        MaterialData glass;
        glass.name = "SR-P1G blue volume glass";
        glass.baseColorFactor = glm::vec4(1.0f);
        glass.roughnessFactor = .06f;
        glass.transmissionFactor = 1.0f;
        glass.indexOfRefraction = 1.52f;
        glass.thicknessFactor = 1.0f;
        glass.attenuationColor = {.28f, .68f, 1.0f};
        glass.attenuationDistance = .72f;
        const auto materialIndex = static_cast<std::int32_t>(model->materials.size());
        model->materials.push_back(glass);

        const float glassLeft = .28f;
        const float glassRight = .82f;
        const float glassBottom = .02f;
        const float glassTop = .88f;
        const float glassFront = .32f;
        const float glassBack = -.32f;
        const std::array<std::array<glm::vec3, 4>, 6> faces{{
            {{{glassLeft, glassBottom, glassFront}, {glassRight, glassBottom, glassFront},
              {glassRight, glassTop, glassFront}, {glassLeft, glassTop, glassFront}}},
            {{{glassRight, glassBottom, glassFront}, {glassRight, glassBottom, glassBack},
              {glassRight, glassTop, glassBack}, {glassRight, glassTop, glassFront}}},
            {{{glassLeft, glassBottom, glassBack}, {glassLeft, glassBottom, glassFront},
              {glassLeft, glassTop, glassFront}, {glassLeft, glassTop, glassBack}}},
            {{{glassRight, glassBottom, glassBack}, {glassLeft, glassBottom, glassBack},
              {glassLeft, glassTop, glassBack}, {glassRight, glassTop, glassBack}}},
            {{{glassLeft, glassTop, glassFront}, {glassRight, glassTop, glassFront},
              {glassRight, glassTop, glassBack}, {glassLeft, glassTop, glassBack}}},
            {{{glassLeft, glassBottom, glassBack}, {glassRight, glassBottom, glassBack},
              {glassRight, glassBottom, glassFront}, {glassLeft, glassBottom, glassFront}}}
        }};
        MeshData glassMesh;
        glassMesh.name = "Closed volume glass box";
        for (const auto& face : faces) {
            const std::uint32_t firstVertex = static_cast<std::uint32_t>(glassMesh.vertices.size());
            const glm::vec3 normal = glm::normalize(glm::cross(
                face[1] - face[0], face[2] - face[0]
            ));
            for (const glm::vec3& position : face) {
                Vertex vertex;
                vertex.position = position;
                vertex.normal = normal;
                glassMesh.vertices.push_back(vertex);
            }
            glassMesh.indices.insert(glassMesh.indices.end(), {
                firstVertex, firstVertex + 1U, firstVertex + 2U,
                firstVertex, firstVertex + 2U, firstVertex + 3U
            });
        }
        glassMesh.submeshes.push_back({
            "Volume glass", 0U, static_cast<std::uint32_t>(glassMesh.indices.size()), materialIndex
        });
        model->rootNode.meshIndices.push_back(static_cast<std::uint32_t>(model->meshes.size()));
        model->meshes.push_back(std::move(glassMesh));
    }
    SnapshotCamera camera;
    camera.position = {0, 1, 3.4f};
    camera.aspectRatio = 1;
    camera.verticalFieldOfViewRadians = glm::radians(43.0f);
    camera.view = glm::lookAt(camera.position, glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));
    SceneSnapshotLighting lighting;
    if (pbrMaterials) {
        auto& environment = lighting.environment;
        environment.sourceName = "SR-P1F synthetic high-dynamic-range environment";
        environment.intensity = .45f;
        environment.width = 32;
        environment.height = 16;
        environment.radiancePixels.resize(
            static_cast<std::size_t>(environment.width) * environment.height
        );
        for (std::uint32_t y = 0; y < environment.height; ++y) {
            const float vertical = static_cast<float>(y) / static_cast<float>(environment.height - 1U);
            for (std::uint32_t x = 0; x < environment.width; ++x) {
                glm::vec3 color = vertical < .55f
                    ? glm::vec3(.025f, .055f, .14f) * (1.0f - vertical)
                        + glm::vec3(.18f, .24f, .38f) * vertical
                    : glm::vec3(.035f, .026f, .02f);
                const float dx = static_cast<float>(x) - 24.0f;
                const float dy = static_cast<float>(y) - 7.5f;
                const float sun = std::exp(-(dx * dx / 3.0f + dy * dy / 1.4f));
                color += glm::vec3(20.0f, 15.0f, 9.0f) * sun;
                environment.radiancePixels[static_cast<std::size_t>(y) * environment.width + x]
                    = color;
            }
        }
    }
    SceneSnapshotBuilder builder(camera, lighting);
    builder.addModel(model, 1, "Diffuse acceptance", glm::mat4(1));
    return builder.finish();
}
} // namespace
SceneSnapshot makeDiffuseAcceptanceScene() {
    return makeAcceptanceScene(false);
}
SceneSnapshot makePbrAcceptanceScene() {
    return makeAcceptanceScene(true);
}
SceneSnapshot makeInstancingStressScene(std::uint32_t gridSize) {
    gridSize = std::max(1U, gridSize);
    auto cube = std::make_shared<ModelData>();
    cube->name = "SR-P1K shared cube";
    MaterialData cubeMaterial;
    cubeMaterial.name = "Shared instance material";
    cubeMaterial.baseColorFactor = glm::vec4(.7f, .72f, .76f, 1.0f);
    cubeMaterial.roughnessFactor = .48f;
    cube->materials.push_back(cubeMaterial);
    MeshData cubeMesh;
    cubeMesh.name = "Shared 12-triangle cube";
    const std::array<std::array<glm::vec3, 4>, 6> faces{{
        {{{-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}}},
        {{{.5f,-.5f,-.5f},{-.5f,-.5f,-.5f},{-.5f,.5f,-.5f},{.5f,.5f,-.5f}}},
        {{{-.5f,-.5f,-.5f},{-.5f,-.5f,.5f},{-.5f,.5f,.5f},{-.5f,.5f,-.5f}}},
        {{{.5f,-.5f,.5f},{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{.5f,.5f,.5f}}},
        {{{-.5f,.5f,.5f},{.5f,.5f,.5f},{.5f,.5f,-.5f},{-.5f,.5f,-.5f}}},
        {{{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,-.5f,.5f},{-.5f,-.5f,.5f}}}
    }};
    for (const auto& face : faces) {
        const std::uint32_t base = static_cast<std::uint32_t>(cubeMesh.vertices.size());
        const glm::vec3 normal = glm::normalize(glm::cross(face[1] - face[0], face[2] - face[0]));
        const glm::vec3 tangent = glm::normalize(face[1] - face[0]);
        for (std::size_t corner = 0; corner < 4U; ++corner) {
            Vertex vertex;
            vertex.position = face[corner];
            vertex.normal = normal;
            vertex.tangent = glm::vec4(tangent, 1.0f);
            vertex.texCoord0 = glm::vec2(corner == 1U || corner == 2U,
                                        corner >= 2U);
            cubeMesh.vertices.push_back(vertex);
        }
        cubeMesh.indices.insert(cubeMesh.indices.end(),
                                {base, base + 1U, base + 2U, base, base + 2U, base + 3U});
    }
    cubeMesh.submeshes.push_back({"Cube", 0U,
                                  static_cast<std::uint32_t>(cubeMesh.indices.size()), 0});
    cube->meshes.push_back(std::move(cubeMesh));
    cube->rootNode.meshIndices = {0U};

    auto ground = std::make_shared<ModelData>();
    MaterialData groundMaterial;
    groundMaterial.name = "Stress scene ground";
    groundMaterial.baseColorFactor = glm::vec4(.22f, .25f, .3f, 1.0f);
    groundMaterial.roughnessFactor = .82f;
    ground->materials.push_back(groundMaterial);
    MeshData groundMesh;
    const float extent = static_cast<float>(gridSize) * .55f;
    for (const glm::vec3 position : {glm::vec3(-extent, 0, extent), glm::vec3(extent, 0, extent),
                                     glm::vec3(extent, 0, -extent), glm::vec3(-extent, 0, -extent)}) {
        Vertex vertex;
        vertex.position = position;
        vertex.normal = {0, 1, 0};
        vertex.tangent = {1, 0, 0, 1};
        groundMesh.vertices.push_back(vertex);
    }
    groundMesh.indices = {0, 1, 2, 0, 2, 3};
    groundMesh.submeshes.push_back({"Ground", 0, 6, 0});
    ground->meshes.push_back(std::move(groundMesh));
    ground->rootNode.meshIndices = {0U};

    SnapshotCamera camera;
    camera.aspectRatio = 1.5f;
    camera.verticalFieldOfViewRadians = glm::radians(46.0f);
    camera.position = {0.0f, static_cast<float>(gridSize) * .55f,
                       static_cast<float>(gridSize) * .9f};
    camera.view = glm::lookAt(camera.position, glm::vec3(0, .4f, 0), glm::vec3(0, 1, 0));
    SceneSnapshotLighting lighting;
    lighting.environment.backgroundColor = {.025f, .035f, .055f};
    lighting.environment.intensity = 1.0f;
    lighting.directional.direction = glm::normalize(glm::vec3(-.5f, -1.0f, -.35f));
    lighting.directional.radiance = {3.2f, 3.0f, 2.7f};
    SceneSnapshotBuilder builder(camera, lighting);
    builder.addModel(ground, 1U, "Ground", glm::mat4(1.0f));
    std::uint64_t entityId = 2U;
    const float center = .5f * static_cast<float>(gridSize - 1U);
    for (std::uint32_t z = 0; z < gridSize; ++z) {
        for (std::uint32_t x = 0; x < gridSize; ++x) {
            const float wave = .18f * std::sin(static_cast<float>(x) * .7f)
                * std::cos(static_cast<float>(z) * .53f);
            glm::mat4 transform = glm::translate(glm::mat4(1.0f),
                glm::vec3((static_cast<float>(x) - center) * 1.05f, .38f + wave,
                          (static_cast<float>(z) - center) * 1.05f));
            transform = glm::rotate(transform, .17f * static_cast<float>((x + z) % 7U),
                                    glm::vec3(0, 1, 0));
            const float height = .55f + .08f * static_cast<float>((x * 3U + z * 5U) % 6U);
            transform = glm::scale(transform, glm::vec3(.62f, height, .62f));
            const glm::vec3 tint{
                .55f + .35f * static_cast<float>(x % 5U) / 4.0f,
                .55f + .35f * static_cast<float>(z % 5U) / 4.0f,
                .7f + .2f * static_cast<float>((x + z) % 4U) / 3.0f
            };
            builder.addModel(cube, entityId++, "Shared cube", transform, tint);
        }
    }
    return builder.finish();
}
} // namespace pathtracer
