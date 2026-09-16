#include "app/Application.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include "render/Shader.h"

#include <array>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

#include <imgui.h>
#include <imgui_internal.h>
#include "app/EditorUi.h"

#include "app/FileDialog.h"
#include "render/GpuModel.h"
#include "render/Renderer.h"
#include "scene/SceneDocument.h"

namespace {

bool hasUndersizedDockLeaf(const ImGuiDockNode* node) {
    if (node == nullptr) return true;
    if (node->IsSplitNode()) {
        return hasUndersizedDockLeaf(node->ChildNodes[0])
            || hasUndersizedDockLeaf(node->ChildNodes[1]);
    }
    if (node->Windows.empty()) return false;
    return node->Size.x + 1.0f < EditorUi::minimumDockedPanelSize.x
        || node->Size.y + 1.0f < EditorUi::minimumDockedPanelSize.y;
}

} // namespace

void Application::drawScenePanel() {
    ImGui::SetNextWindowSizeConstraints(
        EditorUi::minimumDockedPanelSize,
        ImVec2(FLT_MAX, FLT_MAX)
    );
    if (!ImGui::Begin(EditorUi::label("Hierarchy###Hierarchy"))) {
        ImGui::End();
        return;
    }

    ImGui::SeparatorText(EditorUi::label("Scene objects"));
    if (!scene_.entities().empty()) {
        for (const SceneEntity& entity : scene_.entities()) {
            if (!entity.enabledByPreset) continue;
            bool visible = entity.visible;
            const std::string visibilityId = "##Visible_" + std::to_string(entity.id);
            if (EditorUi::Checkbox(visibilityId.c_str(), &visible)) {
                if (SceneEntity* editable = scene_.find(entity.id)) editable->visible = visible;
            }
            ImGui::SameLine();
            const bool selected = selectedSceneEntity_ == entity.id;
            const std::string label = entity.name + "##Entity_" + std::to_string(entity.id);
            if (ImGui::Selectable(label.c_str(), selected)) selectEntity(entity.id);
            if (entity.parent != invalidSceneEntityId) {
                ImGui::SameLine();
                ImGui::TextDisabled("child of #%llu", static_cast<unsigned long long>(entity.parent));
            }
        }
        ImGui::BeginDisabled(selectedSceneEntity_ == invalidSceneEntityId);
        if (ImGui::Button(EditorUi::label("Duplicate selected"))) {
            selectEntity(scene_.duplicateEntity(selectedSceneEntity_));
        }
        ImGui::SameLine();
        if (ImGui::Button(EditorUi::label("Delete"))) deleteSelectedEntity();
        ImGui::EndDisabled();
        if (SceneEntity* selected = scene_.find(selectedSceneEntity_)) {
            const char* parentName = "None";
            if (const SceneEntity* parent = scene_.find(selected->parent)) parentName = parent->name.c_str();
            if (ImGui::BeginCombo(EditorUi::label("Parent"), parentName)) {
                if (ImGui::Selectable(EditorUi::label("None"), selected->parent == invalidSceneEntityId)) {
                    scene_.setParent(selected->id, invalidSceneEntityId);
                }
                for (const SceneEntity& candidate : scene_.entities()) {
                    if (candidate.id == selected->id) continue;
                    const bool isParent = candidate.id == selected->parent;
                    if (ImGui::Selectable(candidate.name.c_str(), isParent)) {
                        scene_.setParent(selected->id, candidate.id);
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::TextDisabled("Entities: %zu", scene_.size());
        ImGui::TextDisabled("Meshes: %zu", loadedMeshCount_);
        ImGui::TextDisabled("Submeshes: %zu", loadedSubmeshCount_);
        ImGui::TextDisabled("Vertices: %zu", loadedVertexCount_);
        ImGui::TextDisabled("Triangles: %zu", loadedTriangleCount_);
        if (lightStressDemoEnabled_) {
            ImGui::TreeNodeEx("Stress instances x100", ImGuiTreeNodeFlags_Leaf);
            ImGui::TreePop();
            ImGui::TextDisabled("Local lights: %zu", rendererSettings_.localLights.size());
        }
        if (instanceStressDemoEnabled_) {
            ImGui::TreeNodeEx("Instance stress x2500", ImGuiTreeNodeFlags_Leaf);
            ImGui::TreePop();
            ImGui::TextDisabled(
                "Visible / culled: %zu / %zu",
                renderer_->visibleInstanceCount(),
                renderer_->culledInstanceCount()
            );
        }
        if (rendererSettings_.showPrismIncidentBeam) {
            ImGui::TreeNodeEx("Incident beam (Prism-0 placeholder)", ImGuiTreeNodeFlags_Leaf);
            ImGui::TreePop();
        }
    } else {
        ImGui::TextDisabled("No model loaded");
    }

    if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput
        && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) deleteSelectedEntity();
    ImGui::End();
}

void Application::drawAssetsPanel() {
    ImGui::SetNextWindowSizeConstraints(
        EditorUi::minimumDockedPanelSize,
        ImVec2(FLT_MAX, FLT_MAX)
    );
    if (!ImGui::Begin(EditorUi::label("Content Browser###Assets"))) {
        ImGui::End();
        return;
    }
    ImGui::Spacing();
    ImGui::SeparatorText(EditorUi::label("Model assets"));
    for (const auto& path : availableModels_) {
        const bool selected = !currentModelPath_.empty() && path.filename() == currentModelPath_.filename();
        const std::string assetLabel = path.filename().string() + "##Asset_" + path.string();
        if (ImGui::Selectable(assetLabel.c_str(), selected)) {
            loadModel(path, true);
        }
    }
    if (availableModels_.empty()) {
        ImGui::TextDisabled("No supported model files found");
    }
    if (unsupportedModelCount_ > 0) {
        ImGui::Spacing();
        ImGui::TextDisabled("%zu model(s) await a format importer", unsupportedModelCount_);
    }

    ImGui::Spacing();
    ImGui::SeparatorText(EditorUi::label("Open path"));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##ModelPath", modelPathBuffer_.data(), modelPathBuffer_.size());
    const bool loadInProgress = pendingModelImport_.has_value();
    ImGui::BeginDisabled(loadInProgress);
    if (ImGui::Button(EditorUi::label("Browse..."), ImVec2(-1.0f, 0.0f))) {
        std::string dialogError;
        const auto selected = openModelFileDialog(dialogError);
        if (selected.has_value()) {
            const std::string selectedPath = selected->string();
            std::snprintf(modelPathBuffer_.data(), modelPathBuffer_.size(), "%s", selectedPath.c_str());
            loadModel(*selected, true);
        } else if (!dialogError.empty()) {
            statusMessage_ = "Open failed: " + dialogError;
        }
    }
    if (ImGui::Button(EditorUi::label("Load entered path"), ImVec2(-1.0f, 0.0f))) {
        loadModel(std::filesystem::u8path(modelPathBuffer_.data()), true);
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("You can also drop OBJ, DAE, glTF or GLB files onto the window.");

    ImGui::Spacing();
    ImGui::SeparatorText(EditorUi::label("Status"));
    ImGui::TextWrapped("%s", statusMessage_.c_str());
    if (pendingModelImport_.has_value()) {
        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - pendingModelImport_->startedAt
        ).count();
        const float activity = static_cast<float>(std::fmod(elapsed * 0.35, 1.0));
        ImGui::ProgressBar(activity, ImVec2(-1.0f, 0.0f), "Importing on CPU...");
        ImGui::TextDisabled(
            "%.2f MiB | %.1f s elapsed | current scene stays active",
            static_cast<double>(pendingModelImport_->fileSize) / (1024.0 * 1024.0),
            elapsed
        );
    }
    drawDiagnostics();
    ImGui::End();
}

void Application::rebuildSceneEntities() {
    stressEntities_.clear();
    editedEntities_.clear();
    scene_.clear();
    primaryEntity_ = invalidSceneEntityId;
    comparisonEntity_ = invalidSceneEntityId;
    backdropEntity_ = invalidSceneEntityId;
    groundEntity_ = invalidSceneEntityId;
    selectedSceneEntity_ = invalidSceneEntityId;
    foundationDemoEntities_.clear();
    if (model_ != nullptr) {
        const std::string name = currentModelPath_.empty()
            ? "Model"
            : currentModelPath_.filename().string();
        const std::string resource = currentModelPath_.generic_u8string();
        primaryEntity_ = scene_.createEntity(name, model_.get(), resource);
        comparisonEntity_ = scene_.createEntity("Comparison instance", model_.get(), resource);
        selectedSceneEntity_ = primaryEntity_;
        if (sceneFoundationDemoEnabled_) {
            for (int index = 0; index < 9; ++index) {
                foundationDemoEntities_.push_back(scene_.createEntity(
                    "Shared scene instance " + std::to_string(index + 2),
                    model_.get(),
                    resource
                ));
            }
        }
    }
    if (glassBackdropModel_ != nullptr) {
        backdropEntity_ = scene_.createEntity(
            "Glass checkerboard backdrop", glassBackdropModel_.get(), builtinGlassBackdropResource
        );
    }
    if (groundModel_ != nullptr) {
        groundEntity_ = scene_.createEntity(
            EditorUi::label("Ground receiver"), groundModel_.get(), builtinGroundResource
        );
    }
}

void Application::syncSceneEntities(const glm::mat4& normalization) {
    if (loadedSceneDocument_) {
        scene_.updateWorldTransforms();
        return;
    }
    if (SceneEntity* primary = scene_.find(primaryEntity_); primary && !editedEntities_.count(primaryEntity_)) {
        primary->model = model_.get();
        primary->transform.translation = modelPosition_;
        primary->transform.rotationDegrees = modelRotationDegrees_;
        primary->transform.scale = glm::vec3(modelScale_);
        primary->transform.assetTransform = normalization;
        primary->tint = rendererSettings_.baseColor;
        primary->enabledByPreset = !lightStressDemoEnabled_ && !instanceStressDemoEnabled_
            && (!prismDemoEnabled_ || prismModelVisible_);
        primary->castsShadow = true;
    }
    if (SceneEntity* comparison = scene_.find(comparisonEntity_); comparison && !editedEntities_.count(comparisonEntity_)) {
        comparison->model = model_.get();
        comparison->transform.translation = modelPosition_ + (glassVolumeDemoEnabled_
            ? glm::vec3(0.92f, 0.0f, 0.0f)
            : glm::vec3(0.95f, 0.0f, 0.35f));
        comparison->transform.rotationDegrees = glassVolumeDemoEnabled_
            ? glm::vec3(0.0f)
            : glm::vec3(0.0f, -28.0f, 0.0f);
        comparison->transform.scale = glm::vec3(
            modelScale_ * (glassVolumeDemoEnabled_ ? 0.88f : 0.50f)
        );
        comparison->transform.assetTransform = normalization;
        comparison->tint = glassVolumeDemoEnabled_
            ? glm::vec3(1.0f)
            : glm::vec3(0.72f, 0.82f, 1.0f);
        comparison->enabledByPreset = showComparisonObject_
            && !lightStressDemoEnabled_ && !instanceStressDemoEnabled_;
        comparison->castsShadow = true;
    }
    static constexpr std::array<glm::vec3, 6> foundationTints{
        glm::vec3(0.90f, 0.38f, 0.28f),
        glm::vec3(0.94f, 0.68f, 0.24f),
        glm::vec3(0.36f, 0.78f, 0.46f),
        glm::vec3(0.28f, 0.62f, 0.92f),
        glm::vec3(0.54f, 0.40f, 0.90f),
        glm::vec3(0.88f, 0.34f, 0.68f)
    };
    for (std::size_t index = 0; index < foundationDemoEntities_.size(); ++index) {
        if (SceneEntity* entity = scene_.find(foundationDemoEntities_[index]); entity && !editedEntities_.count(entity->id)) {
            const int slot = static_cast<int>(index) + 1;
            entity->model = model_.get();
            entity->transform.translation = glm::vec3(
                -3.0f + static_cast<float>(slot % 5) * 1.5f,
                0.0f,
                slot < 5 ? -1.05f : 1.05f
            );
            entity->transform.rotationDegrees = glm::vec3(0.0f, static_cast<float>(slot * 23), 0.0f);
            entity->transform.scale = glm::vec3(0.82f);
            entity->transform.assetTransform = normalization;
            entity->tint = foundationTints[index % foundationTints.size()];
            entity->enabledByPreset = sceneFoundationDemoEnabled_;
            entity->castsShadow = false;
            entity->instanceCandidate = false;
        }
    }
    if (SceneEntity* backdrop = scene_.find(backdropEntity_); backdrop && !editedEntities_.count(backdropEntity_)) {
        backdrop->model = glassBackdropModel_.get();
        backdrop->transform = SceneTransform{};
        backdrop->enabledByPreset = glassVolumeDemoEnabled_ && !glassCausticsDemoEnabled_;
        backdrop->castsShadow = false;
    }
    if (SceneEntity* ground = scene_.find(groundEntity_); ground && !editedEntities_.count(groundEntity_)) {
        ground->model = groundModel_.get();
        ground->transform = SceneTransform{};
        ground->transform.translation = glm::vec3(
            modelPosition_.x,
            modelPosition_.y + groundOffset_ * modelScale_,
            modelPosition_.z
        );
        ground->tint = groundColor_;
        ground->enabledByPreset = showGroundPlane_;
        ground->castsShadow = false;
    }
    scene_.updateWorldTransforms();
}

void Application::drawEditorLayout() {
    const ImGuiID dock = ImGui::DockSpaceOverViewport();
    const ImGuiDockNode* root = ImGui::DockBuilderGetNode(dock);
    const bool explicitReset = resetEditorLayout_;
    const bool layoutNeedsRepair = root == nullptr || !root->IsSplitNode()
        || hasUndersizedDockLeaf(root);
    if (explicitReset || layoutNeedsRepair) {
        resetEditorLayout_ = false;
        if (explicitReset) {
            hierarchyPanelOpen_ = true;
            inspectorPanelOpen_ = true;
            assetsPanelOpen_ = true;
        }
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        const ImVec2 workSize = ImGui::GetMainViewport()->WorkSize;
        ImGui::DockBuilderSetNodeSize(dock, workSize);
        ImGuiID center = dock;
        const float leftWidth = std::max(EditorUi::minimumDockedPanelSize.x, workSize.x * 0.18f);
        const float leftRatio = std::min(leftWidth / std::max(workSize.x, 1.0f), 0.28f);
        const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, leftRatio, nullptr, &center);
        const float remainingWidth = std::max(workSize.x - leftWidth, 1.0f);
        const float rightWidth = std::max(300.0f, workSize.x * 0.23f);
        const float rightRatio = std::min(rightWidth / remainingWidth, 0.38f);
        const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, rightRatio, nullptr, &center);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.24f, nullptr, &center);
        ImGui::DockBuilderDockWindow("###Hierarchy", left);
        ImGui::DockBuilderDockWindow("###Inspector", right);
        ImGui::DockBuilderDockWindow("###Viewport", center);
        ImGui::DockBuilderDockWindow("###Assets", bottom);
        ImGui::DockBuilderFinish(dock);
    }
}

void Application::selectEntity(SceneEntityId id) {
    selectedSceneEntity_ = scene_.find(id) ? id : invalidSceneEntityId;
    focusObjectTab_ = true;
}

void Application::deleteSelectedEntity() {
    const auto id = selectedSceneEntity_;
    if (!scene_.destroyEntity(id)) return;
    editedEntities_.erase(id);
    if (id == primaryEntity_) primaryEntity_ = invalidSceneEntityId;
    if (id == comparisonEntity_) { comparisonEntity_ = invalidSceneEntityId; showComparisonObject_ = false; }
    if (id == groundEntity_) { groundEntity_ = invalidSceneEntityId; showGroundPlane_ = false; }
    if (id == backdropEntity_) backdropEntity_ = invalidSceneEntityId;
    selectEntity(invalidSceneEntityId);
}

void Application::newEmptyScene() {
    // In-flight CPU work may finish, but its result belongs to the old generation.
    ++sceneGeneration_;
    droppedModelPaths_.clear();
    scene_.clear();
    importedModels_.clear();
    model_.reset();
    stressEntities_.clear();
    editedEntities_.clear();
    foundationDemoEntities_.clear();
    primaryEntity_ = comparisonEntity_ = groundEntity_ = backdropEntity_ = invalidSceneEntityId;
    selectEntity(invalidSceneEntityId);
    emptySceneSession_ = true;
    loadedSceneDocument_ = false;
    prismDemoPreviousState_.reset();
    prismDemoEnabled_ = glassVolumeDemoEnabled_ = glassCausticsDemoEnabled_ = false;
    lightStressDemoEnabled_ = instanceStressDemoEnabled_ = sceneFoundationDemoEnabled_ = false;
    animationDemoEnabled_ = animationEnabled_ = false;
    temporalMotionDemoEnabled_ = objectMotionDemoEnabled_ = false;
    autoRotate_ = showGroundPlane_ = showComparisonObject_ = false;
    prismCameraLocked_ = false;
    rendererSettings_ = RendererSettings{};
    currentModelPath_.clear();
    currentScenePath_.clear();
    modelPathBuffer_.fill(0);
    modelDiagnostics_.clear();
    pendingScreenshotPath_.clear();
    loadedMeshCount_ = loadedSubmeshCount_ = loadedTransparentSubmeshCount_ = 0;
    loadedVertexCount_ = loadedTriangleCount_ = loadedMaterialCount_ = loadedTextureCount_ = 0;
    loadedDecodedTextureCount_ = loadedFallbackTextureCount_ = loadedTextureMemoryBytes_ = 0;
    modelCenter_ = glm::vec3(0.0f);
    modelNormalizationScale_ = 1.0f;
    resetObjectTransform();
    camera_.reset();
    statusMessage_ = EditorUi::chinese ? "已新建空场景，可导入多个模型。" : "New empty scene. Import models to begin.";
}

SceneDocument Application::captureSceneDocument() const {
    SceneDocument document;
    document.camera = camera_.orbitState();
    document.renderer = rendererSettings_;
    document.playback.animationEnabled = animationEnabled_;
    document.playback.animationPlaying = animationPlaying_;
    document.playback.animationTimeSeconds = animationTimeSeconds_;
    document.playback.animationSpeed = animationSpeed_;
    document.playback.animationClipIndex = animationClipIndex_;
    document.playback.prismEnabled = prismDemoEnabled_;
    document.playback.prismCameraLocked = prismCameraLocked_;
    document.playback.prismPreset = prismOpticalPreset_;
    document.playback.prismParameters = prismParameters_;
    document.entities.reserve(scene_.size());
    for (const SceneEntity& source : scene_.entities()) {
        SceneDocumentEntity entity;
        entity.id = source.id;
        entity.name = source.name;
        entity.parent = source.parent;
        entity.modelResource = source.modelResource;
        if (entity.modelResource.empty() && source.model == groundModel_.get()) {
            entity.modelResource = builtinGroundResource;
        } else if (entity.modelResource.empty() && source.model == glassBackdropModel_.get()) {
            entity.modelResource = builtinGlassBackdropResource;
        } else if (entity.modelResource.empty() && source.model == model_.get()) {
            entity.modelResource = currentModelPath_.generic_u8string();
        } else if (entity.modelResource.empty() && source.model != nullptr) {
            const auto matching = std::find_if(
                scene_.entities().begin(), scene_.entities().end(),
                [&](const SceneEntity& candidate) {
                    return candidate.model == source.model && !candidate.modelResource.empty();
                }
            );
            if (matching != scene_.entities().end()) entity.modelResource = matching->modelResource;
        }
        entity.transform = source.transform;
        entity.tint = source.tint;
        entity.visible = source.visible && source.enabledByPreset;
        entity.castsShadow = source.castsShadow;
        entity.instanceCandidate = source.instanceCandidate;
        document.entities.push_back(std::move(entity));
    }
    return document;
}

bool Application::saveCurrentScene() {
    return currentScenePath_.empty() ? saveSceneAs() : saveSceneTo(currentScenePath_);
}

bool Application::saveSceneAs() {
    std::filesystem::path suggestion = currentScenePath_;
    if (suggestion.empty()) suggestion = sourceRoot_ / "assets" / "scenes" / "untitled.myscene";
    std::string dialogError;
    const auto selected = saveSceneFileDialog(suggestion, dialogError);
    if (!selected.has_value()) {
        if (!dialogError.empty()) statusMessage_ = "Save scene failed: " + dialogError;
        return false;
    }
    std::filesystem::path path = *selected;
    if (path.extension().empty()) path += myRendererSceneExtension;
    return saveSceneTo(path);
}

bool Application::saveSceneTo(const std::filesystem::path& path) {
    if (pendingModelImport_.has_value()) {
        statusMessage_ = "Wait for the current model import before saving the scene.";
        return false;
    }
    SceneDocument document = captureSceneDocument();
    for (const SceneDocumentEntity& entity : document.entities) {
        const SceneEntity* source = scene_.find(entity.id);
        if (source != nullptr && source->model != nullptr && entity.modelResource.empty()) {
            statusMessage_ = "Save scene failed: entity '" + entity.name + "' has no persistent model resource.";
            return false;
        }
    }
    std::error_code pathError;
    const std::filesystem::path absolute = std::filesystem::absolute(path, pathError).lexically_normal();
    std::string error;
    if (pathError || !saveSceneDocument(pathError ? path : absolute, document, error)) {
        statusMessage_ = "Save scene failed: " + (pathError ? pathError.message() : error);
        return false;
    }
    currentScenePath_ = absolute;
    rememberRecentScene(absolute);
    statusMessage_ = "Saved scene " + absolute.filename().u8string();
    std::cout << statusMessage_ << " (" << document.entities.size() << " entities)\n";
    return true;
}

void Application::openSceneFromDialog() {
    if (pendingModelImport_.has_value()) return;
    std::string dialogError;
    const auto selected = openSceneFileDialog(dialogError);
    if (selected.has_value()) {
        openScene(*selected);
    } else if (!dialogError.empty()) {
        statusMessage_ = "Open scene failed: " + dialogError;
    }
}

bool Application::openScene(const std::filesystem::path& path) {
    if (pendingModelImport_.has_value()) {
        statusMessage_ = "Wait for the current model import before opening a scene.";
        return false;
    }
    std::error_code pathError;
    const std::filesystem::path absolute = std::filesystem::absolute(path, pathError).lexically_normal();
    if (pathError) {
        statusMessage_ = "Open scene failed: " + pathError.message();
        return false;
    }
    SceneDocument document;
    std::string documentError;
    if (!loadSceneDocument(absolute, document, documentError)) {
        statusMessage_ = "Open scene failed: " + documentError;
        return false;
    }

    struct PreparedModel {
        std::filesystem::path path;
        std::unique_ptr<GpuModel> model;
    };
    std::vector<PreparedModel> prepared;
    std::unordered_map<std::string, const GpuModel*> modelsByPath;
    std::unordered_map<std::string, std::string> resolvedResources;
    try {
        for (const SceneDocumentEntity& entity : document.entities) {
            if (entity.modelResource.empty()
                || entity.modelResource == builtinGroundResource
                || entity.modelResource == builtinGlassBackdropResource) {
                continue;
            }
            const std::filesystem::path resolved = resolveSceneResource(entity.modelResource, absolute);
            const std::string key = resolved.generic_u8string();
            resolvedResources[entity.modelResource] = key;
            if (modelsByPath.count(key) != 0U) continue;
            if (!std::filesystem::is_regular_file(resolved)) {
                throw std::runtime_error("Missing model resource: " + resolved.string());
            }
            const ModelImporter* importer = findImporter(resolved);
            if (importer == nullptr) throw std::runtime_error("Unsupported model resource: " + resolved.string());
            ModelImportResult imported = importer->load(resolved);
            std::vector<TextureUploadWarning> warnings;
            auto gpu = std::make_unique<GpuModel>(
                std::move(imported.model), renderer_->textureCache(), warnings
            );
            const GpuModel* pointer = gpu.get();
            prepared.push_back(PreparedModel{resolved, std::move(gpu)});
            modelsByPath.emplace(key, pointer);
        }

        newEmptyScene();
        currentScenePath_ = absolute;
        loadedSceneDocument_ = true;
        emptySceneSession_ = false;
        rendererSettings_ = document.renderer;
        camera_.setOrbitState(document.camera);
        animationEnabled_ = document.playback.animationEnabled;
        animationPlaying_ = document.playback.animationPlaying;
        animationTimeSeconds_ = document.playback.animationTimeSeconds;
        animationSpeed_ = document.playback.animationSpeed;
        animationClipIndex_ = document.playback.animationClipIndex;
        prismDemoEnabled_ = document.playback.prismEnabled;
        prismCameraLocked_ = document.playback.prismCameraLocked;
        prismOpticalPreset_ = document.playback.prismPreset;
        prismParameters_ = document.playback.prismParameters;

        if (!prepared.empty()) {
            currentModelPath_ = prepared.front().path;
            model_ = std::move(prepared.front().model);
            for (std::size_t index = 1; index < prepared.size(); ++index) {
                importedModels_.push_back(std::move(prepared[index].model));
            }
        }
        const auto modelFor = [&](const std::string& resource) -> const GpuModel* {
            if (resource.empty()) return nullptr;
            if (resource == builtinGroundResource) return groundModel_.get();
            if (resource == builtinGlassBackdropResource) return glassBackdropModel_.get();
            const auto resolved = resolvedResources.find(resource);
            if (resolved == resolvedResources.end()) return nullptr;
            const auto found = modelsByPath.find(resolved->second);
            return found == modelsByPath.end() ? nullptr : found->second;
        };
        for (const SceneDocumentEntity& saved : document.entities) {
            const std::string persistentResource = saved.modelResource.rfind("builtin:", 0U) == 0U
                ? saved.modelResource
                : resolvedResources[saved.modelResource];
            const SceneEntityId id = scene_.createEntityWithId(
                saved.id, saved.name, modelFor(saved.modelResource), persistentResource
            );
            if (id == invalidSceneEntityId) throw std::runtime_error("Could not restore scene entity ID");
            SceneEntity* entity = scene_.find(id);
            entity->transform = saved.transform;
            entity->tint = saved.tint;
            entity->visible = saved.visible;
            entity->enabledByPreset = true;
            entity->castsShadow = saved.castsShadow;
            entity->instanceCandidate = saved.instanceCandidate;
            editedEntities_.insert(id);
        }
        for (const SceneDocumentEntity& saved : document.entities) {
            if (saved.parent != invalidSceneEntityId && !scene_.setParent(saved.id, saved.parent)) {
                throw std::runtime_error("Could not restore scene hierarchy");
            }
        }
        scene_.updateWorldTransforms();
        selectedSceneEntity_ = document.entities.empty()
            ? invalidSceneEntityId
            : document.entities.front().id;
        showGroundPlane_ = std::any_of(
            document.entities.begin(), document.entities.end(),
            [](const SceneDocumentEntity& entity) {
                return entity.modelResource == builtinGroundResource && entity.visible;
            }
        );

        loadedMeshCount_ = loadedSubmeshCount_ = loadedTransparentSubmeshCount_ = 0U;
        loadedVertexCount_ = loadedTriangleCount_ = loadedMaterialCount_ = loadedTextureCount_ = 0U;
        loadedDecodedTextureCount_ = loadedFallbackTextureCount_ = loadedTextureMemoryBytes_ = 0U;
        std::unordered_set<const GpuModel*> counted;
        for (const SceneEntity& entity : scene_.entities()) {
            if (entity.model == nullptr || entity.model == groundModel_.get()
                || entity.model == glassBackdropModel_.get() || !counted.insert(entity.model).second) continue;
            loadedMeshCount_ += entity.model->meshCount();
            loadedSubmeshCount_ += entity.model->submeshCount();
            loadedTransparentSubmeshCount_ += entity.model->transparentSubmeshCount();
            loadedVertexCount_ += entity.model->vertexCount();
            loadedTriangleCount_ += entity.model->triangleCount();
            loadedMaterialCount_ += entity.model->materialCount();
            loadedTextureCount_ += entity.model->textureCount();
            loadedDecodedTextureCount_ += entity.model->loadedTextureCount();
            loadedFallbackTextureCount_ += entity.model->fallbackTextureCount();
            loadedTextureMemoryBytes_ += entity.model->textureMemoryBytes();
        }
        if (!currentModelPath_.empty()) {
            const std::string modelPath = currentModelPath_.string();
            std::snprintf(modelPathBuffer_.data(), modelPathBuffer_.size(), "%s", modelPath.c_str());
        }
        if (prismDemoEnabled_) updatePrismDemoOptics();
        if (model_ != nullptr && model_->hasSkinning()) {
            animationClipIndex_ = std::min(
                animationClipIndex_,
                model_->animationCount() > 0U ? model_->animationCount() - 1U : 0U
            );
            model_->updateAnimation(animationEnabled_, animationClipIndex_, animationTimeSeconds_);
        }
        rememberRecentScene(absolute);
        statusMessage_ = "Opened scene " + absolute.filename().u8string() + " ("
            + std::to_string(scene_.size()) + " entities, "
            + std::to_string(counted.size()) + " model assets)";
        std::cout << statusMessage_ << '\n';
        return true;
    } catch (const std::exception& exception) {
        statusMessage_ = "Open scene failed; current scene preserved: " + std::string(exception.what());
        std::cerr << statusMessage_ << '\n';
        return false;
    }
}

void Application::rememberRecentScene(const std::filesystem::path& path) {
    std::ofstream stream("MyRenderer.recent-scene", std::ios::binary | std::ios::trunc);
    if (stream) stream << path.generic_u8string();
}

std::filesystem::path Application::recentScenePath() const {
    std::ifstream stream("MyRenderer.recent-scene", std::ios::binary);
    std::string value;
    std::getline(stream, value);
    return value.empty() ? std::filesystem::path{} : std::filesystem::u8path(value);
}

void Application::materializeStressEntities(std::vector<RenderItem>& items) {
    // Benchmark mode keeps its original synthetic submission path.
    if (stressEntities_.empty()) {
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto id = scene_.createEntity("Stress object " + std::to_string(i + 1), items[i].model);
            stressEntities_.push_back(id);
            auto* entity = scene_.find(id);
            if (items[i].model == model_.get()) {
                entity->modelResource = currentModelPath_.generic_u8string();
            } else {
                const auto source = std::find_if(
                    scene_.entities().begin(), scene_.entities().end(),
                    [&](const SceneEntity& candidate) {
                        return candidate.model == items[i].model && !candidate.modelResource.empty();
                    }
                );
                if (source != scene_.entities().end()) entity->modelResource = source->modelResource;
            }
            const auto& matrix = items[i].modelMatrix;
            entity->transform.translation = glm::vec3(matrix[3]);
            entity->transform.scale = glm::vec3(glm::length(glm::vec3(matrix[0])),
                glm::length(glm::vec3(matrix[1])), glm::length(glm::vec3(matrix[2])));
            const glm::mat3 rotation{glm::vec3(matrix[0]) / entity->transform.scale.x,
                glm::vec3(matrix[1]) / entity->transform.scale.y,
                glm::vec3(matrix[2]) / entity->transform.scale.z};
            entity->transform.rotationDegrees = glm::degrees(glm::eulerAngles(glm::quat_cast(rotation)));
            entity->tint = items[i].tint;
            entity->castsShadow = items[i].castsShadow;
            entity->instanceCandidate = items[i].instanceCandidate;
        }
    }
    scene_.updateWorldTransforms();
    items.clear();
    for (auto id : stressEntities_) {
        const auto* entity = scene_.find(id);
        if (!entity || !entity->visible) continue;
        items.push_back(RenderItem{entity->model, entity->worldTransform, entity->tint,
            true, entity->castsShadow, entity->instanceCandidate, id,
            entity->previousWorldTransform, entity->motionHistoryValid});
    }
}

SceneEntityId Application::pickEntity(const std::vector<RenderItem>& items,
    int width, int height, int x, int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) return invalidSceneEntityId;
    if (!pickingShader_) pickingShader_ = std::make_unique<Shader>(
        sourceRoot_ / "shaders/basic.vert", sourceRoot_ / "shaders/editor_pick.frag");
    // Only run on clicks; integer IDs are unaffected by lighting, tone mapping or MSAA.
    GLint previousDraw, previousRead, viewport[4], scissor[4], depthFunc, program, vao;
    GLint cullFace, frontFace, polygon[2], renderbuffer, packAlignment;
    GLboolean depthMask, colorMask[4];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    glGetIntegerv(GL_VIEWPORT, viewport); glGetIntegerv(GL_SCISSOR_BOX, scissor);
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc); glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_CULL_FACE_MODE, &cullFace); glGetIntegerv(GL_FRONT_FACE, &frontFace);
    glGetIntegerv(GL_POLYGON_MODE, polygon); glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
    const GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
    const GLboolean cull = glIsEnabled(GL_CULL_FACE), scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    GLuint framebuffer = 0, color = 0, depthBuffer = 0;
    glGenFramebuffers(1, &framebuffer); glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glGenRenderbuffers(1, &color); glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_R32UI, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    glGenRenderbuffers(1, &depthBuffer); glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    GLuint hit = 0;
    if (complete) {
        glViewport(0, 0, width, height);
        glEnable(GL_SCISSOR_TEST); glScissor(x, y, 1, 1);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
        glDisable(GL_BLEND); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glFrontFace(GL_CCW);
        const GLuint zero[4]{}; const float farDepth = 1.0f;
        glClearBufferuiv(GL_COLOR, 0, zero); glClearBufferfv(GL_DEPTH, 0, &farDepth);
        pickingShader_->use();
        pickingShader_->setMat4("uView", camera_.viewMatrix());
        pickingShader_->setMat4("uProjection", camera_.projectionMatrix(static_cast<float>(width) / height));
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            if (!item.visible || !item.model) continue;
            pickingShader_->setInt("uPickIndex", static_cast<int>(i + 1));
            pickingShader_->setMat4("uModel", item.modelMatrix);
            item.model->drawOpaque(*pickingShader_, glm::vec3(1.0f), rendererSettings_.cullBackFaces);
            for (std::size_t mesh = 0; mesh < item.model->transparentSubmeshCount(); ++mesh)
                item.model->drawTransparentSubmesh(*pickingShader_, glm::vec3(1.0f), mesh, rendererSettings_.cullBackFaces);
        }
        glReadBuffer(GL_COLOR_ATTACHMENT0); glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &hit);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previousDraw); glBindFramebuffer(GL_READ_FRAMEBUFFER, previousRead);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glDeleteRenderbuffers(1, &color); glDeleteRenderbuffers(1, &depthBuffer); glDeleteFramebuffers(1, &framebuffer);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    const auto restore = [](GLenum cap, GLboolean enabled) { if (enabled) glEnable(cap); else glDisable(cap); };
    restore(GL_DEPTH_TEST, depth); restore(GL_BLEND, blend); restore(GL_CULL_FACE, cull); restore(GL_SCISSOR_TEST, scissorEnabled);
    glDepthFunc(depthFunc); glDepthMask(depthMask); glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    glCullFace(cullFace); glFrontFace(frontFace); glPolygonMode(GL_FRONT_AND_BACK, polygon[0]);
    glUseProgram(program); glBindVertexArray(vao); glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
    if (!complete) throw std::runtime_error("Editor picking framebuffer is incomplete");
    return hit > 0 && hit <= items.size() ? items[hit - 1].entityId : invalidSceneEntityId;
}

bool Application::editorInteractionRegression() {
    try {
        const auto check = [](bool passed, const char* message) { if (!passed) throw std::runtime_error(message); };
        newEmptyScene();
        const auto cube = sourceRoot_ / "assets/models/cube.obj";
        finishModelLoad(cube, findImporter(cube)->load(cube), true);
        check(scene_.size() == 1 && !model_ && !lightStressDemoEnabled_, "empty scene first import");
        scene_.updateWorldTransforms();
        const auto first = selectedSceneEntity_;
        camera_.reset();
        check(pickEntity(scene_.buildRenderItems(), 400, 300, 200, 150) == first, "viewport picks visible geometry");
        std::vector<unsigned char> lastOutlinePixels;
        const auto outlinePixels = [&](SceneEntityId selected, int width, int height) {
            RendererSettings settings;
            settings.skyboxEnabled = false;
            settings.bloom = false;
            renderer_->render(scene_.buildRenderItems(), camera_, settings, width, height);
            renderer_->drawSelectionOutline(scene_.buildRenderItems(), camera_, selected, settings.cullBackFaces);
            std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, renderer_->colorTexture());
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            std::size_t orange = 0;
            for (std::size_t i = 0; i < pixels.size(); i += 4) {
                if (pixels[i] > 250 && pixels[i + 1] > 140 && pixels[i + 1] < 155 && pixels[i + 2] < 30) ++orange;
            }
            lastOutlinePixels = std::move(pixels);
            return orange;
        };
        check(outlinePixels(first, 400, 300) > 100, "selection produces orange outline");
        check(outlinePixels(0, 400, 300) == 0, "deselection removes outline");
        check(outlinePixels(first, 240, 180) > 50, "outline survives viewport resize");
        const auto isOrange = [](const std::vector<unsigned char>& pixels, std::size_t i) {
            return pixels[i] > 250 && pixels[i + 1] > 140 && pixels[i + 1] < 155 && pixels[i + 2] < 30;
        };
        const auto occluder = scene_.duplicateEntity(first);
        const auto originalTransform = scene_.find(first)->transform;
        for (int fixture = 0; fixture < 2; ++fixture) {
            for (float pitch : {20.0f, 55.0f}) {
                camera_.setOrbitPose(glm::vec3(0.0f), 35.0f, pitch, 5.0f, 45.0f);
                scene_.find(first)->transform = originalTransform;
                if (fixture == 0) {
                    scene_.find(first)->transform.scale = glm::vec3(4.0f, 0.01f, 4.0f);
                    scene_.find(first)->transform.translation.y = -0.72f;
                }
                scene_.find(occluder)->visible = false;
                scene_.updateWorldTransforms();
                check(outlinePixels(first, 400, 300) > 50, "unoccluded fixture has outline");
                const auto silhouettePixels = lastOutlinePixels;
                for (float offset : {0.0f, 0.65f}) {
                    auto* blocker = scene_.find(occluder);
                    blocker->visible = true;
                    blocker->transform = originalTransform;
                    blocker->transform.scale = glm::vec3(fixture == 0 ? 0.8f : 0.45f);
                    blocker->transform.translation = fixture == 0 ? glm::vec3(offset, 0.0f, 0.0f)
                        : camera_.position() * 0.22f + glm::vec3(offset, 0.0f, 0.0f);
                    scene_.updateWorldTransforms();
                    outlinePixels(first, 400, 300);
                    if (fixture == 0 && pitch == 55.0f && offset == 0.0f) {
                        if (const char* capture = std::getenv("MYRENDERER_OUTLINE_CAPTURE")) {
                            std::string error;
                            check(renderer_->saveScreenshot(std::filesystem::u8path(capture), error), "outline regression screenshot");
                        }
                    }
                    for (std::size_t i = 0; i < lastOutlinePixels.size(); i += 4)
                        check(!isOrange(lastOutlinePixels, i) || isOrange(silhouettePixels, i),
                            "occluder must not generate extra outline edges");
                }
            }
        }
        scene_.find(first)->transform = originalTransform;
        camera_.reset();
        scene_.find(occluder)->transform = originalTransform;
        scene_.find(occluder)->transform.translation = camera_.position() * 0.4f;
        scene_.find(occluder)->transform.scale = glm::vec3(1.1f);
        scene_.updateWorldTransforms();
        check(outlinePixels(first, 400, 300) == 0, "fully occluded selection has no outline");
        scene_.destroyEntity(occluder);
        scene_.updateWorldTransforms();
        check(pickEntity(scene_.buildRenderItems(), 400, 300, 0, 0) == 0, "background clears selection");
        const auto second = scene_.duplicateEntity(first);
        scene_.find(second)->transform.translation = camera_.position() * 0.2f;
        scene_.updateWorldTransforms();
        check(pickEntity(scene_.buildRenderItems(), 400, 300, 200, 150) == second, "nearest overlapping model selected");
        selectEntity(second); deleteSelectedEntity();
        check(!scene_.find(second) && selectedSceneEntity_ == 0, "delete updates selection");
        check(pickEntity(scene_.buildRenderItems(), 400, 300, 200, 150) == first, "deleted model no longer picked");
        scene_.find(first)->visible = false;
        check(pickEntity(scene_.buildRenderItems(), 400, 300, 200, 150) == 0, "hidden models not picked");
        std::vector<RenderItem> stress{{importedModels_.front().get(), glm::mat4(1.0f)}};
        materializeStressEntities(stress);
        selectEntity(stress.front().entityId); deleteSelectedEntity();
        materializeStressEntities(stress);
        check(stress.empty(), "deleted stress instance stays deleted");
        loadModel(cube, true);
        newEmptyScene();
        pendingModelImport_->future.wait(); updateModelLoad();
        check(scene_.size() == 0 && importedModels_.empty(), "new scene discards in-flight import");
        const auto glass = sourceRoot_ / "assets/models/glass_volume_sphere.gltf";
        finishModelLoad(glass, findImporter(glass)->load(glass), true);
        check(scene_.size() == 1 && !glassVolumeDemoEnabled_ && !showComparisonObject_, "ordinary import does not activate fixture preset");
        newEmptyScene();
        check(scene_.size() == 0 && rendererSettings_.localLights.empty(), "new scene clears all content");
        check(glGetError() == GL_NO_ERROR, "picking leaves no OpenGL errors");
        std::cout << "Editor interaction validation: PASS\n";
        return true;
    } catch (const std::exception& error) {
        std::cerr << "Editor interaction validation: FAIL: " << error.what() << '\n';
        return false;
    }
}
