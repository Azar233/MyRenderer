#pragma once

#include <filesystem>
#include <optional>
#include <string>

std::optional<std::filesystem::path> openModelFileDialog(std::string& error);
