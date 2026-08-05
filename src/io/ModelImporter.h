#pragma once

#include <filesystem>
#include <string>

#include "asset/ModelData.h"

struct ModelImportResult {
    ModelData model;
    std::string warnings;
};

class ModelImporter {
public:
    virtual ~ModelImporter() = default;

    virtual bool supports(const std::filesystem::path& path) const = 0;
    virtual ModelImportResult load(const std::filesystem::path& path) const = 0;
};
