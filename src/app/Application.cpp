#include "app/Application.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat3x3.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "app/AppIcon.h"
#include "app/FileDialog.h"
#include "io/AssimpImporter.h"
#include "io/ModelImporter.h"
#include "io/ObjLoader.h"
#include "render/GpuModel.h"
#include "render/OpenGlDebug.h"
#include "render/Renderer.h"

namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

const char* glString(unsigned int name) {
    const auto* value = glGetString(name);
    return value == nullptr ? "Unknown" : reinterpret_cast<const char*>(value);
}

const char* diagnosticScopeName(ModelDiagnosticScope scope) {
    switch (scope) {
    case ModelDiagnosticScope::File: return "File";
    case ModelDiagnosticScope::Node: return "Node";
    case ModelDiagnosticScope::Mesh: return "Mesh";
    case ModelDiagnosticScope::Material: return "Material";
    case ModelDiagnosticScope::Texture: return "Texture";
    }
    return "Unknown";
}

const char* diagnosticSeverityName(ModelDiagnosticSeverity severity) {
    switch (severity) {
    case ModelDiagnosticSeverity::Info: return "Info";
    case ModelDiagnosticSeverity::Warning: return "Warning";
    case ModelDiagnosticSeverity::Error: return "Error";
    }
    return "Unknown";
}

} // namespace

Application::Application()
    : sourceRoot_(std::filesystem::path(MYRENDERER_SOURCE_DIR)) {
}

Application::~Application() {
    shutdown();
}

int Application::run(const std::filesystem::path& initialModel) {
    initializeWindow();
    initializeGui();
    initializeRenderer();
    initializeImporters();
    discoverModels();
    if (const char* msaa = std::getenv("MYRENDERER_MSAA")) {
        rendererSettings_.msaaSamples = std::atoi(msaa) <= 1 ? 1 : 4;
    }
    if (const char* value = std::getenv("MYRENDERER_PBR")) rendererSettings_.pbrEnabled = std::atoi(value) != 0;
    if (const char* value = std::getenv("MYRENDERER_IBL")) rendererSettings_.iblEnabled = std::atoi(value) != 0;
    if (const char* value = std::getenv("MYRENDERER_SHADOWS")) rendererSettings_.shadowsEnabled = std::atoi(value) != 0;
    if (const char* value = std::getenv("MYRENDERER_BLOOM")) rendererSettings_.bloom = std::atoi(value) != 0;

    std::filesystem::path modelToLoad = initialModel;
    if (modelToLoad.empty()) {
        const auto cube = sourceRoot_ / "assets" / "models" / "cube.obj";
        modelToLoad = std::filesystem::exists(cube)
            ? cube
            : (availableModels_.empty() ? std::filesystem::path{} : availableModels_.front());
    }
    if (!modelToLoad.empty()) {
        loadModel(modelToLoad);
    } else {
        statusMessage_ = "No supported model was found in assets/models";
    }

    if (const char* screenshotPath = std::getenv("MYRENDERER_SCREENSHOT")) {
        pendingScreenshotPath_ = std::filesystem::absolute(screenshotPath).lexically_normal();
    }
    const char* recoveryModelValue = std::getenv("MYRENDERER_RECOVERY_TEST");
    const std::filesystem::path recoveryModel = recoveryModelValue == nullptr
        ? std::filesystem::path{}
        : std::filesystem::path(recoveryModelValue);
    bool recoveryScheduled = recoveryModel.empty();

    int smokeTestFrames = std::getenv("MYRENDERER_SMOKE_TEST") == nullptr ? -1 : 5;
    previousFrameTime_ = glfwGetTime();
    while (!glfwWindowShouldClose(window_)) {
        const double cpuFrameStart = glfwGetTime();
        glfwPollEvents();
        if (droppedModelPath_.has_value() && !pendingModelImport_.has_value()) {
            const std::filesystem::path dropped = std::move(*droppedModelPath_);
            droppedModelPath_.reset();
            loadModel(dropped);
        }
        updateModelLoad();
        if (!recoveryScheduled && model_ != nullptr && !pendingModelImport_.has_value()) {
            recoveryScheduled = loadModel(recoveryModel);
        }
        if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }

        const double currentTime = glfwGetTime();
        const float deltaTime = static_cast<float>(std::min(currentTime - previousFrameTime_, 0.1));
        previousFrameTime_ = currentTime;
        if (autoRotate_) {
            modelRotationDegrees_.y = std::fmod(modelRotationDegrees_.y + 25.0f * deltaTime, 360.0f);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        drawMainMenu();
        ImGui::DockSpaceOverViewport();
        drawScenePanel();
        drawInspectorPanel();
        drawViewportPanel();
        drawAboutPopup();
        if (showImGuiDemo_) {
            ImGui::ShowDemoWindow(&showImGuiDemo_);
        }

        ImGui::Render();
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window_, &framebufferWidth, &framebufferHeight);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, framebufferWidth, framebufferHeight);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glClearColor(0.035f, 0.04f, 0.055f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window_);
        const double measuredCpuTime = (glfwGetTime() - cpuFrameStart) * 1000.0;
        cpuFrameTimeMilliseconds_ = cpuFrameTimeMilliseconds_ > 0.0
            ? cpuFrameTimeMilliseconds_ * 0.9 + measuredCpuTime * 0.1
            : measuredCpuTime;
        if (smokeTestFrames > 0 && !pendingModelImport_.has_value() && --smokeTestFrames == 0) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
    }

    const bool recoveryPassed = recoveryModel.empty()
        || (recoveryScheduled && lastLoadFailed_ && model_ != nullptr);
    shutdown();
    return recoveryPassed ? 0 : 2;
}

void Application::initializeWindow() {
    glfwSetErrorCallback([](int code, const char* description) {
        std::cerr << "GLFW error " << code << ": " << description << '\n';
    });
    if (glfwInit() != GLFW_TRUE) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 0);
    if (std::getenv("MYRENDERER_SMOKE_TEST") != nullptr) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    window_ = glfwCreateWindow(1440, 900, "MyRenderer - OpenGL Rasterizer", nullptr, nullptr);
    if (window_ == nullptr) {
        glfwTerminate();
        throw std::runtime_error("Failed to create an OpenGL 3.3 window");
    }
    setMyRendererWindowIcon(window_);
    glfwSetWindowUserPointer(window_, this);
    glfwSetDropCallback(window_, [](GLFWwindow* window, int count, const char** paths) {
        auto* application = static_cast<Application*>(glfwGetWindowUserPointer(window));
        if (application != nullptr) {
            application->queueDroppedFiles(count, paths);
        }
    });
    glfwSetWindowSizeLimits(window_, 960, 600, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(vsync_ ? 1 : 0);

    const int version = gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress));
    if (version == 0) {
        throw std::runtime_error("Failed to load OpenGL functions through GLAD");
    }

    initializeOpenGlDebugOutput();

    gpuDescription_ = std::string(glString(GL_RENDERER)) + " | OpenGL " + glString(GL_VERSION);
    std::cout << "GPU: " << glString(GL_RENDERER) << '\n'
              << "Vendor: " << glString(GL_VENDOR) << '\n'
              << "OpenGL: " << glString(GL_VERSION) << '\n';

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
}

void Application::initializeGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = "MyRenderer.ini";

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.085f, 0.11f, 1.0f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.055f, 0.065f, 0.09f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.12f, 0.17f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.17f, 0.23f, 0.36f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.23f, 0.32f, 0.50f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.16f, 0.22f, 0.35f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.34f, 0.54f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.42f, 0.67f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.42f, 0.67f, 1.0f, 1.0f);

    if (!ImGui_ImplGlfw_InitForOpenGL(window_, true)) {
        throw std::runtime_error("Failed to initialize the ImGui GLFW backend");
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
        throw std::runtime_error("Failed to initialize the ImGui OpenGL backend");
    }
    guiInitialized_ = true;
}

void Application::initializeRenderer() {
    renderer_ = std::make_unique<Renderer>(
        sourceRoot_ / "shaders" / "basic.vert",
        sourceRoot_ / "shaders" / "basic.frag",
        sourceRoot_ / "shaders" / "debug_lines.vert",
        sourceRoot_ / "shaders" / "debug_lines.frag"
    );
}

void Application::initializeImporters() {
    importers_.push_back(std::make_unique<ObjLoader>());
    importers_.push_back(std::make_unique<AssimpImporter>());
}

void Application::shutdown() {
    if (shutdownComplete_) {
        return;
    }
    shutdownComplete_ = true;

    if (pendingModelImport_.has_value()) {
        pendingModelImport_->future.wait();
        try {
            pendingModelImport_->future.get();
        } catch (...) {
        }
        pendingModelImport_.reset();
    }

    if (window_ != nullptr) {
        glfwMakeContextCurrent(window_);
        model_.reset();
        renderer_.reset();
    }
    if (guiInitialized_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        guiInitialized_ = false;
    }
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

void Application::drawMainMenu() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open model...", "Ctrl+O", false, !pendingModelImport_.has_value())) {
            std::string dialogError;
            const auto selected = openModelFileDialog(dialogError);
            if (selected.has_value()) {
                loadModel(*selected);
            } else if (!dialogError.empty()) {
                statusMessage_ = "Open failed: " + dialogError;
            }
        }
        if (ImGui::BeginMenu("Open bundled model")) {
            for (const auto& path : availableModels_) {
                if (ImGui::MenuItem(path.filename().string().c_str())) {
                    loadModel(path);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(
            "Reload current",
            "Ctrl+R",
            false,
            !currentModelPath_.empty() && !pendingModelImport_.has_value()
        )) {
            loadModel(currentModelPath_);
        }
        if (ImGui::MenuItem("Save viewport PNG", nullptr, false, model_ != nullptr)) {
            pendingScreenshotPath_ = nextScreenshotPath();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Esc")) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Reset camera", "F")) {
            camera_.reset();
        }
        ImGui::MenuItem("Wireframe", nullptr, &rendererSettings_.wireframe);
        ImGui::MenuItem("Back-face culling", nullptr, &rendererSettings_.cullBackFaces);
        ImGui::Separator();
        ImGui::MenuItem("Ground grid", nullptr, &rendererSettings_.showGrid);
        ImGui::MenuItem("XYZ axes", nullptr, &rendererSettings_.showAxes);
        ImGui::MenuItem("Auto rotate", nullptr, &autoRotate_);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("Dear ImGui demo", nullptr, &showImGuiDemo_);
        if (ImGui::MenuItem("About MyRenderer")) {
            showAbout_ = true;
        }
        ImGui::EndMenu();
    }

    const std::string fps = std::to_string(static_cast<int>(ImGui::GetIO().Framerate)) + " FPS";
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - ImGui::CalcTextSize(fps.c_str()).x - 16.0f));
    ImGui::TextDisabled("%s", fps.c_str());
    ImGui::EndMainMenuBar();
}

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

    ImGui::SeparatorText("Current mesh");
    if (model_) {
        const std::string currentMeshLabel = currentModelPath_.filename().string() + "##CurrentMesh";
        ImGui::TreeNodeEx(currentMeshLabel.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::TreePop();
        ImGui::TextDisabled("Meshes: %zu", loadedMeshCount_);
        ImGui::TextDisabled("Submeshes: %zu", loadedSubmeshCount_);
        ImGui::TextDisabled("Vertices: %zu", loadedVertexCount_);
        ImGui::TextDisabled("Triangles: %zu", loadedTriangleCount_);
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

void Application::drawInspectorPanel() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float menuHeight = ImGui::GetFrameHeight();
    const ImVec2 contentPosition(viewport->Pos.x, viewport->Pos.y + menuHeight);
    const ImVec2 contentSize(viewport->Size.x, viewport->Size.y - menuHeight);
    ImGui::SetNextWindowPos(
        ImVec2(contentPosition.x + contentSize.x - 335.0f, contentPosition.y),
        ImGuiCond_Once
    );
    ImGui::SetNextWindowSize(ImVec2(335.0f, contentSize.y), ImGuiCond_Once);
    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("InspectorTabs")) {
        if (ImGui::BeginTabItem("Object")) {
            ImGui::SeparatorText("Transform");
            ImGui::DragFloat3("Position", &modelPosition_.x, 0.01f, 0.0f, 0.0f, "%.2f");
            ImGui::DragFloat3("Rotation", &modelRotationDegrees_.x, 0.25f, -360.0f, 360.0f, "%.1f deg");
            ImGui::SliderFloat("Scale", &modelScale_, 0.1f, 4.0f, "%.2f");
            ImGui::Checkbox("Auto rotate", &autoRotate_);
            if (ImGui::Button("Reset transform", ImVec2(-1.0f, 0.0f))) {
                resetObjectTransform();
                camera_.reset(modelPosition_);
            }

            ImGui::SeparatorText("Material");
            ImGui::ColorEdit3("Base color tint", &rendererSettings_.baseColor.x);
            ImGui::SliderFloat("Ambient", &rendererSettings_.ambientStrength, 0.0f, 1.0f);
            ImGui::SliderFloat("Diffuse", &rendererSettings_.diffuseStrength, 0.0f, 2.0f);
            ImGui::SliderFloat("Specular", &rendererSettings_.specularStrength, 0.0f, 2.0f);
            ImGui::SliderFloat("Shininess", &rendererSettings_.shininess, 1.0f, 256.0f, "%.0f", ImGuiSliderFlags_Logarithmic);

            ImGui::SeparatorText("Asset statistics");
            if (model_) {
                ImGui::Text("Meshes: %zu", loadedMeshCount_);
                ImGui::Text("Submeshes / draw calls: %zu", loadedSubmeshCount_);
                ImGui::Text("Materials: %zu", loadedMaterialCount_);
                ImGui::Text("Textures: %zu", loadedTextureCount_);
                ImGui::Text("Decoded textures: %zu", loadedDecodedTextureCount_);
                ImGui::Text("Fallback textures: %zu", loadedFallbackTextureCount_);
                ImGui::Text(
                    "Estimated texture memory: %.2f MiB",
                    static_cast<double>(loadedTextureMemoryBytes_) / (1024.0 * 1024.0)
                );
            } else {
                ImGui::TextDisabled("No model loaded");
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Renderer")) {
            ImGui::SeparatorText("PBR & environment");
            ImGui::Checkbox("Metallic-roughness PBR", &rendererSettings_.pbrEnabled);
            ImGui::Checkbox("Image-based lighting", &rendererSettings_.iblEnabled);
            ImGui::Checkbox("Skybox", &rendererSettings_.skyboxEnabled);
            ImGui::Checkbox("Shadow mapping", &rendererSettings_.shadowsEnabled);
            ImGui::SliderFloat("Environment", &rendererSettings_.environmentIntensity, 0.0f, 2.0f, "%.2f");
            ImGui::TextDisabled("Shadow map: %d x %d", renderer_->shadowResolution(), renderer_->shadowResolution());

            ImGui::SeparatorText("Post processing");
            ImGui::Checkbox("ACES tone mapping", &rendererSettings_.toneMapping);
            ImGui::Checkbox("Bloom", &rendererSettings_.bloom);
            ImGui::SliderFloat("Exposure", &rendererSettings_.exposure, 0.1f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::BeginDisabled(!rendererSettings_.bloom);
            ImGui::SliderFloat("Bloom threshold", &rendererSettings_.bloomThreshold, 0.1f, 4.0f, "%.2f");
            ImGui::SliderFloat("Bloom intensity", &rendererSettings_.bloomIntensity, 0.0f, 1.0f, "%.2f");
            ImGui::EndDisabled();

            ImGui::SeparatorText("Rasterization");
            ImGui::Checkbox("Wireframe", &rendererSettings_.wireframe);
            ImGui::Checkbox("Back-face culling", &rendererSettings_.cullBackFaces);
            ImGui::Checkbox("Normal mapping", &rendererSettings_.normalMapping);
            ImGui::Checkbox("Ground grid", &rendererSettings_.showGrid);
            ImGui::Checkbox("XYZ axes + gizmo", &rendererSettings_.showAxes);
            ImGui::ColorEdit3("Background", &rendererSettings_.backgroundColor.x);
            const char* msaaOptions[] = {"1x", "4x"};
            int msaaSelection = rendererSettings_.msaaSamples > 1 ? 1 : 0;
            if (ImGui::Combo("MSAA", &msaaSelection, msaaOptions, 2)) {
                rendererSettings_.msaaSamples = msaaSelection == 0 ? 1 : 4;
            }
            ImGui::TextDisabled("Active samples: %dx", renderer_->activeMsaaSamples());

            ImGui::SeparatorText("Directional light");
            ImGui::DragFloat3("Direction", &rendererSettings_.lightDirection.x, 0.01f, -1.0f, 1.0f, "%.2f");

            ImGui::SeparatorText("Camera");
            float fieldOfView = camera_.fieldOfView();
            if (ImGui::SliderFloat("Field of view", &fieldOfView, 15.0f, 90.0f, "%.0f deg")) {
                camera_.setFieldOfView(fieldOfView);
            }
            if (ImGui::Button("Frame model", ImVec2(-1.0f, 0.0f))) {
                camera_.reset(modelPosition_);
            }

            ImGui::SeparatorText("Runtime");
            if (ImGui::Checkbox("VSync", &vsync_)) {
                glfwSwapInterval(vsync_ ? 1 : 0);
            }
            ImGui::Text("CPU frame: %.2f ms", cpuFrameTimeMilliseconds_);
            if (renderer_->hasGpuFrameTime()) {
                ImGui::Text("GPU viewport: %.2f ms", renderer_->gpuFrameTimeMilliseconds());
            } else {
                ImGui::TextDisabled("GPU viewport: collecting...");
            }
            ImGui::Text("Draw calls: %zu", renderer_->drawCallCount());
            ImGui::Text("Active passes: %zu", renderer_->activePassNames().size());
            for (const auto& passName : renderer_->activePassNames()) {
                ImGui::BulletText("%s", passName.c_str());
            }
            ImGui::Text("Triangles: %zu", model_ ? loadedTriangleCount_ : 0U);
            ImGui::Text(
                "Texture memory: %.2f MiB",
                static_cast<double>(loadedTextureMemoryBytes_) / (1024.0 * 1024.0)
            );
            if (lastLoadTotalMilliseconds_ > 0.0) {
                ImGui::TextDisabled(
                    "Last load: %.1f ms CPU + %.1f ms GPU = %.1f ms",
                    lastCpuImportMilliseconds_,
                    lastGpuUploadMilliseconds_,
                    lastLoadTotalMilliseconds_
                );
            }
            ImGui::TextWrapped("%s", gpuDescription_.c_str());
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void Application::drawViewportPanel() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float menuHeight = ImGui::GetFrameHeight();
    const ImVec2 contentPosition(viewport->Pos.x, viewport->Pos.y + menuHeight);
    const ImVec2 contentSize(viewport->Size.x, viewport->Size.y - menuHeight);
    ImGui::SetNextWindowPos(ImVec2(contentPosition.x + 285.0f, contentPosition.y), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(std::max(contentSize.x - 620.0f, 320.0f), contentSize.y), ImGuiCond_Once);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin("Viewport");
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return;
    }

    ImGui::SetCursorPos(ImVec2(10.0f, 30.0f));
    if (ImGui::SmallButton("Frame")) {
        camera_.reset(modelPosition_);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Save PNG")) {
        pendingScreenshotPath_ = nextScreenshotPath();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &rendererSettings_.showGrid);
    ImGui::SameLine();
    ImGui::Checkbox("Axes", &rendererSettings_.showAxes);
    ImGui::SameLine();
    ImGui::TextDisabled("RMB orbit | MMB pan | Wheel zoom");
    ImGui::SetCursorPosY(55.0f);

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const int width = std::max(static_cast<int>(available.x), 1);
    const int height = std::max(static_cast<int>(available.y), 1);

    const glm::mat4 normalization =
        glm::scale(glm::mat4(1.0f), glm::vec3(modelNormalizationScale_))
        * glm::translate(glm::mat4(1.0f), -modelCenter_);
    glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), modelPosition_);
    modelMatrix = glm::rotate(modelMatrix, glm::radians(modelRotationDegrees_.z), glm::vec3(0.0f, 0.0f, 1.0f));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(modelRotationDegrees_.y), glm::vec3(0.0f, 1.0f, 0.0f));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(modelRotationDegrees_.x), glm::vec3(1.0f, 0.0f, 0.0f));
    modelMatrix = glm::scale(modelMatrix, glm::vec3(modelScale_));
    modelMatrix *= normalization;
    renderer_->render(model_.get(), camera_, modelMatrix, rendererSettings_, width, height);

    if (!pendingScreenshotPath_.empty() && model_ != nullptr && !pendingModelImport_.has_value()) {
        std::string screenshotError;
        if (renderer_->saveScreenshot(pendingScreenshotPath_, screenshotError)) {
            statusMessage_ = "Saved screenshot (MSAA "
                           + std::to_string(renderer_->activeMsaaSamples())
                           + "x): " + pendingScreenshotPath_.string();
            std::cout << statusMessage_ << '\n';
        } else {
            statusMessage_ = "Screenshot failed: " + screenshotError;
            std::cerr << statusMessage_ << '\n';
        }
        pendingScreenshotPath_.clear();
    }

    ImGui::Image(
        static_cast<ImTextureID>(static_cast<std::uintptr_t>(renderer_->colorTexture())),
        ImVec2(static_cast<float>(width), static_cast<float>(height)),
        ImVec2(0.0f, 1.0f),
        ImVec2(1.0f, 0.0f)
    );

    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f) {
            camera_.zoom(io.MouseWheel);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            camera_.orbit(-io.MouseDelta.x * 0.007f, -io.MouseDelta.y * 0.007f);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            camera_.pan(io.MouseDelta.x, io.MouseDelta.y);
        }
    }
    if (rendererSettings_.showAxes) {
        drawOrientationGizmo();
    }
    ImGui::End();
}

void Application::drawOrientationGizmo() {
    const ImVec2 imageMin = ImGui::GetItemRectMin();
    const ImVec2 imageMax = ImGui::GetItemRectMax();
    if (imageMax.x - imageMin.x < 120.0f || imageMax.y - imageMin.y < 120.0f) {
        return;
    }

    struct AxisGuide {
        glm::vec3 direction;
        const char* label;
        ImU32 color;
        glm::vec3 cameraDirection{0.0f};
    };
    std::array<AxisGuide, 3> axes{
        AxisGuide{{1.0f, 0.0f, 0.0f}, "X", IM_COL32(255, 70, 70, 255)},
        AxisGuide{{0.0f, 1.0f, 0.0f}, "Y", IM_COL32(70, 235, 95, 255)},
        AxisGuide{{0.0f, 0.0f, 1.0f}, "Z", IM_COL32(70, 125, 255, 255)}
    };
    const glm::mat3 viewRotation(camera_.viewMatrix());
    for (AxisGuide& axis : axes) {
        axis.cameraDirection = viewRotation * axis.direction;
    }
    std::sort(axes.begin(), axes.end(), [](const AxisGuide& left, const AxisGuide& right) {
        return left.cameraDirection.z < right.cameraDirection.z;
    });

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 center(imageMin.x + 58.0f, imageMax.y - 58.0f);
    constexpr float radius = 34.0f;
    drawList->AddCircleFilled(center, 47.0f, IM_COL32(10, 13, 20, 185), 32);
    drawList->AddCircle(center, 47.0f, IM_COL32(115, 125, 150, 145), 32, 1.0f);
    for (const AxisGuide& axis : axes) {
        const ImVec2 endpoint(
            center.x + axis.cameraDirection.x * radius,
            center.y - axis.cameraDirection.y * radius
        );
        drawList->AddLine(center, endpoint, axis.color, 3.0f);
        drawList->AddCircleFilled(endpoint, 4.5f, axis.color, 12);
        const ImVec2 textSize = ImGui::CalcTextSize(axis.label);
        const float offsetX = endpoint.x >= center.x ? 7.0f : -textSize.x - 7.0f;
        const float offsetY = endpoint.y >= center.y ? 4.0f : -textSize.y - 4.0f;
        drawList->AddText({endpoint.x + offsetX, endpoint.y + offsetY}, axis.color, axis.label);
    }
    drawList->AddCircleFilled(center, 3.5f, IM_COL32(230, 235, 245, 255), 12);
}

void Application::drawDiagnostics() {
    if (modelDiagnostics_.empty()) {
        ImGui::TextDisabled("No import diagnostics.");
        return;
    }

    if (!ImGui::TreeNodeEx("Import diagnostics", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    static constexpr std::array<ModelDiagnosticScope, 5> scopes{
        ModelDiagnosticScope::File,
        ModelDiagnosticScope::Node,
        ModelDiagnosticScope::Mesh,
        ModelDiagnosticScope::Material,
        ModelDiagnosticScope::Texture
    };
    for (const ModelDiagnosticScope scope : scopes) {
        const std::size_t count = static_cast<std::size_t>(std::count_if(
            modelDiagnostics_.begin(),
            modelDiagnostics_.end(),
            [scope](const ModelDiagnostic& diagnostic) { return diagnostic.scope == scope; }
        ));
        if (count == 0U) {
            continue;
        }
        const std::string label = std::string(diagnosticScopeName(scope))
                                + " (" + std::to_string(count) + ")";
        if (!ImGui::TreeNode(label.c_str())) {
            continue;
        }
        for (std::size_t index = 0; index < modelDiagnostics_.size(); ++index) {
            const ModelDiagnostic& diagnostic = modelDiagnostics_[index];
            if (diagnostic.scope != scope) {
                continue;
            }
            ImGui::PushID(static_cast<int>(index));
            const ImVec4 color = diagnostic.severity == ModelDiagnosticSeverity::Error
                ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                : diagnostic.severity == ModelDiagnosticSeverity::Warning
                    ? ImVec4(1.0f, 0.75f, 0.25f, 1.0f)
                    : ImVec4(0.45f, 0.72f, 1.0f, 1.0f);
            ImGui::TextColored(
                color,
                "%s%s%s",
                diagnosticSeverityName(diagnostic.severity),
                diagnostic.context.empty() ? "" : " - ",
                diagnostic.context.c_str()
            );
            ImGui::TextWrapped("%s", diagnostic.message.c_str());
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    ImGui::TreePop();
}

void Application::drawAboutPopup() {
    if (showAbout_) {
        ImGui::OpenPopup("About MyRenderer");
        showAbout_ = false;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("About MyRenderer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("MyRenderer 0.1.0");
        ImGui::Separator();
        ImGui::Text("C++17 / OpenGL 3.3 / GPU rasterization");
        ImGui::TextWrapped("A compact GPU renderer with a format-independent model pipeline.");
        if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Application::discoverModels() {
    availableModels_.clear();
    unsupportedModelCount_ = 0;
    const auto modelDirectory = sourceRoot_ / "assets" / "models";
    if (!std::filesystem::exists(modelDirectory)) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(modelDirectory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (findImporter(entry.path()) != nullptr) {
            availableModels_.push_back(entry.path());
        } else {
            const std::string extension = lowercase(entry.path().extension().string());
            if (extension == ".dae" || extension == ".fbx" || extension == ".gltf" || extension == ".glb") {
                ++unsupportedModelCount_;
            }
        }
    }
    std::sort(availableModels_.begin(), availableModels_.end());
}

bool Application::loadModel(const std::filesystem::path& path) {
    if (pendingModelImport_.has_value()) {
        statusMessage_ = "A model is already loading; wait for it to finish before starting another import.";
        return false;
    }

    try {
        const auto resolved = resolvePath(path);
        const ModelImporter* importer = findImporter(resolved);
        if (importer == nullptr) {
            throw std::runtime_error("No model importer supports: " + resolved.string());
        }

        std::error_code fileSizeError;
        const std::uintmax_t fileSize = std::filesystem::file_size(resolved, fileSizeError);
        modelDiagnostics_.clear();
        modelDiagnostics_.push_back(ModelDiagnostic{
            ModelDiagnosticScope::File,
            ModelDiagnosticSeverity::Info,
            resolved.filename().string(),
            "CPU asset import started; the current scene will remain active until validation succeeds."
        });
        const std::string pathString = resolved.string();
        std::snprintf(modelPathBuffer_.data(), modelPathBuffer_.size(), "%s", pathString.c_str());
        statusMessage_ = "Loading " + resolved.filename().string() + " on a background CPU task...";
        lastLoadFailed_ = false;
        std::cout << statusMessage_ << '\n';

        auto future = std::async(std::launch::async, [importer, resolved]() {
            const auto startedAt = std::chrono::steady_clock::now();
            ModelImportResult loaded = importer->load(resolved);
            loaded.cpuTimeMilliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - startedAt
            ).count();
            return loaded;
        });
        pendingModelImport_.emplace(PendingModelImport{
            resolved,
            std::move(future),
            std::chrono::steady_clock::now(),
            fileSizeError ? 0U : fileSize
        });
        return true;
    } catch (const std::exception& error) {
        lastLoadFailed_ = true;
        statusMessage_ = std::string("Load failed: ") + error.what();
        modelDiagnostics_.clear();
        modelDiagnostics_.push_back(ModelDiagnostic{
            ModelDiagnosticScope::File,
            ModelDiagnosticSeverity::Error,
            path.filename().string(),
            error.what()
        });
        std::cerr << statusMessage_ << '\n';
        return false;
    }
}

void Application::updateModelLoad() {
    if (!pendingModelImport_.has_value()
        || pendingModelImport_->future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        return;
    }

    PendingModelImport pending = std::move(*pendingModelImport_);
    pendingModelImport_.reset();
    try {
        ModelImportResult loaded = pending.future.get();
        finishModelLoad(pending.path, std::move(loaded));
    } catch (const std::exception& error) {
        lastLoadFailed_ = true;
        lastLoadTotalMilliseconds_ = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - pending.startedAt
        ).count();
        statusMessage_ = "Load failed; current scene preserved: " + std::string(error.what());
        modelDiagnostics_.clear();
        modelDiagnostics_.push_back(ModelDiagnostic{
            ModelDiagnosticScope::File,
            ModelDiagnosticSeverity::Error,
            pending.path.filename().string(),
            error.what()
        });
        std::cerr << statusMessage_ << '\n';
    }
}

void Application::finishModelLoad(const std::filesystem::path& path, ModelImportResult loaded) {
    const auto gpuUploadStartedAt = std::chrono::steady_clock::now();
    std::vector<TextureUploadWarning> textureWarnings;
    auto newModel = std::make_unique<GpuModel>(
        loaded.model,
        renderer_->textureCache(),
        textureWarnings
    );
    const glm::vec3 extent = loaded.model.boundsMax - loaded.model.boundsMin;
    const float maximumExtent = std::max({extent.x, extent.y, extent.z});
    if (!std::isfinite(maximumExtent) || maximumExtent <= 1e-8f) {
        throw std::runtime_error("Model bounds are empty or degenerate");
    }

    lastCpuImportMilliseconds_ = loaded.cpuTimeMilliseconds;
    lastGpuUploadMilliseconds_ = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - gpuUploadStartedAt
    ).count();
    lastLoadTotalMilliseconds_ = lastCpuImportMilliseconds_ + lastGpuUploadMilliseconds_;
    lastLoadFailed_ = false;
    modelDiagnostics_ = std::move(loaded.diagnostics);
    for (auto& warning : textureWarnings) {
        modelDiagnostics_.push_back(ModelDiagnostic{
            ModelDiagnosticScope::Texture,
            ModelDiagnosticSeverity::Warning,
            std::move(warning.textureName),
            std::move(warning.message)
        });
    }

    model_ = std::move(newModel);
    currentModelPath_ = path;
    loadedMeshCount_ = model_->meshCount();
    loadedSubmeshCount_ = model_->submeshCount();
    loadedVertexCount_ = model_->vertexCount();
    loadedTriangleCount_ = model_->triangleCount();
    loadedMaterialCount_ = model_->materialCount();
    loadedTextureCount_ = model_->textureCount();
    loadedDecodedTextureCount_ = model_->loadedTextureCount();
    loadedFallbackTextureCount_ = model_->fallbackTextureCount();
    loadedTextureMemoryBytes_ = model_->textureMemoryBytes();
    modelCenter_ = 0.5f * (loaded.model.boundsMin + loaded.model.boundsMax);
    modelNormalizationScale_ = 1.4f / maximumExtent;
    resetObjectTransform();
    camera_.reset();

    const std::string pathString = currentModelPath_.string();
    std::snprintf(modelPathBuffer_.data(), modelPathBuffer_.size(), "%s", pathString.c_str());
    statusMessage_ = "Loaded " + currentModelPath_.filename().string() + " ("
                   + std::to_string(loadedMeshCount_) + " mesh, "
                   + std::to_string(loadedSubmeshCount_) + " submesh, "
                   + std::to_string(loadedVertexCount_) + " vertices, "
                   + std::to_string(loadedTriangleCount_) + " triangles, "
                   + std::to_string(loadedMaterialCount_) + " materials, "
                   + std::to_string(loadedTextureCount_) + " textures, "
                   + std::to_string(loadedDecodedTextureCount_) + " decoded, "
                   + std::to_string(loadedFallbackTextureCount_) + " fallback; "
                   + std::to_string(static_cast<int>(lastLoadTotalMilliseconds_)) + " ms)";
    std::cout << statusMessage_ << '\n';
    for (const ModelDiagnostic& diagnostic : modelDiagnostics_) {
        if (diagnostic.severity == ModelDiagnosticSeverity::Info) {
            continue;
        }
        std::cout << diagnosticSeverityName(diagnostic.severity) << " ["
                  << diagnosticScopeName(diagnostic.scope) << "] "
                  << diagnostic.context << ": " << diagnostic.message << '\n';
    }
}

void Application::queueDroppedFiles(int count, const char** paths) {
    if (count <= 0 || paths == nullptr) {
        return;
    }
    for (int index = 0; index < count; ++index) {
        const std::filesystem::path candidate = std::filesystem::u8path(paths[index]);
        if (findImporter(candidate) != nullptr) {
            droppedModelPath_ = candidate;
            statusMessage_ = "Dropped " + candidate.filename().string() + "; queued for loading.";
            return;
        }
    }
    const std::filesystem::path candidate = std::filesystem::u8path(paths[0]);
    droppedModelPath_ = candidate;
    statusMessage_ = "Dropped file is not a supported model: " + candidate.filename().string();
}

const ModelImporter* Application::findImporter(const std::filesystem::path& path) const {
    for (const auto& importer : importers_) {
        if (importer->supports(path)) {
            return importer.get();
        }
    }
    return nullptr;
}

std::filesystem::path Application::resolvePath(const std::filesystem::path& path) const {
    if (path.empty()) {
        throw std::runtime_error("Model path is empty");
    }
    if (std::filesystem::exists(path)) {
        return std::filesystem::absolute(path).lexically_normal();
    }
    const auto fromSource = sourceRoot_ / path;
    if (std::filesystem::exists(fromSource)) {
        return std::filesystem::absolute(fromSource).lexically_normal();
    }
    const auto byFilename = sourceRoot_ / "assets" / "models" / path.filename();
    if (std::filesystem::exists(byFilename)) {
        return std::filesystem::absolute(byFilename).lexically_normal();
    }
    return std::filesystem::absolute(path).lexically_normal();
}

std::filesystem::path Application::nextScreenshotPath() const {
    const std::filesystem::path directory = sourceRoot_ / "screenshots";
    const std::string stem = currentModelPath_.empty() ? "viewport" : currentModelPath_.stem().string();
    for (std::size_t sequence = 1; sequence < 10000U; ++sequence) {
        const std::filesystem::path candidate = directory
            / (stem + "_" + std::to_string(sequence) + ".png");
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return directory / (stem + "_latest.png");
}

void Application::resetObjectTransform() {
    modelPosition_ = glm::vec3(0.0f);
    modelRotationDegrees_ = glm::vec3(0.0f);
    modelScale_ = 1.0f;
}
