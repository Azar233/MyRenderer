#include "app/AssetThumbnail.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>

#include "asset/BuiltinModels.h"
#include "io/AssimpImporter.h"
#include "io/ObjLoader.h"
#include "runtime/RenderJob.h"
#include "scene/SceneDocument.h"

namespace {

constexpr std::uint64_t offset = 1469598103934665603ULL;
constexpr std::uint64_t prime = 1099511628211ULL;
constexpr std::uint64_t rasterVersion = 3U;

void hashBytes(std::uint64_t& value, const void* bytes, std::size_t count) {
    const auto* data = static_cast<const unsigned char*>(bytes);
    for (std::size_t i = 0; i < count; ++i) value = (value ^ data[i]) * prime;
}

void hashFile(std::uint64_t& value, const std::filesystem::path& path) {
    const std::string name = path.lexically_normal().generic_u8string();
    hashBytes(value, name.data(), name.size());
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    const std::uintmax_t safeSize = error ? 0U : size;
    hashBytes(value, &safeSize, sizeof(safeSize));
    error.clear();
    const auto time = std::filesystem::last_write_time(path, error);
    const auto ticks = error ? 0 : time.time_since_epoch().count();
    hashBytes(value, &ticks, sizeof(ticks));
}

void hashModelAndSidecars(std::uint64_t& value, const std::filesystem::path& model) {
    hashFile(value, model);
    // OBJ materials, glTF buffers, and most referenced images live beside the model.
    // Hashing siblings may over-invalidate, but cannot leave a stale sidecar preview.
    std::error_code error;
    std::vector<std::filesystem::path> sidecars;
    for (std::filesystem::directory_iterator it(model.parent_path(), error), end;
         !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error)) sidecars.push_back(it->path());
        if (error) break;
    }
    std::sort(sidecars.begin(), sidecars.end());
    for (const auto& path : sidecars) hashFile(value, path);
}

struct Triangle {
    std::array<glm::vec3, 3> points;
    glm::vec3 color{0.7f};
};

void appendModel(std::vector<Triangle>& triangles, const ModelData& model,
                 const glm::mat4& transform, const glm::vec3& tint) {
    const auto visit = [&](const auto& self, const ModelNodeData& node,
                           const glm::mat4& parent) -> void {
        const glm::mat4 world = parent * node.localTransform;
        for (std::uint32_t meshIndex : node.meshIndices) {
            if (meshIndex >= model.meshes.size()) continue;
            const MeshData& mesh = model.meshes[meshIndex];
            const auto appendRange = [&](std::size_t first, std::size_t count, int materialIndex) {
                glm::vec3 color(0.68f, 0.75f, 0.83f);
                if (materialIndex >= 0 && static_cast<std::size_t>(materialIndex) < model.materials.size()) {
                    color = glm::vec3(model.materials[static_cast<std::size_t>(materialIndex)].baseColorFactor);
                }
                color *= tint;
                const std::size_t stop = std::min(mesh.indices.size(), first + count);
                const std::size_t step = std::max<std::size_t>(1U, (stop - first) / 60000U);
                for (std::size_t i = first; i + 2U < stop && triangles.size() < 100000U; i += 3U * step) {
                    Triangle triangle;
                    triangle.color = color;
                    bool valid = true;
                    for (int corner = 0; corner < 3; ++corner) {
                        const std::uint32_t vertex = mesh.indices[i + static_cast<std::size_t>(corner)];
                        if (vertex >= mesh.vertices.size()) { valid = false; break; }
                        triangle.points[static_cast<std::size_t>(corner)] =
                            glm::vec3(world * glm::vec4(mesh.vertices[vertex].position, 1.0f));
                    }
                    if (valid) triangles.push_back(triangle);
                }
            };
            if (mesh.submeshes.empty()) appendRange(0U, mesh.indices.size(), -1);
            else for (const SubmeshData& part : mesh.submeshes) {
                appendRange(part.firstIndex, part.indexCount, part.materialIndex);
            }
        }
        for (const ModelNodeData& child : node.children) self(self, child, world);
    };
    visit(visit, model.rootNode, transform);
}

ModelData importModel(const std::filesystem::path& path) {
    ObjLoader obj;
    AssimpImporter assimp;
    const ModelImporter* importer = obj.supports(path)
        ? static_cast<const ModelImporter*>(&obj)
        : (assimp.supports(path) ? static_cast<const ModelImporter*>(&assimp) : nullptr);
    if (importer == nullptr) throw std::runtime_error("No model importer for " + path.string());
    return importer->load(path).model;
}

void appendScene(std::vector<Triangle>& triangles, const std::filesystem::path& scenePath) {
    SceneDocument document;
    std::string error;
    if (!loadSceneDocument(scenePath, document, error)) throw std::runtime_error(error);
    std::unordered_map<SceneEntityId, std::size_t> indices;
    for (std::size_t i = 0; i < document.entities.size(); ++i) indices[document.entities[i].id] = i;
    std::vector<glm::mat4> worlds(document.entities.size(), glm::mat4(1.0f));
    std::vector<unsigned char> state(document.entities.size(), 0U);
    const auto worldFor = [&](const auto& self, std::size_t i) -> glm::mat4 {
        if (state[i] == 2U) return worlds[i];
        if (state[i] == 1U) throw std::runtime_error("Scene hierarchy cycle");
        state[i] = 1U;
        const SceneDocumentEntity& entity = document.entities[i];
        worlds[i] = entity.transform.matrix();
        if (entity.parent != invalidSceneEntityId) {
            const auto parent = indices.find(entity.parent);
            if (parent == indices.end()) throw std::runtime_error("Missing scene parent");
            worlds[i] = self(self, parent->second) * worlds[i];
        }
        state[i] = 2U;
        return worlds[i];
    };
    std::unordered_map<std::string, ModelData> models;
    for (std::size_t i = 0; i < document.entities.size(); ++i) {
        const SceneDocumentEntity& entity = document.entities[i];
        if (!entity.visible || entity.modelResource.empty()
            || entity.modelResource == builtinGroundResource) continue;
        auto found = models.find(entity.modelResource);
        if (found == models.end()) {
            ModelData data;
            if (entity.modelResource == builtinGroundResource) data = makeGroundPlaneData();
            else if (entity.modelResource == builtinGlassBackdropResource) data = makeGlassCheckerboardData();
            else data = importModel(resolveSceneResource(entity.modelResource, scenePath));
            found = models.emplace(entity.modelResource, std::move(data)).first;
        }
        appendModel(triangles, found->second, worldFor(worldFor, i), entity.tint);
    }
}

AssetThumbnail drawTriangles(const std::vector<Triangle>& triangles) {
    AssetThumbnail image;
    image.rgba.resize(static_cast<std::size_t>(AssetThumbnail::width * AssetThumbnail::height * 4));
    for (int y = 0; y < AssetThumbnail::height; ++y) {
        for (int x = 0; x < AssetThumbnail::width; ++x) {
            const std::size_t p = static_cast<std::size_t>((y * AssetThumbnail::width + x) * 4);
            image.rgba[p] = static_cast<std::uint8_t>(30 + y / 8);
            image.rgba[p + 1U] = static_cast<std::uint8_t>(38 + y / 7);
            image.rgba[p + 2U] = static_cast<std::uint8_t>(51 + y / 6);
            image.rgba[p + 3U] = 255U;
        }
    }
    if (triangles.empty()) return image;
    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    for (const Triangle& triangle : triangles) for (const glm::vec3& point : triangle.points) {
        if (std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z)) {
            minimum = glm::min(minimum, point);
            maximum = glm::max(maximum, point);
        }
    }
    if (minimum.x > maximum.x) return image;
    const glm::vec3 center = (minimum + maximum) * 0.5f;
    const glm::vec3 eye = glm::normalize(glm::vec3(1.0f, 0.8f, 1.25f));
    const glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0, 1, 0), eye));
    const glm::vec3 up = glm::cross(eye, right);
    float spanX = 0.001f, spanY = 0.001f;
    for (const Triangle& triangle : triangles) for (const glm::vec3& point : triangle.points) {
        const glm::vec3 delta = point - center;
        spanX = std::max(spanX, std::abs(glm::dot(delta, right)));
        spanY = std::max(spanY, std::abs(glm::dot(delta, up)));
    }
    const float scale = 0.84f * std::min((AssetThumbnail::width * 0.5f) / spanX,
                                        (AssetThumbnail::height * 0.5f) / spanY);
    std::vector<float> depth(static_cast<std::size_t>(AssetThumbnail::width * AssetThumbnail::height),
                             -std::numeric_limits<float>::infinity());
    const auto edge = [](const glm::vec2& a, const glm::vec2& b, const glm::vec2& p) {
        return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
    };
    for (const Triangle& triangle : triangles) {
        std::array<glm::vec2, 3> screen{};
        std::array<float, 3> z{};
        for (int i = 0; i < 3; ++i) {
            const glm::vec3 delta = triangle.points[static_cast<std::size_t>(i)] - center;
            screen[static_cast<std::size_t>(i)] = glm::vec2(
                AssetThumbnail::width * 0.5f + glm::dot(delta, right) * scale,
                AssetThumbnail::height * 0.5f - glm::dot(delta, up) * scale);
            z[static_cast<std::size_t>(i)] = glm::dot(delta, eye);
        }
        const float area = edge(screen[0], screen[1], screen[2]);
        if (!std::isfinite(area) || std::abs(area) < 0.01f) continue;
        const glm::vec3 normal = glm::normalize(glm::cross(triangle.points[1] - triangle.points[0],
                                                          triangle.points[2] - triangle.points[0]));
        const float light = 0.40f + 0.60f * std::abs(glm::dot(normal,
            glm::normalize(glm::vec3(0.45f, 0.9f, 0.6f))));
        const int minX = std::max(0, static_cast<int>(std::floor(std::min({screen[0].x, screen[1].x, screen[2].x}))));
        const int maxX = std::min(AssetThumbnail::width - 1,
            static_cast<int>(std::ceil(std::max({screen[0].x, screen[1].x, screen[2].x}))));
        const int minY = std::max(0, static_cast<int>(std::floor(std::min({screen[0].y, screen[1].y, screen[2].y}))));
        const int maxY = std::min(AssetThumbnail::height - 1,
            static_cast<int>(std::ceil(std::max({screen[0].y, screen[1].y, screen[2].y}))));
        for (int y = minY; y <= maxY; ++y) for (int x = minX; x <= maxX; ++x) {
            const glm::vec2 sample(x + 0.5f, y + 0.5f);
            const float a = edge(screen[1], screen[2], sample) / area;
            const float b = edge(screen[2], screen[0], sample) / area;
            const float c = 1.0f - a - b;
            if (a < 0.0f || b < 0.0f || c < 0.0f) continue;
            const std::size_t pixel = static_cast<std::size_t>(y * AssetThumbnail::width + x);
            const float distance = a * z[0] + b * z[1] + c * z[2];
            if (distance <= depth[pixel]) continue;
            depth[pixel] = distance;
            for (int channel = 0; channel < 3; ++channel) {
                image.rgba[pixel * 4U + static_cast<std::size_t>(channel)] =
                    static_cast<std::uint8_t>(std::clamp(
                        triangle.color[static_cast<std::size_t>(channel)] * light * 255.0f,
                        0.0f, 255.0f));
            }
        }
    }
    return image;
}

std::filesystem::path cachePath(const std::filesystem::path& root, std::uint64_t key) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string name(16U, '0');
    for (int i = 15; i >= 0; --i) { name[static_cast<std::size_t>(i)] = digits[key & 15U]; key >>= 4U; }
    return root / (name + ".rgba");
}

} // namespace

bool isPreviewableAsset(WorkspaceAssetCategory category) {
    return category == WorkspaceAssetCategory::Scenes
        || category == WorkspaceAssetCategory::Models
        || category == WorkspaceAssetCategory::RenderJobs;
}

std::uint64_t assetThumbnailContentKey(const WorkspaceAssetRecord& asset) {
    std::uint64_t key = offset;
    hashBytes(key, &rasterVersion, sizeof(rasterVersion));
    hashBytes(key, &asset.previewCacheKey, sizeof(asset.previewCacheKey));
    const std::string absolutePath = asset.path.lexically_normal().generic_u8string();
    hashBytes(key, absolutePath.data(), absolutePath.size());
    try {
        std::filesystem::path scene;
        if (asset.category == WorkspaceAssetCategory::RenderJobs) {
            RenderJob job;
            std::string error;
            if (loadRenderJob(asset.path, job, error)) scene = job.scenePath;
        } else if (asset.category == WorkspaceAssetCategory::Scenes) scene = asset.path;
        else if (asset.category == WorkspaceAssetCategory::Models) hashModelAndSidecars(key, asset.path);
        if (!scene.empty()) {
            hashFile(key, scene);
            SceneDocument document;
            std::string error;
            if (loadSceneDocument(scene, document, error)) {
                for (const SceneDocumentEntity& entity : document.entities) {
                    if (!entity.modelResource.empty() && entity.modelResource.rfind("builtin:", 0U) != 0U) {
                        hashModelAndSidecars(key, resolveSceneResource(entity.modelResource, scene));
                    }
                }
            }
        }
    } catch (...) {
        // The generation path will report the invalid asset. Keep a stable key here.
    }
    return key;
}

AssetThumbnail rasterizeAssetThumbnail(const WorkspaceAssetRecord& asset) {
    AssetThumbnail image;
    try {
        std::vector<Triangle> triangles;
        if (asset.category == WorkspaceAssetCategory::Models) {
            appendModel(triangles, importModel(asset.path), glm::mat4(1.0f), glm::vec3(1.0f));
        } else if (asset.category == WorkspaceAssetCategory::Scenes) {
            appendScene(triangles, asset.path);
        } else if (asset.category == WorkspaceAssetCategory::RenderJobs) {
            RenderJob job;
            std::string error;
            if (!loadRenderJob(asset.path, job, error)) throw std::runtime_error(error);
            appendScene(triangles, job.scenePath);
        } else {
            throw std::runtime_error("Asset category is not previewable");
        }
        image = drawTriangles(triangles);
    } catch (const std::exception& exception) {
        image.error = exception.what();
    }
    return image;
}

AssetThumbnail loadOrGenerateAssetThumbnail(const WorkspaceAssetRecord& asset,
                                            std::uint64_t contentKey,
                                            const std::filesystem::path& cacheDirectory) {
    const std::filesystem::path path = cachePath(cacheDirectory, contentKey);
    constexpr std::size_t bytes = static_cast<std::size_t>(AssetThumbnail::width * AssetThumbnail::height * 4);
    AssetThumbnail image;
    {
        std::ifstream input(path, std::ios::binary);
        if (input) {
            image.rgba.resize(bytes);
            input.read(reinterpret_cast<char*>(image.rgba.data()), static_cast<std::streamsize>(bytes));
            if (input.gcount() == static_cast<std::streamsize>(bytes) && input.peek() == EOF) return image;
            image.rgba.clear();
        }
    }
    image = rasterizeAssetThumbnail(asset);
    if (image.error.empty() && image.rgba.size() == bytes) {
        std::error_code error;
        std::filesystem::create_directories(cacheDirectory, error);
        if (!error) {
            const std::filesystem::path temporary = path.string() + "."
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                + "." + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()))
                + ".tmp";
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(image.rgba.data()),
                         static_cast<std::streamsize>(image.rgba.size()));
            output.close();
            if (output) {
                std::filesystem::rename(temporary, path, error);
                if (error) std::filesystem::remove(temporary, error);
            }
        }
    }
    return image;
}
