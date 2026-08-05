#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "asset/ModelData.h"

class Texture2D {
public:
    static std::shared_ptr<Texture2D> fromSource(const TextureData& source);
    static std::shared_ptr<Texture2D> solidColor(
        std::uint8_t red,
        std::uint8_t green,
        std::uint8_t blue,
        std::uint8_t alpha,
        bool srgb
    );
    static std::shared_ptr<Texture2D> missingTexture(bool srgb);

    ~Texture2D();

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    void bind(unsigned int unit = 0U) const;
    unsigned int id() const { return id_; }
    int width() const { return width_; }
    int height() const { return height_; }
    std::size_t estimatedBytes() const;

private:
    Texture2D(const std::uint8_t* rgbaPixels, int width, int height, bool flipVertically, bool srgb);

    unsigned int id_{0};
    int width_{0};
    int height_{0};
};

struct TextureLoadResult {
    std::shared_ptr<Texture2D> texture;
    bool usedFallback{false};
    std::string warning;
};

class TextureCache {
public:
    TextureCache();

    TextureLoadResult load(const TextureData& source);
    const std::shared_ptr<Texture2D>& whiteTexture() const { return whiteTexture_; }
    const std::shared_ptr<Texture2D>& missingTexture() const { return missingTexture_; }
    const std::shared_ptr<Texture2D>& flatNormalTexture() const { return flatNormalTexture_; }
    std::size_t cachedTextureCount() const;

private:
    std::unordered_map<std::string, std::weak_ptr<Texture2D>> textures_;
    std::unordered_set<std::string> fallbackKeys_;
    std::shared_ptr<Texture2D> whiteTexture_;
    std::shared_ptr<Texture2D> missingTexture_;
    std::shared_ptr<Texture2D> flatNormalTexture_;
};
