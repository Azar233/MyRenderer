#pragma once

#include "io/ModelImporter.h"

class AssimpImporter final : public ModelImporter {
public:
    bool supports(const std::filesystem::path& path) const override;
    ModelImportResult load(const std::filesystem::path& path) const override;
};
