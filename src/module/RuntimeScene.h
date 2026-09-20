#pragma once

#include <cstdint>

#include "scene/Scene.h"

// The scene a module is allowed to mutate.
//
// Preview runs, module failures and Reset must never reach the unsaved edit scene,
// so the runtime side owns an independent copy: `resetFrom` rebuilds it from the
// edit scene, modules write into `scene()`, and nothing is written back
// automatically. Only an explicit apply/bake may copy results into the edit scene
// or a cache.
class RuntimeScene {
public:
    // Rebuilds the copy from the edit scene and returns the new generation. The
    // generation only advances when the copy is actually rebuilt, so a caller can
    // tell "the same runtime input" from "a new one".
    std::uint64_t resetFrom(const Scene& editScene);

    std::uint64_t generation() const { return generation_; }
    Scene& scene() { return scene_; }
    const Scene& scene() const { return scene_; }

    bool dirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }
    void clearDirty() { dirty_ = false; }

    // Deterministic hash of the current runtime content, used as the simulation
    // cache key together with the scene generation.
    std::uint64_t contentHash() const;

private:
    Scene scene_;
    std::uint64_t generation_{0U};
    bool dirty_{false};
};

// Identity of one scene's content: entity id, hierarchy, model resource, local
// transform, tint, visibility and shadow flag. Entities are hashed in entity-id
// order, so the value does not depend on insertion order, and every field is mixed
// sequentially, so a changed transform always changes the hash, which keeps the
// simulation cache conservative (a false "changed" only costs a recompute).
std::uint64_t sceneContentHash(const Scene& scene);
