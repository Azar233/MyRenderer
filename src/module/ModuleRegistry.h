#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "module/SceneModule.h"

// Explicit registry of the statically linked module set.
//
// The registry is the only way an id becomes an instance: nothing scans or parses
// C++ sources, so the Modules panel and Content Browser read `manifests()` and the
// runtime reads `create()`. A missing or duplicate id is a hard, reported error
// instead of a silently missing feature.
class ModuleRegistry {
public:
    using Factory = std::function<std::unique_ptr<ISceneModule>()>;

    // Returns false and reports why when the id is empty or already registered.
    bool add(ModuleManifest manifest, Factory factory, std::string& error);

    std::size_t size() const { return entries_.size(); }
    bool contains(const std::string& id) const;
    // Sorted by module id so panels, reports and caches are deterministic.
    std::vector<ModuleManifest> manifests() const;
    const ModuleManifest* find(const std::string& id) const;

    std::unique_ptr<ISceneModule> create(const std::string& id, std::string& error) const;

private:
    struct Entry {
        ModuleManifest manifest;
        Factory factory;
    };
    std::vector<Entry> entries_;
};

// The module set compiled into `MyRendererModules`. The GUI, the CLI batch runtime
// and repeated runs all resolve the same ids through this one entry point.
ModuleRegistry createBuiltinModuleRegistry();

// Build stamp shared by every manifest, injected by CMake.
const std::string& moduleBuildId();
