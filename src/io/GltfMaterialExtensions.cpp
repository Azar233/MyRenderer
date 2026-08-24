#include "io/GltfMaterialExtensions.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#define RAPIDJSON_NAMESPACE myrenderer_rapidjson
#include <rapidjson/document.h>
#undef RAPIDJSON_NAMESPACE

namespace {

constexpr std::uint32_t glbMagic = 0x46546c67U;
constexpr std::uint32_t glbJsonChunk = 0x4e4f534aU;

std::uint32_t readLittleEndianU32(const std::vector<char>& bytes, std::size_t offset) {
    if (offset + 4U > bytes.size()) {
        return 0U;
    }
    return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset]))
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 1U])) << 8U)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 2U])) << 16U)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 3U])) << 24U);
}

std::string loadJsonDocument(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    const std::vector<char> bytes{
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()
    };
    if (bytes.empty()) {
        return {};
    }
    if (bytes.size() < 12U || readLittleEndianU32(bytes, 0U) != glbMagic) {
        return std::string(bytes.begin(), bytes.end());
    }

    std::size_t offset = 12U;
    while (offset + 8U <= bytes.size()) {
        const std::uint32_t chunkLength = readLittleEndianU32(bytes, offset);
        const std::uint32_t chunkType = readLittleEndianU32(bytes, offset + 4U);
        offset += 8U;
        if (offset + static_cast<std::size_t>(chunkLength) > bytes.size()) {
            return {};
        }
        if (chunkType == glbJsonChunk) {
            return std::string(bytes.data() + offset, static_cast<std::size_t>(chunkLength));
        }
        offset += static_cast<std::size_t>(chunkLength);
    }
    return {};
}

} // namespace

std::vector<float> loadGltfMaterialDispersion(const std::filesystem::path& path) {
    const std::string json = loadJsonDocument(path);
    if (json.empty()) {
        return {};
    }

    myrenderer_rapidjson::Document document;
    document.Parse(json.data(), json.size());
    if (document.HasParseError() || !document.IsObject()
        || !document.HasMember("materials") || !document["materials"].IsArray()) {
        return {};
    }

    const myrenderer_rapidjson::Value& materials = document["materials"];
    std::vector<float> values(materials.Size(), 0.0f);
    for (myrenderer_rapidjson::SizeType index = 0; index < materials.Size(); ++index) {
        const myrenderer_rapidjson::Value& material = materials[index];
        if (!material.IsObject() || !material.HasMember("extensions")) {
            continue;
        }
        const myrenderer_rapidjson::Value& extensions = material["extensions"];
        if (!extensions.IsObject() || !extensions.HasMember("KHR_materials_dispersion")) {
            continue;
        }
        const myrenderer_rapidjson::Value& extension = extensions["KHR_materials_dispersion"];
        if (extension.IsObject() && extension.HasMember("dispersion")
            && extension["dispersion"].IsNumber()) {
            values[index] = std::max(extension["dispersion"].GetFloat(), 0.0f);
        }
    }
    return values;
}
