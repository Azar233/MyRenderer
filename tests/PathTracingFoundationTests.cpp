#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include "pathtracer/Bvh.h"
#include "pathtracer/SceneSnapshot.h"

namespace {

using pathtracer::Bounds3;
using pathtracer::Bvh;
using pathtracer::BvhSplitStrategy;
using pathtracer::BvhTraversalStats;
using pathtracer::Ray;
using pathtracer::SceneSnapshotBuilder;
using pathtracer::SnapshotCamera;
using pathtracer::SurfaceInteraction;
using pathtracer::Triangle;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void requireNear(float actual, float expected, const std::string& message) {
    if (std::abs(actual - expected) > 1.0e-4f) {
        throw std::runtime_error(message + ": expected " + std::to_string(expected)
            + ", got " + std::to_string(actual));
    }
}

Triangle makeTriangle(float z, std::uint32_t primitiveIndex) {
    Triangle triangle;
    triangle.positions[0] = glm::vec3(-1.0f, -1.0f, z);
    triangle.positions[1] = glm::vec3(1.0f, -1.0f, z);
    triangle.positions[2] = glm::vec3(0.0f, 1.0f, z);
    triangle.normals[0] = triangle.normals[1] = triangle.normals[2] = glm::vec3(0.0f, 0.0f, 1.0f);
    triangle.texCoords[0] = glm::vec2(0.0f, 0.0f);
    triangle.texCoords[1] = glm::vec2(1.0f, 0.0f);
    triangle.texCoords[2] = glm::vec2(0.5f, 1.0f);
    triangle.primitiveIndex = primitiveIndex;
    return triangle;
}

void testBoundsIntersection() {
    Bounds3 bounds;
    bounds.expand(glm::vec3(-1.0f));
    bounds.expand(glm::vec3(1.0f));
    float nearDistance = 0.0f;
    float farDistance = 0.0f;
    require(
        bounds.intersect(
            Ray{glm::vec3(0.0f, 0.0f, -3.0f), glm::vec3(0.0f, 0.0f, 1.0f)},
            &nearDistance,
            &farDistance
        ),
        "front-facing ray should intersect an AABB"
    );
    requireNear(nearDistance, 2.0f, "AABB near distance should be stable");
    requireNear(farDistance, 4.0f, "AABB far distance should be stable");
    require(
        !bounds.intersect(Ray{glm::vec3(2.0f, 0.0f, -3.0f), glm::vec3(0.0f, 0.0f, 1.0f)}),
        "parallel ray outside an AABB slab should miss"
    );
    require(
        !bounds.intersect(Ray{
            glm::vec3(0.0f, 0.0f, -3.0f),
            glm::vec3(0.0f, 0.0f, 1.0f),
            0.0f,
            1.5f
        }),
        "ray interval should clip an otherwise valid AABB hit"
    );
}

void testTriangleIntersection() {
    const Triangle triangle = makeTriangle(0.0f, 17U);
    SurfaceInteraction hit;
    require(
        pathtracer::intersectTriangle(
            Ray{glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f)},
            triangle,
            hit
        ),
        "ray should intersect the triangle"
    );
    requireNear(hit.t, 2.0f, "triangle distance should be correct");
    requireNear(hit.barycentrics.x, 0.25f, "first barycentric coordinate should be correct");
    requireNear(hit.barycentrics.y, 0.25f, "second barycentric coordinate should be correct");
    requireNear(hit.barycentrics.z, 0.5f, "third barycentric coordinate should be correct");
    requireNear(hit.texCoord.x, 0.5f, "interpolated texture U should be correct");
    requireNear(hit.texCoord.y, 0.5f, "interpolated texture V should be correct");
    require(hit.frontFace, "front-face classification should preserve medium transitions");
    require(hit.primitiveIndex == 17U, "surface metadata should identify the primitive");

    SurfaceInteraction backHit;
    require(
        pathtracer::intersectTriangle(
            Ray{glm::vec3(0.0f, 0.0f, -2.0f), glm::vec3(0.0f, 0.0f, 1.0f)},
            triangle,
            backHit
        ),
        "double-sided reference geometry should intersect from the back"
    );
    require(!backHit.frontFace, "back-face classification should be reported");
    require(glm::dot(backHit.geometricNormal, glm::vec3(0.0f, 0.0f, -1.0f)) > 0.99f,
        "interaction normal should face against the incoming ray");

    Triangle degenerate = triangle;
    degenerate.positions[2] = degenerate.positions[1];
    require(
        !pathtracer::intersectTriangle(
            Ray{glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f)},
            degenerate,
            hit
        ),
        "degenerate triangles must not produce invalid interactions"
    );
}

void testMedianBvh() {
    std::vector<Triangle> triangles;
    triangles.push_back(makeTriangle(8.0f, 80U));
    triangles.push_back(makeTriangle(2.0f, 20U));
    triangles.push_back(makeTriangle(6.0f, 60U));
    triangles.push_back(makeTriangle(4.0f, 40U));
    Bvh bvh(std::move(triangles), 1U);
    require(!bvh.empty(), "BVH should retain input primitives");
    require(bvh.stats().primitiveCount == 4U, "BVH statistics should count primitives");
    require(bvh.stats().leafCount == 4U, "leaf-size one should create one leaf per primitive");
    require(bvh.stats().nodeCount == 7U, "balanced four-primitive BVH should have seven nodes");

    SurfaceInteraction hit;
    require(
        bvh.intersect(Ray{glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f)}, hit),
        "BVH traversal should find a hit"
    );
    requireNear(hit.t, 2.0f, "BVH traversal should return the nearest hit");
    require(hit.primitiveIndex == 20U, "BVH reordering must preserve primitive identity");
    require(
        !bvh.occluded(Ray{
            glm::vec3(0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f),
            1.0e-4f,
            1.5f
        }),
        "BVH occlusion should respect the ray maximum distance"
    );
}

void testBinnedSah() {
    std::vector<Triangle> triangles;
    for (std::uint32_t index = 0U; index < 15U; ++index) {
        Triangle triangle = makeTriangle(1.0f, index);
        for (glm::vec3& position : triangle.positions)
            position += glm::vec3(static_cast<float>(index) * 0.25f, 0.0f, 0.0f);
        triangles.push_back(triangle);
    }
    Triangle outlier = makeTriangle(1.0f, 100U);
    for (glm::vec3& position : outlier.positions)
        position += glm::vec3(100.0f, 0.0f, 0.0f);
    triangles.push_back(outlier);

    Bvh median(triangles, 4U, BvhSplitStrategy::Median);
    Bvh sah(triangles, 4U, BvhSplitStrategy::BinnedSah);
    require(sah.stats().sahSplitCount > 0U, "Binned SAH did not create any SAH split");
    require(sah.stats().primitiveCount == median.stats().primitiveCount,
            "SAH build lost input primitives");
    const Ray outlierRay{glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
    SurfaceInteraction medianHit;
    SurfaceInteraction sahHit;
    BvhTraversalStats medianTraversal;
    BvhTraversalStats sahTraversal;
    require(median.intersect(outlierRay, medianHit, &medianTraversal)
                && sah.intersect(outlierRay, sahHit, &sahTraversal),
            "Median/SAH failed to intersect the outlier primitive");
    require(medianHit.primitiveIndex == 100U && sahHit.primitiveIndex == 100U,
            "SAH changed nearest primitive identity");
    requireNear(sahHit.t, medianHit.t, "SAH changed nearest hit distance");
    require(sahTraversal.triangleTests < medianTraversal.triangleTests,
            "Binned SAH did not reduce outlier triangle tests");

    Triangle later = makeTriangle(2.0f, 90U);
    Triangle stableWinner = makeTriangle(2.0f, 10U);
    Bvh overlapMedian({later, stableWinner}, 4U, BvhSplitStrategy::Median);
    Bvh overlapSah({stableWinner, later}, 4U, BvhSplitStrategy::BinnedSah);
    SurfaceInteraction overlapMedianHit;
    SurfaceInteraction overlapSahHit;
    const Ray overlapRay{glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
    require(overlapMedian.intersect(overlapRay, overlapMedianHit)
                && overlapSah.intersect(overlapRay, overlapSahHit)
                && overlapMedianHit.primitiveIndex == 10U
                && overlapSahHit.primitiveIndex == 10U,
            "Equal-distance BVH hits must resolve to the stable primitive ID");
}

std::shared_ptr<const ModelData> makeInstancedModel() {
    auto model = std::make_shared<ModelData>();
    model->name = "Snapshot fixture";
    model->materials.push_back(MaterialData{});

    MeshData mesh;
    mesh.name = "Shared triangle";
    mesh.vertices.resize(3U);
    mesh.vertices[0].position = glm::vec3(-1.0f, -1.0f, 0.0f);
    mesh.vertices[1].position = glm::vec3(1.0f, -1.0f, 0.0f);
    mesh.vertices[2].position = glm::vec3(0.0f, 1.0f, 0.0f);
    for (Vertex& vertex : mesh.vertices) vertex.normal = glm::vec3(0.0f, 0.0f, 1.0f);
    mesh.indices = {0U, 1U, 2U};
    mesh.submeshes.push_back(SubmeshData{"Triangle", 0U, 3U, 0});
    model->meshes.push_back(std::move(mesh));

    ModelNodeData left;
    left.name = "Left";
    left.localTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, 0.0f, 0.0f));
    left.meshIndices = {0U};
    ModelNodeData right;
    right.name = "Right";
    right.localTransform = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    right.meshIndices = {0U};
    model->rootNode.children = {left, right};
    return model;
}

void testSceneSnapshotInstancing() {
    SnapshotCamera camera;
    camera.position = glm::vec3(0.0f, 0.0f, 5.0f);
    SceneSnapshotBuilder builder(camera);
    const auto model = makeInstancedModel();
    builder.addModel(
        model,
        11U,
        "First entity",
        glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)),
        glm::vec3(0.8f, 0.6f, 0.4f)
    );
    builder.addModel(
        model,
        12U,
        "Second entity",
        glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f))
    );
    const pathtracer::SceneSnapshot snapshot = builder.finish();

    require(snapshot.assets().size() == 1U, "shared ModelData should appear once in a snapshot");
    require(snapshot.instances().size() == 4U, "two nodes across two entities should create four instances");
    requireNear(snapshot.camera().position.z, 5.0f, "snapshot should preserve the camera");

    const std::vector<Triangle> triangles = pathtracer::buildWorldTriangles(snapshot);
    require(triangles.size() == 4U, "each mesh instance should produce one world triangle");
    require(triangles[0].assetIndex == triangles[3].assetIndex,
        "triangles should reference one deduplicated asset");
    require(triangles[0].materialIndex == 0, "submesh material should reach path-tracing geometry");
    requireNear(triangles[0].centroid().x, 1.0f, "entity and node transforms should compose");
    requireNear(triangles[1].centroid().x, 3.0f, "second node transform should remain an instance");
    requireNear(triangles[2].centroid().x, 9.0f, "shared geometry should support another entity transform");
    requireNear(triangles[3].centroid().x, 11.0f, "all shared instances should be exported");

    Bvh bvh(triangles, 2U);
    SurfaceInteraction hit;
    require(
        bvh.intersect(
            Ray{glm::vec3(1.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, -1.0f)},
            hit
        ),
        "snapshot geometry should feed BVH traversal without reparsing assets"
    );
    require(hit.instanceIndex == 0U, "surface interaction should identify the scene instance");
}

void testSnapshotTangentTransform() {
    auto model = std::make_shared<ModelData>();
    model->materials.push_back(MaterialData{});
    MeshData mesh;
    mesh.vertices.resize(3U);
    mesh.vertices[0].position = {-1, -1, 0};
    mesh.vertices[1].position = {1, -1, 0};
    mesh.vertices[2].position = {0, 1, 0};
    for (Vertex& vertex : mesh.vertices) {
        vertex.normal = {0, 0, 1};
        vertex.tangent = {1, 0, 0, 1};
    }
    mesh.indices = {0, 1, 2};
    mesh.submeshes.push_back({"Mirrored", 0, 3, 0});
    model->meshes.push_back(mesh);
    model->rootNode.meshIndices = {0};

    SnapshotCamera camera;
    SceneSnapshotBuilder builder(camera);
    builder.addModel(model, 1, "Mirrored tangent", glm::scale(glm::mat4(1), glm::vec3(-2, 3, 1)));
    const auto triangles = pathtracer::buildWorldTriangles(builder.finish());
    require(triangles.size() == 1, "mirrored tangent fixture should produce one triangle");
    requireNear(triangles[0].tangents[0].x, -1.0f, "tangent should follow object transform");
    requireNear(triangles[0].tangents[0].w, -1.0f, "mirrored transform should flip handedness");
}

} // namespace

int main() {
    try {
        testBoundsIntersection();
        testTriangleIntersection();
        testMedianBvh();
        testBinnedSah();
        testSceneSnapshotInstancing();
        testSnapshotTangentTransform();
        std::cout << "Path-tracing foundation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Path-tracing foundation test failed: " << error.what() << '\n';
        return 1;
    }
}
