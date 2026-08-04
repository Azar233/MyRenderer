#include "app/Application.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "io/ObjLoader.h"
#include "render/Mesh.h"
#include "render/RenderTarget.h"
#include "render/Shader.h"

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
    discoverModels();

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
        statusMessage_ = "No OBJ model was found in assets/models";
    }

    int smokeTestFrames = std::getenv("MYRENDERER_SMOKE_TEST") == nullptr ? -1 : 5;
    previousFrameTime_ = glfwGetTime();
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }

        const double currentTime = glfwGetTime();
        const float deltaTime = static_cast<float>(std::min(currentTime - previousFrameTime_, 0.1));
        previousFrameTime_ = currentTime;
        if (settings_.autoRotate) {
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
        if (smokeTestFrames > 0 && --smokeTestFrames == 0) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
    }

    shutdown();
    return 0;
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
    glfwSetWindowSizeLimits(window_, 960, 600, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(settings_.vsync ? 1 : 0);

    const int version = gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress));
    if (version == 0) {
        throw std::runtime_error("Failed to load OpenGL functions through GLAD");
    }

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
    shader_ = std::make_unique<Shader>(
        sourceRoot_ / "shaders" / "basic.vert",
        sourceRoot_ / "shaders" / "basic.frag"
    );
    renderTarget_ = std::make_unique<RenderTarget>();
}

void Application::shutdown() {
    if (shutdownComplete_) {
        return;
    }
    shutdownComplete_ = true;

    if (window_ != nullptr) {
        glfwMakeContextCurrent(window_);
        mesh_.reset();
        shader_.reset();
        renderTarget_.reset();
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
        if (ImGui::BeginMenu("Open bundled OBJ")) {
            for (const auto& path : availableModels_) {
                if (ImGui::MenuItem(path.filename().string().c_str())) {
                    loadModel(path);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Reload current", "Ctrl+R", false, !currentModelPath_.empty())) {
            loadModel(currentModelPath_);
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
        ImGui::MenuItem("Wireframe", nullptr, &settings_.wireframe);
        ImGui::MenuItem("Back-face culling", nullptr, &settings_.cullBackFaces);
        ImGui::MenuItem("Auto rotate", nullptr, &settings_.autoRotate);
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
    if (mesh_) {
        const std::string currentMeshLabel = currentModelPath_.filename().string() + "##CurrentMesh";
        ImGui::TreeNodeEx(currentMeshLabel.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::TreePop();
        ImGui::TextDisabled("Vertices: %zu", loadedVertexCount_);
        ImGui::TextDisabled("Triangles: %zu", loadedTriangleCount_);
    } else {
        ImGui::TextDisabled("No model loaded");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("OBJ assets");
    for (const auto& path : availableModels_) {
        const bool selected = !currentModelPath_.empty() && path.filename() == currentModelPath_.filename();
        const std::string assetLabel = path.filename().string() + "##Asset_" + path.string();
        if (ImGui::Selectable(assetLabel.c_str(), selected)) {
            loadModel(path);
        }
    }
    if (availableModels_.empty()) {
        ImGui::TextDisabled("No .obj files found");
    }
    if (unsupportedModelCount_ > 0) {
        ImGui::Spacing();
        ImGui::TextDisabled("%zu non-OBJ model(s) hidden in MVP", unsupportedModelCount_);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Open path");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##ModelPath", modelPathBuffer_.data(), modelPathBuffer_.size());
    if (ImGui::Button("Load OBJ", ImVec2(-1.0f, 0.0f))) {
        loadModel(modelPathBuffer_.data());
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Status");
    ImGui::TextWrapped("%s", statusMessage_.c_str());
    if (!modelWarnings_.empty() && ImGui::TreeNode("Loader warnings")) {
        ImGui::TextWrapped("%s", modelWarnings_.c_str());
        ImGui::TreePop();
    }
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
            ImGui::DragFloat3("Rotation", &modelRotationDegrees_.x, 0.25f, -360.0f, 360.0f, "%.1f deg");
            ImGui::SliderFloat("Scale", &modelScale_, 0.1f, 4.0f, "%.2f");
            ImGui::Checkbox("Auto rotate", &settings_.autoRotate);
            if (ImGui::Button("Reset transform", ImVec2(-1.0f, 0.0f))) {
                resetObjectTransform();
            }

            ImGui::SeparatorText("Material");
            ImGui::ColorEdit3("Base color", &settings_.baseColor.x);
            ImGui::SliderFloat("Ambient", &settings_.ambientStrength, 0.0f, 1.0f);
            ImGui::SliderFloat("Diffuse", &settings_.diffuseStrength, 0.0f, 2.0f);
            ImGui::SliderFloat("Specular", &settings_.specularStrength, 0.0f, 2.0f);
            ImGui::SliderFloat("Shininess", &settings_.shininess, 1.0f, 256.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Renderer")) {
            ImGui::SeparatorText("Rasterization");
            ImGui::Checkbox("Wireframe", &settings_.wireframe);
            ImGui::Checkbox("Back-face culling", &settings_.cullBackFaces);
            ImGui::ColorEdit3("Background", &settings_.backgroundColor.x);

            ImGui::SeparatorText("Directional light");
            ImGui::DragFloat3("Direction", &settings_.lightDirection.x, 0.01f, -1.0f, 1.0f, "%.2f");

            ImGui::SeparatorText("Camera");
            float fieldOfView = camera_.fieldOfView();
            if (ImGui::SliderFloat("Field of view", &fieldOfView, 15.0f, 90.0f, "%.0f deg")) {
                camera_.setFieldOfView(fieldOfView);
            }
            if (ImGui::Button("Frame model", ImVec2(-1.0f, 0.0f))) {
                camera_.reset();
            }

            ImGui::SeparatorText("Runtime");
            if (ImGui::Checkbox("VSync", &settings_.vsync)) {
                glfwSwapInterval(settings_.vsync ? 1 : 0);
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
        camera_.reset();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("RMB orbit | MMB pan | Wheel zoom");
    ImGui::SetCursorPosY(55.0f);

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const int width = std::max(static_cast<int>(available.x), 1);
    const int height = std::max(static_cast<int>(available.y), 1);
    renderTarget_->resize(width, height);
    renderScene(width, height);

    ImGui::Image(
        static_cast<ImTextureID>(static_cast<std::uintptr_t>(renderTarget_->colorTexture())),
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
    ImGui::End();
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
        ImGui::TextWrapped("A compact OBJ renderer extracted from the rendering concepts of the Dandelion graphics lab.");
        if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Application::renderScene(int width, int height) {
    renderTarget_->bind();
    glViewport(0, 0, width, height);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClearColor(
        settings_.backgroundColor.r,
        settings_.backgroundColor.g,
        settings_.backgroundColor.b,
        1.0f
    );
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (mesh_) {
        if (settings_.cullBackFaces) {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glFrontFace(GL_CCW);
        } else {
            glDisable(GL_CULL_FACE);
        }
        glPolygonMode(GL_FRONT_AND_BACK, settings_.wireframe ? GL_LINE : GL_FILL);

        const glm::mat4 normalization =
            glm::scale(glm::mat4(1.0f), glm::vec3(modelNormalizationScale_))
            * glm::translate(glm::mat4(1.0f), -modelCenter_);
        glm::mat4 model(1.0f);
        model = glm::rotate(model, glm::radians(modelRotationDegrees_.z), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::rotate(model, glm::radians(modelRotationDegrees_.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(modelRotationDegrees_.x), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::scale(model, glm::vec3(modelScale_));
        model *= normalization;

        glm::vec3 lightDirection = settings_.lightDirection;
        if (glm::dot(lightDirection, lightDirection) < 1e-8f) {
            lightDirection = glm::vec3(-0.45f, -0.8f, -0.35f);
        }

        shader_->use();
        shader_->setMat4("uModel", model);
        shader_->setMat4("uView", camera_.viewMatrix());
        shader_->setMat4("uProjection", camera_.projectionMatrix(static_cast<float>(width) / static_cast<float>(height)));
        shader_->setVec3("uBaseColor", settings_.baseColor);
        shader_->setVec3("uLightDirection", glm::normalize(lightDirection));
        shader_->setVec3("uCameraPosition", camera_.position());
        shader_->setFloat("uAmbientStrength", settings_.ambientStrength);
        shader_->setFloat("uDiffuseStrength", settings_.diffuseStrength);
        shader_->setFloat("uSpecularStrength", settings_.specularStrength);
        shader_->setFloat("uShininess", settings_.shininess);
        mesh_->draw();
    }

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_CULL_FACE);
    RenderTarget::unbind();
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
        const std::string extension = lowercase(entry.path().extension().string());
        if (extension == ".obj") {
            availableModels_.push_back(entry.path());
        } else if (extension == ".dae" || extension == ".fbx" || extension == ".gltf" || extension == ".glb") {
            ++unsupportedModelCount_;
        }
    }
    std::sort(availableModels_.begin(), availableModels_.end());
}

bool Application::loadModel(const std::filesystem::path& path) {
    try {
        const auto resolved = resolvePath(path);
        if (lowercase(resolved.extension().string()) != ".obj") {
            throw std::runtime_error("MVP currently supports .obj files only: " + resolved.string());
        }

        ObjLoadResult loaded = ObjLoader::load(resolved);
        auto newMesh = std::make_unique<Mesh>(loaded.mesh);
        const glm::vec3 extent = loaded.mesh.boundsMax - loaded.mesh.boundsMin;
        const float maximumExtent = std::max({extent.x, extent.y, extent.z});
        if (!std::isfinite(maximumExtent) || maximumExtent <= 1e-8f) {
            throw std::runtime_error("Model bounds are empty or degenerate");
        }

        mesh_ = std::move(newMesh);
        currentModelPath_ = resolved;
        loadedVertexCount_ = mesh_->vertexCount();
        loadedTriangleCount_ = mesh_->triangleCount();
        modelCenter_ = 0.5f * (loaded.mesh.boundsMin + loaded.mesh.boundsMax);
        modelNormalizationScale_ = 1.4f / maximumExtent;
        modelWarnings_ = loaded.warnings;
        resetObjectTransform();
        camera_.reset();

        const std::string pathString = currentModelPath_.string();
        std::snprintf(modelPathBuffer_.data(), modelPathBuffer_.size(), "%s", pathString.c_str());
        statusMessage_ = "Loaded " + currentModelPath_.filename().string() + " ("
                       + std::to_string(loadedTriangleCount_) + " triangles)";
        std::cout << statusMessage_ << '\n';
        return true;
    } catch (const std::exception& error) {
        statusMessage_ = std::string("Load failed: ") + error.what();
        std::cerr << statusMessage_ << '\n';
        return false;
    }
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

void Application::resetObjectTransform() {
    modelRotationDegrees_ = glm::vec3(0.0f);
    modelScale_ = 1.0f;
}
