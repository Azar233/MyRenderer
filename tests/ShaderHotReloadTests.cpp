#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "render/Shader.h"

namespace {

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Could not write " + path.string());
    output << text;
}

void advanceTimestamp(const std::filesystem::path& path, int seconds) {
    std::filesystem::last_write_time(
        path,
        std::filesystem::file_time_type::clock::now() + std::chrono::seconds(seconds)
    );
}

} // namespace

int main() {
    if (glfwInit() != GLFW_TRUE) {
        std::cerr << "GLFW initialization failed\n";
        return 1;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(64, 64, "Shader reload test", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "OpenGL 3.3 context creation failed\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    if (gladLoadGL(glfwGetProcAddress) == 0) {
        std::cerr << "GLAD initialization failed\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory = std::filesystem::temp_directory_path()
        / ("myrenderer-shader-reload-" + std::to_string(serial));
    std::filesystem::create_directories(directory);
    const std::filesystem::path vertexPath = directory / "reload.vert";
    const std::filesystem::path fragmentPath = directory / "reload.frag";

    int result = 0;
    try {
        writeText(vertexPath, R"(#version 330 core
layout(location = 0) in vec3 aPosition;
void main() { gl_Position = vec4(aPosition, 1.0); }
)");
        writeText(fragmentPath, R"(#version 330 core
out vec4 color;
void main() { color = vec4(1.0); }
)");

        {
            Shader shader(vertexPath, fragmentPath);
            shader.use();
            GLint originalProgram = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &originalProgram);

            writeText(fragmentPath, "#version 330 core\nthis is intentionally invalid\n");
            advanceTimestamp(fragmentPath, 2);
            const Shader::ReloadReport failed = Shader::reloadChangedShaders();
            shader.use();
            GLint retainedProgram = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &retainedProgram);
            if (failed.failed != 1U || failed.reloaded != 0U
                || failed.message.empty() || retainedProgram != originalProgram) {
                throw std::runtime_error("Failed shader reload did not retain the previous program");
            }

            writeText(fragmentPath, R"(#version 330 core
out vec4 color;
void main() { color = vec4(0.25, 0.5, 0.75, 1.0); }
)");
            advanceTimestamp(fragmentPath, 4);
            const Shader::ReloadReport recovered = Shader::reloadChangedShaders();
            shader.use();
            GLint replacementProgram = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &replacementProgram);
            if (recovered.reloaded != 1U || recovered.failed != 0U
                || replacementProgram == 0 || replacementProgram == originalProgram) {
                throw std::runtime_error("Corrected shader did not replace the previous program");
            }
        }
        std::cout << "Shader hot reload fail-safe and recovery passed\n";
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        result = 1;
    }

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
