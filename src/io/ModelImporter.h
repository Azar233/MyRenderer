#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "asset/ModelData.h"

enum class ModelDiagnosticScope {
    File,
    Node,
    Mesh,
    Material,
    Texture
};

enum class ModelDiagnosticSeverity {
    Info,
    Warning,
    Error
};

struct ModelDiagnostic {
    ModelDiagnosticScope scope{ModelDiagnosticScope::File};
    ModelDiagnosticSeverity severity{ModelDiagnosticSeverity::Warning};
    std::string context;
    std::string message;
};

struct ModelImportResult {
    ModelData model;
    std::string warnings;
    std::vector<ModelDiagnostic> diagnostics;
    double cpuTimeMilliseconds{0.0};

    void addDiagnostic(
        ModelDiagnosticScope scope,
        ModelDiagnosticSeverity severity,
        std::string context,
        std::string message
    ) {
        if (severity != ModelDiagnosticSeverity::Info) {
            warnings += message;
            if (warnings.empty() || warnings.back() != '\n') {
                warnings.push_back('\n');
            }
        }
        diagnostics.push_back(ModelDiagnostic{
            scope,
            severity,
            std::move(context),
            std::move(message)
        });
    }
};

class ModelImporter {
public:
    virtual ~ModelImporter() = default;

    virtual bool supports(const std::filesystem::path& path) const = 0;
    virtual ModelImportResult load(const std::filesystem::path& path) const = 0;
};
