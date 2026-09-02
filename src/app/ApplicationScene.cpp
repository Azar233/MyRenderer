#include "app/Application.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include <imgui.h>

#include "app/FileDialog.h"
#include "render/GpuModel.h"
#include "render/Renderer.h"

void Application::drawScenePanel() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float menuHeight = ImGui::GetFrameHeight();
    const ImVec2 contentPosition(viewport->Pos.x, viewport->Pos.y + menuHeight);
    const ImVec2 contentSize(viewport->Size.x, viewport->Size.y - menuHeight);
    ImGui::SetNextWindowPos(contentPosition, ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(285.0f, contentSize.y), ImGuiCond_Once);
    if (!ImGui::Begin("Scene")) {
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Scene objects");
    if (model_) {
        for (const SceneEntity& entity : scene_.entities()) {
            bool visible = entity.visible;
            const std::string visibilityId = "##Visible_" + std::to_string(entity.id);
            if (ImGui::Checkbox(visibilityId.c_str(), &visible)) {
                if (SceneEntity* editable = scene_.find(entity.id)) editable->visible = visible;
            }
            ImGui::SameLine();
            const bool selected = selectedSceneEntity_ == entity.id;
            const std::string label = entity.name + "##Entity_" + std::to_string(entity.id);
            if (ImGui::Selectable(label.c_str(), selected)) selectedSceneEntity_ = entity.id;
            if (entity.parent != invalidSceneEntityId) {
                ImGui::SameLine();
                ImGui::TextDisabled("child of #%llu", static_cast<unsigned long long>(entity.parent));
            }
        }
        ImGui::BeginDisabled(selectedSceneEntity_ == invalidSceneEntityId);
        if (ImGui::Button("Duplicate selected")) {
            selectedSceneEntity_ = scene_.duplicateEntity(selectedSceneEntity_);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(
            selectedSceneEntity_ == primaryEntity_
            || selectedSceneEntity_ == comparisonEntity_
            || selectedSceneEntity_ == backdropEntity_
            || selectedSceneEntity_ == groundEntity_
        );
        if (ImGui::Button("Delete")) {
            scene_.destroyEntity(selectedSceneEntity_);
            selectedSceneEntity_ = primaryEntity_;
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (SceneEntity* selected = scene_.find(selectedSceneEntity_)) {
            const char* parentName = "None";
            if (const SceneEntity* parent = scene_.find(selected->parent)) parentName = parent->name.c_str();
            if (ImGui::BeginCombo("Parent", parentName)) {
                if (ImGui::Selectable("None", selected->parent == invalidSceneEntityId)) {
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

    ImGui::Spacing();
    ImGui::SeparatorText("Model assets");
    for (const auto& path : availableModels_) {
        const bool selected = !currentModelPath_.empty() && path.filename() == currentModelPath_.filename();
        const std::string assetLabel = path.filename().string() + "##Asset_" + path.string();
        if (ImGui::Selectable(assetLabel.c_str(), selected)) {
            loadModel(path);
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
    ImGui::SeparatorText("Open path");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##ModelPath", modelPathBuffer_.data(), modelPathBuffer_.size());
    const bool loadInProgress = pendingModelImport_.has_value();
    ImGui::BeginDisabled(loadInProgress);
    if (ImGui::Button("Browse...", ImVec2(-1.0f, 0.0f))) {
        std::string dialogError;
        const auto selected = openModelFileDialog(dialogError);
        if (selected.has_value()) {
            const std::string selectedPath = selected->string();
            std::snprintf(modelPathBuffer_.data(), modelPathBuffer_.size(), "%s", selectedPath.c_str());
            loadModel(*selected);
        } else if (!dialogError.empty()) {
            statusMessage_ = "Open failed: " + dialogError;
        }
    }
    if (ImGui::Button("Load entered path", ImVec2(-1.0f, 0.0f))) {
        loadModel(std::filesystem::u8path(modelPathBuffer_.data()));
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("You can also drop OBJ, DAE, glTF or GLB files onto the window.");

    ImGui::Spacing();
    ImGui::SeparatorText("Status");
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
        primaryEntity_ = scene_.createEntity(name, model_.get());
        comparisonEntity_ = scene_.createEntity("Comparison instance", model_.get());
        selectedSceneEntity_ = primaryEntity_;
        if (sceneFoundationDemoEnabled_) {
            for (int index = 0; index < 9; ++index) {
                foundationDemoEntities_.push_back(scene_.createEntity(
                    "Shared scene instance " + std::to_string(index + 2),
                    model_.get()
                ));
            }
        }
    }
    if (glassBackdropModel_ != nullptr) {
        backdropEntity_ = scene_.createEntity("Glass checkerboard backdrop", glassBackdropModel_.get());
    }
    if (groundModel_ != nullptr) {
        groundEntity_ = scene_.createEntity("Ground receiver", groundModel_.get());
    }
}

void Application::syncSceneEntities(const glm::mat4& normalization) {
    if (SceneEntity* primary = scene_.find(primaryEntity_)) {
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
    if (SceneEntity* comparison = scene_.find(comparisonEntity_)) {
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
        if (SceneEntity* entity = scene_.find(foundationDemoEntities_[index])) {
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
    if (SceneEntity* backdrop = scene_.find(backdropEntity_)) {
        backdrop->model = glassBackdropModel_.get();
        backdrop->transform = SceneTransform{};
        backdrop->enabledByPreset = glassVolumeDemoEnabled_ && !glassCausticsDemoEnabled_;
        backdrop->castsShadow = false;
    }
    if (SceneEntity* ground = scene_.find(groundEntity_)) {
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
