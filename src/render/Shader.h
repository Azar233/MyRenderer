#pragma once

#include <filesystem>
#include <array>
#include <cstddef>
#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

class Shader {
public:
    struct ReloadReport {
        std::size_t reloaded{0U};
        std::size_t failed{0U};
        std::string message;
    };

    Shader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath);
    Shader(
        const std::filesystem::path& vertexPath,
        const std::filesystem::path& geometryPath,
        const std::filesystem::path& fragmentPath
    );
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    void use() const;
    void setBool(const char* name, bool value) const;
    void setInt(const char* name, int value) const;
    void setFloat(const char* name, float value) const;
    void setVec3(const char* name, const glm::vec3& value) const;
    void setVec4(const char* name, const glm::vec4& value) const;
    void setVec4Array(const char* name, const glm::vec4* values, std::size_t count) const;
    void setMat4(const char* name, const glm::mat4& value) const;
    void setMat4Array(const char* name, const glm::mat4* values, std::size_t count) const;
    static ReloadReport reloadChangedShaders();

private:
    static std::string readFile(const std::filesystem::path& path);
    static unsigned int compile(unsigned int type, const std::string& source, const std::filesystem::path& path);
    int uniformLocation(const char* name) const;
    bool reloadIfChanged(std::string& error);
    void captureWriteTimes();

    unsigned int program_{0};
    std::array<std::filesystem::path, 3> stagePaths_{};
    std::array<std::filesystem::file_time_type, 3> writeTimes_{};
    bool hasGeometryStage_{false};
};
