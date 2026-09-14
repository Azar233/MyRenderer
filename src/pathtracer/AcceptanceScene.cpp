#include "pathtracer/ProgressiveRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
namespace pathtracer {
namespace {
SceneSnapshot makeAcceptanceScene(bool pbrMaterials) {
    auto model = std::make_shared<ModelData>();
    auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 color,
                    glm::vec3 emission = glm::vec3(0), float metallic = 0.0f, float roughness = 1.0f) {
        MaterialData material;
        material.baseColorFactor = glm::vec4(color, 1);
        material.emissiveFactor = emission;
        material.metallicFactor = metallic;
        material.roughnessFactor = roughness;
        model->materials.push_back(material);
        MeshData mesh;
        for (auto p : {a, b, c, d}) {
            Vertex v;
            v.position = p;
            v.normal = glm::normalize(glm::cross(b - a, c - a));
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
    quad({l, 0, f}, {r, 0, f}, {r, h, f}, {l, h, f}, boxColor, {}, boxMetallic, boxRoughness);
    quad({r, 0, f}, {r, 0, b}, {r, h, b}, {r, h, f}, boxColor, {}, boxMetallic, boxRoughness);
    quad({l, 0, b}, {l, 0, f}, {l, h, f}, {l, h, b}, boxColor, {}, boxMetallic, boxRoughness);
    quad({r, 0, b}, {l, 0, b}, {l, h, b}, {r, h, b}, boxColor, {}, boxMetallic, boxRoughness);
    quad({l, h, f}, {r, h, f}, {r, h, b}, {l, h, b}, boxColor, {}, boxMetallic, boxRoughness);
    SnapshotCamera camera;
    camera.position = {0, 1, 3.4f};
    camera.aspectRatio = 1;
    camera.verticalFieldOfViewRadians = glm::radians(43.0f);
    camera.view = glm::lookAt(camera.position, glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));
    SceneSnapshotLighting lighting;
    if (pbrMaterials)
        lighting.environment.backgroundColor = {.18f, .22f, .32f};
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
} // namespace pathtracer
