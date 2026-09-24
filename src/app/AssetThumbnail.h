#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "app/WorkspaceAssets.h"

struct AssetThumbnail {
    static constexpr int width = 128;
    static constexpr int height = 80;
    std::vector<std::uint8_t> rgba;
    std::string error;
};

bool isPreviewableAsset(WorkspaceAssetCategory category);

// Extends the catalog's stable source key with scene/job references and model sidecars.
// Call on catalog refresh, never from the per-frame UI path.
std::uint64_t assetThumbnailContentKey(const WorkspaceAssetRecord& asset);

// CPU-only. Safe to run away from the OpenGL/context-owning thread.
AssetThumbnail rasterizeAssetThumbnail(const WorkspaceAssetRecord& asset);

// A bounded, versioned on-disk raster cache. Failed generations are not persisted.
AssetThumbnail loadOrGenerateAssetThumbnail(const WorkspaceAssetRecord& asset,
                                            std::uint64_t contentKey,
                                            const std::filesystem::path& cacheDirectory);
