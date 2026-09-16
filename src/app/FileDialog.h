#pragma once

#include <filesystem>
#include <optional>
#include <string>

std::optional<std::filesystem::path> openModelFileDialog(std::string& error);
std::optional<std::filesystem::path> openSceneFileDialog(std::string& error);
std::optional<std::filesystem::path> saveSceneFileDialog(
    const std::filesystem::path& suggestedPath,
    std::string& error
);
