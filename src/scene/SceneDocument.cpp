#include "scene/SceneDocument.h"

#include <cmath>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

// Assimp also embeds RapidJSON, but builds it with its own configuration. Keep
// the scene-document parser in a private namespace so MinGW cannot merge the
// two libraries' inline COMDAT symbols and corrupt RapidJSON calls at runtime.
#define RAPIDJSON_NAMESPACE myrenderer_scene_json
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#undef RAPIDJSON_NAMESPACE
#undef RAPIDJSON_NAMESPACE_BEGIN
#undef RAPIDJSON_NAMESPACE_END

namespace scene_json = myrenderer_scene_json;

namespace {

using Writer = scene_json::PrettyWriter<scene_json::StringBuffer>;

void writeVec3(Writer& writer, const glm::vec3& value) {
    writer.StartArray();
    writer.Double(value.x); writer.Double(value.y); writer.Double(value.z);
    writer.EndArray();
}

void writeMatrix(Writer& writer, const glm::mat4& value) {
    writer.StartArray();
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) writer.Double(value[column][row]);
    }
    writer.EndArray();
}

void writeRendererSettings(Writer& writer, const RendererSettings& settings) {
    writer.StartObject();
#define WRITE_FLOAT(field) writer.Key(#field); writer.Double(settings.field)
#define WRITE_INT(field) writer.Key(#field); writer.Int(settings.field)
#define WRITE_BOOL(field) writer.Key(#field); writer.Bool(settings.field)
#define WRITE_VEC3(field) writer.Key(#field); writeVec3(writer, settings.field)
    WRITE_VEC3(backgroundColor); WRITE_VEC3(baseColor); WRITE_VEC3(lightDirection);
    WRITE_FLOAT(ambientStrength); WRITE_FLOAT(diffuseStrength); WRITE_FLOAT(specularStrength);
    WRITE_FLOAT(shininess); WRITE_INT(msaaSamples);
    writer.Key("renderPath"); writer.Int(static_cast<int>(settings.renderPath));
    writer.Key("shadingMode"); writer.Int(static_cast<int>(settings.shadingMode));
    writer.Key("gBufferDebugView"); writer.Int(static_cast<int>(settings.gBufferDebugView));
    WRITE_BOOL(wireframe); WRITE_BOOL(cullBackFaces); WRITE_BOOL(normalMapping);
    WRITE_BOOL(showGrid); WRITE_BOOL(showAxes); WRITE_BOOL(pbrEnabled); WRITE_BOOL(iblEnabled);
    WRITE_INT(stylizedBandCount); WRITE_FLOAT(stylizedBandSoftness);
    WRITE_FLOAT(stylizedSpecularSize); WRITE_FLOAT(stylizedSpecularSoftness);
    WRITE_FLOAT(stylizedRimWidth); WRITE_FLOAT(stylizedRimSoftness);
    WRITE_FLOAT(stylizedRimIntensity); WRITE_VEC3(stylizedShadowTint);
    WRITE_VEC3(stylizedRimColor);
    WRITE_BOOL(stylizedOutlineEnabled); WRITE_FLOAT(stylizedOutlineWidth);
    WRITE_FLOAT(stylizedOutlineDepthThreshold); WRITE_FLOAT(stylizedOutlineNormalThreshold);
    WRITE_VEC3(stylizedOutlineColor);
    WRITE_BOOL(shadowsEnabled); WRITE_BOOL(coloredTransmissionShadowsEnabled);
    WRITE_BOOL(causticsEnabled);
    writer.Key("causticsMode"); writer.Int(static_cast<int>(settings.causticsMode));
    WRITE_FLOAT(causticsStrength); WRITE_FLOAT(causticsScale); WRITE_VEC3(causticsDirection);
    WRITE_FLOAT(causticsSharpness); WRITE_BOOL(causticsAnimated);
    WRITE_FLOAT(causticsReceiverPlaneY); WRITE_BOOL(transmissionEnabled);
    WRITE_BOOL(skyboxEnabled); WRITE_BOOL(toneMapping); WRITE_BOOL(bloom);
    WRITE_BOOL(showPrismIncidentBeam); WRITE_FLOAT(environmentIntensity);
    WRITE_FLOAT(refractionScale); WRITE_INT(refractionSteps); WRITE_FLOAT(volumeThicknessScale);
    WRITE_BOOL(geometricThicknessEnabled); WRITE_BOOL(twoInterfaceRefractionEnabled);
    WRITE_BOOL(volumeGlassOverrideEnabled); WRITE_FLOAT(volumeGlassTransmission);
    WRITE_FLOAT(volumeGlassRoughness); WRITE_VEC3(volumeGlassAttenuationColor);
    WRITE_FLOAT(volumeGlassAttenuationDistance); WRITE_BOOL(dispersionEnabled);
    WRITE_FLOAT(dispersionStrength);
    writer.Key("glassDebugView"); writer.Int(static_cast<int>(settings.glassDebugView));
    WRITE_FLOAT(exposure); WRITE_FLOAT(bloomThreshold); WRITE_FLOAT(bloomIntensity);
    WRITE_FLOAT(prismBeamOutputLength); WRITE_FLOAT(prismBeamWidth);
    WRITE_FLOAT(prismBeamIntensity); WRITE_FLOAT(prismBeamEdgeSoftness);
    WRITE_FLOAT(prismBeamBloomContribution); WRITE_FLOAT(indexOfRefractionOverride);
    WRITE_BOOL(showPrismOpticalPathDebug); WRITE_BOOL(instanceOptimizationEnabled);
    WRITE_BOOL(frustumCullingEnabled); WRITE_BOOL(lodSelectionEnabled);
    WRITE_FLOAT(lodMediumThresholdPixels); WRITE_FLOAT(lodHighThresholdPixels);
    WRITE_BOOL(ssaoEnabled); WRITE_FLOAT(ssaoRadius); WRITE_FLOAT(ssaoBias); WRITE_FLOAT(ssaoStrength);
    WRITE_BOOL(temporalAaEnabled); WRITE_FLOAT(temporalHistoryWeight);
    WRITE_INT(temporalDebugView); WRITE_INT(skinningDebugView); WRITE_BOOL(shaderHotReloadEnabled);
    writer.Key("localLights"); writer.StartArray();
    for (const LocalLight& light : settings.localLights) {
        writer.StartObject();
        writer.Key("type"); writer.Int(static_cast<int>(light.type));
        writer.Key("position"); writeVec3(writer, light.position);
        writer.Key("radius"); writer.Double(light.radius);
        writer.Key("color"); writeVec3(writer, light.color);
        writer.Key("intensity"); writer.Double(light.intensity);
        writer.Key("direction"); writeVec3(writer, light.direction);
        writer.Key("outerConeCosine"); writer.Double(light.outerConeCosine);
        writer.EndObject();
    }
    writer.EndArray();
#undef WRITE_FLOAT
#undef WRITE_INT
#undef WRITE_BOOL
#undef WRITE_VEC3
    writer.EndObject();
}

const scene_json::Value* optionalMember(
    const scene_json::Value& object,
    const char* name
) {
    if (!object.IsObject()) throw std::runtime_error("Expected a JSON object");
    const auto found = object.FindMember(name);
    return found == object.MemberEnd() ? nullptr : &found->value;
}

float readFloat(const scene_json::Value& object, const char* name, float fallback) {
    const scene_json::Value* value = optionalMember(object, name);
    if (value == nullptr) return fallback;
    if (!value->IsNumber() || !std::isfinite(value->GetDouble())) {
        throw std::runtime_error(std::string("Expected finite number for '") + name + "'");
    }
    return static_cast<float>(value->GetDouble());
}

int readInt(const scene_json::Value& object, const char* name, int fallback) {
    const scene_json::Value* value = optionalMember(object, name);
    if (value == nullptr) return fallback;
    if (!value->IsInt()) throw std::runtime_error(std::string("Expected integer for '") + name + "'");
    return value->GetInt();
}

bool readBool(const scene_json::Value& object, const char* name, bool fallback) {
    const scene_json::Value* value = optionalMember(object, name);
    if (value == nullptr) return fallback;
    if (!value->IsBool()) throw std::runtime_error(std::string("Expected boolean for '") + name + "'");
    return value->GetBool();
}

std::string readString(
    const scene_json::Value& object,
    const char* name,
    std::string fallback = {}
) {
    const scene_json::Value* value = optionalMember(object, name);
    if (value == nullptr) return fallback;
    if (!value->IsString()) throw std::runtime_error(std::string("Expected string for '") + name + "'");
    return std::string(value->GetString(), value->GetStringLength());
}

glm::vec3 readVec3(
    const scene_json::Value& object,
    const char* name,
    const glm::vec3& fallback
) {
    const scene_json::Value* value = optionalMember(object, name);
    if (value == nullptr) return fallback;
    if (!value->IsArray() || value->Size() != 3U) {
        throw std::runtime_error(std::string("Expected three-number array for '") + name + "'");
    }
    glm::vec3 result;
    for (scene_json::SizeType index = 0; index < 3U; ++index) {
        if (!(*value)[index].IsNumber() || !std::isfinite((*value)[index].GetDouble())) {
            throw std::runtime_error(std::string("Expected finite vector for '") + name + "'");
        }
        result[static_cast<int>(index)] = static_cast<float>((*value)[index].GetDouble());
    }
    return result;
}

glm::mat4 readMatrix(
    const scene_json::Value& object,
    const char* name,
    const glm::mat4& fallback
) {
    const scene_json::Value* value = optionalMember(object, name);
    if (value == nullptr) return fallback;
    if (!value->IsArray() || value->Size() != 16U) {
        throw std::runtime_error(std::string("Expected 16-number matrix for '") + name + "'");
    }
    glm::mat4 result(1.0f);
    for (scene_json::SizeType index = 0; index < 16U; ++index) {
        if (!(*value)[index].IsNumber() || !std::isfinite((*value)[index].GetDouble())) {
            throw std::runtime_error(std::string("Expected finite matrix for '") + name + "'");
        }
        result[static_cast<int>(index / 4U)][static_cast<int>(index % 4U)] =
            static_cast<float>((*value)[index].GetDouble());
    }
    return result;
}

void readRendererSettings(const scene_json::Value& value, RendererSettings& settings) {
#define READ_FLOAT(field) settings.field = readFloat(value, #field, settings.field)
#define READ_INT(field) settings.field = readInt(value, #field, settings.field)
#define READ_BOOL(field) settings.field = readBool(value, #field, settings.field)
#define READ_VEC3(field) settings.field = readVec3(value, #field, settings.field)
    READ_VEC3(backgroundColor); READ_VEC3(baseColor); READ_VEC3(lightDirection);
    READ_FLOAT(ambientStrength); READ_FLOAT(diffuseStrength); READ_FLOAT(specularStrength);
    READ_FLOAT(shininess); READ_INT(msaaSamples);
    settings.renderPath = static_cast<RenderPath>(readInt(value, "renderPath", static_cast<int>(settings.renderPath)));
    settings.shadingMode = static_cast<ShadingMode>(readInt(value, "shadingMode", static_cast<int>(settings.shadingMode)));
    settings.gBufferDebugView = static_cast<GBufferDebugView>(readInt(value, "gBufferDebugView", static_cast<int>(settings.gBufferDebugView)));
    READ_BOOL(wireframe); READ_BOOL(cullBackFaces); READ_BOOL(normalMapping);
    READ_BOOL(showGrid); READ_BOOL(showAxes); READ_BOOL(pbrEnabled); READ_BOOL(iblEnabled);
    READ_INT(stylizedBandCount); READ_FLOAT(stylizedBandSoftness);
    READ_FLOAT(stylizedSpecularSize); READ_FLOAT(stylizedSpecularSoftness);
    READ_FLOAT(stylizedRimWidth); READ_FLOAT(stylizedRimSoftness);
    READ_FLOAT(stylizedRimIntensity); READ_VEC3(stylizedShadowTint);
    READ_VEC3(stylizedRimColor);
    READ_BOOL(stylizedOutlineEnabled); READ_FLOAT(stylizedOutlineWidth);
    READ_FLOAT(stylizedOutlineDepthThreshold); READ_FLOAT(stylizedOutlineNormalThreshold);
    READ_VEC3(stylizedOutlineColor);
    READ_BOOL(shadowsEnabled); READ_BOOL(coloredTransmissionShadowsEnabled); READ_BOOL(causticsEnabled);
    settings.causticsMode = static_cast<CausticsMode>(readInt(value, "causticsMode", static_cast<int>(settings.causticsMode)));
    READ_FLOAT(causticsStrength); READ_FLOAT(causticsScale); READ_VEC3(causticsDirection);
    READ_FLOAT(causticsSharpness); READ_BOOL(causticsAnimated); READ_FLOAT(causticsReceiverPlaneY);
    READ_BOOL(transmissionEnabled); READ_BOOL(skyboxEnabled); READ_BOOL(toneMapping); READ_BOOL(bloom);
    READ_BOOL(showPrismIncidentBeam); READ_FLOAT(environmentIntensity); READ_FLOAT(refractionScale);
    READ_INT(refractionSteps); READ_FLOAT(volumeThicknessScale); READ_BOOL(geometricThicknessEnabled);
    READ_BOOL(twoInterfaceRefractionEnabled); READ_BOOL(volumeGlassOverrideEnabled);
    READ_FLOAT(volumeGlassTransmission); READ_FLOAT(volumeGlassRoughness);
    READ_VEC3(volumeGlassAttenuationColor); READ_FLOAT(volumeGlassAttenuationDistance);
    READ_BOOL(dispersionEnabled); READ_FLOAT(dispersionStrength);
    settings.glassDebugView = static_cast<GlassDebugView>(readInt(value, "glassDebugView", static_cast<int>(settings.glassDebugView)));
    READ_FLOAT(exposure); READ_FLOAT(bloomThreshold); READ_FLOAT(bloomIntensity);
    READ_FLOAT(prismBeamOutputLength); READ_FLOAT(prismBeamWidth); READ_FLOAT(prismBeamIntensity);
    READ_FLOAT(prismBeamEdgeSoftness); READ_FLOAT(prismBeamBloomContribution);
    READ_FLOAT(indexOfRefractionOverride); READ_BOOL(showPrismOpticalPathDebug);
    READ_BOOL(instanceOptimizationEnabled); READ_BOOL(frustumCullingEnabled);
    READ_BOOL(lodSelectionEnabled); READ_FLOAT(lodMediumThresholdPixels);
    READ_FLOAT(lodHighThresholdPixels); READ_BOOL(ssaoEnabled); READ_FLOAT(ssaoRadius);
    READ_FLOAT(ssaoBias); READ_FLOAT(ssaoStrength); READ_BOOL(temporalAaEnabled);
    READ_FLOAT(temporalHistoryWeight); READ_INT(temporalDebugView); READ_INT(skinningDebugView);
    READ_BOOL(shaderHotReloadEnabled);
    if (const scene_json::Value* lights = optionalMember(value, "localLights")) {
        if (!lights->IsArray()) throw std::runtime_error("Expected array for 'localLights'");
        settings.localLights.clear();
        settings.localLights.reserve(lights->Size());
        for (const scene_json::Value& item : lights->GetArray()) {
            if (!item.IsObject()) throw std::runtime_error("Expected local light object");
            LocalLight light;
            light.type = static_cast<LocalLightType>(readInt(item, "type", static_cast<int>(light.type)));
            light.position = readVec3(item, "position", light.position);
            light.radius = readFloat(item, "radius", light.radius);
            light.color = readVec3(item, "color", light.color);
            light.intensity = readFloat(item, "intensity", light.intensity);
            light.direction = readVec3(item, "direction", light.direction);
            light.outerConeCosine = readFloat(item, "outerConeCosine", light.outerConeCosine);
            settings.localLights.push_back(light);
        }
    }
#undef READ_FLOAT
#undef READ_INT
#undef READ_BOOL
#undef READ_VEC3
}

void validateDocument(const SceneDocument& document) {
    std::unordered_map<SceneEntityId, SceneEntityId> parents;
    for (const SceneDocumentEntity& entity : document.entities) {
        if (entity.id == invalidSceneEntityId || !parents.emplace(entity.id, entity.parent).second) {
            throw std::runtime_error("Scene entity IDs must be unique non-zero values");
        }
    }
    for (const auto& pair : parents) {
        if (pair.second != invalidSceneEntityId && parents.count(pair.second) == 0U) {
            throw std::runtime_error("Scene entity references a missing parent");
        }
        std::unordered_set<SceneEntityId> visited;
        SceneEntityId current = pair.first;
        while (current != invalidSceneEntityId) {
            if (!visited.insert(current).second) throw std::runtime_error("Scene entity hierarchy contains a cycle");
            current = parents.at(current);
        }
    }
}

} // namespace

bool saveSceneDocument(
    const std::filesystem::path& path,
    const SceneDocument& document,
    std::string& error
) {
    error.clear();
    try {
        validateDocument(document);
        scene_json::StringBuffer buffer;
        Writer writer(buffer);
        writer.SetIndent(' ', 2);
        writer.StartObject();
        writer.Key("format"); writer.String("MyRendererScene");
        writer.Key("version"); writer.Int(SceneDocument::currentVersion);
        writer.Key("camera"); writer.StartObject();
        writer.Key("target"); writeVec3(writer, document.camera.target);
        writer.Key("yawDegrees"); writer.Double(document.camera.yawDegrees);
        writer.Key("pitchDegrees"); writer.Double(document.camera.pitchDegrees);
        writer.Key("distance"); writer.Double(document.camera.distance);
        writer.Key("fieldOfViewDegrees"); writer.Double(document.camera.fieldOfViewDegrees);
        writer.EndObject();
        writer.Key("renderer"); writeRendererSettings(writer, document.renderer);
        writer.Key("playback"); writer.StartObject();
        writer.Key("animationEnabled"); writer.Bool(document.playback.animationEnabled);
        writer.Key("animationPlaying"); writer.Bool(document.playback.animationPlaying);
        writer.Key("animationTimeSeconds"); writer.Double(document.playback.animationTimeSeconds);
        writer.Key("animationSpeed"); writer.Double(document.playback.animationSpeed);
        writer.Key("animationClipIndex"); writer.Uint64(document.playback.animationClipIndex);
        writer.Key("prismEnabled"); writer.Bool(document.playback.prismEnabled);
        writer.Key("prismCameraLocked"); writer.Bool(document.playback.prismCameraLocked);
        writer.Key("prismPreset"); writer.Int(static_cast<int>(document.playback.prismPreset));
        writer.Key("prismParameters"); writer.StartObject();
        writer.Key("beamAngleDegrees"); writer.Double(document.playback.prismParameters.beamAngleDegrees);
        writer.Key("centralIndexOfRefraction"); writer.Double(document.playback.prismParameters.centralIndexOfRefraction);
        writer.Key("dispersion"); writer.Double(document.playback.prismParameters.dispersion);
        writer.Key("spectralSampleCount"); writer.Int(document.playback.prismParameters.spectralSampleCount);
        writer.Key("spectrumMode"); writer.Int(static_cast<int>(document.playback.prismParameters.spectrumMode));
        writer.Key("whitePointKelvin"); writer.Double(document.playback.prismParameters.whitePointKelvin);
        writer.Key("attenuationDistance"); writer.Double(document.playback.prismParameters.attenuationDistance);
        writer.Key("attenuationColor"); writeVec3(writer, document.playback.prismParameters.attenuationColor);
        writer.EndObject();
        writer.EndObject();
        writer.Key("entities"); writer.StartArray();
        for (const SceneDocumentEntity& entity : document.entities) {
            writer.StartObject();
            writer.Key("id"); writer.Uint64(entity.id);
            writer.Key("name"); writer.String(entity.name.c_str(), static_cast<scene_json::SizeType>(entity.name.size()));
            writer.Key("parent"); writer.Uint64(entity.parent);
            writer.Key("model");
            const std::string relative = makeSceneRelativeResource(entity.modelResource, path);
            writer.String(relative.c_str(), static_cast<scene_json::SizeType>(relative.size()));
            writer.Key("transform"); writer.StartObject();
            writer.Key("translation"); writeVec3(writer, entity.transform.translation);
            writer.Key("rotationDegrees"); writeVec3(writer, entity.transform.rotationDegrees);
            writer.Key("scale"); writeVec3(writer, entity.transform.scale);
            writer.Key("assetMatrix"); writeMatrix(writer, entity.transform.assetTransform);
            writer.EndObject();
            writer.Key("visible"); writer.Bool(entity.visible);
            writer.Key("tint"); writeVec3(writer, entity.tint);
            writer.Key("castsShadow"); writer.Bool(entity.castsShadow);
            writer.Key("instanceCandidate"); writer.Bool(entity.instanceCandidate);
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();

        std::error_code filesystemError;
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), filesystemError);
        if (filesystemError) throw std::runtime_error("Cannot create scene directory: " + filesystemError.message());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("Cannot open scene file for writing");
        stream.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
        if (!stream) throw std::runtime_error("Failed while writing scene file");
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool loadSceneDocument(
    const std::filesystem::path& path,
    SceneDocument& document,
    std::string& error
) {
    error.clear();
    try {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("Cannot open scene file");
        const std::string json(
            (std::istreambuf_iterator<char>(stream)),
            std::istreambuf_iterator<char>()
        );
        scene_json::Document root;
        root.Parse(json.data(), json.size());
        if (root.HasParseError()) {
            throw std::runtime_error(
                std::string("JSON parse error: ") + scene_json::GetParseError_En(root.GetParseError())
            );
        }
        if (!root.IsObject() || readString(root, "format") != "MyRendererScene") {
            throw std::runtime_error("Not a MyRenderer scene file");
        }
        const int version = readInt(root, "version", 0);
        if (version < 1 || version > SceneDocument::currentVersion) {
            throw std::runtime_error("Unsupported scene version " + std::to_string(version));
        }

        SceneDocument loaded;
        if (const scene_json::Value* camera = optionalMember(root, "camera")) {
            loaded.camera.target = readVec3(*camera, "target", loaded.camera.target);
            loaded.camera.yawDegrees = readFloat(*camera, "yawDegrees", loaded.camera.yawDegrees);
            loaded.camera.pitchDegrees = readFloat(*camera, "pitchDegrees", loaded.camera.pitchDegrees);
            loaded.camera.distance = readFloat(*camera, "distance", loaded.camera.distance);
            loaded.camera.fieldOfViewDegrees = readFloat(*camera, "fieldOfViewDegrees", loaded.camera.fieldOfViewDegrees);
        }
        if (const scene_json::Value* renderer = optionalMember(root, "renderer")) {
            readRendererSettings(*renderer, loaded.renderer);
        }
        if (const scene_json::Value* playback = optionalMember(root, "playback")) {
            loaded.playback.animationEnabled = readBool(*playback, "animationEnabled", loaded.playback.animationEnabled);
            loaded.playback.animationPlaying = readBool(*playback, "animationPlaying", loaded.playback.animationPlaying);
            loaded.playback.animationTimeSeconds = readFloat(*playback, "animationTimeSeconds", loaded.playback.animationTimeSeconds);
            loaded.playback.animationSpeed = readFloat(*playback, "animationSpeed", loaded.playback.animationSpeed);
            if (const scene_json::Value* clip = optionalMember(*playback, "animationClipIndex")) {
                if (!clip->IsUint64()) throw std::runtime_error("Expected unsigned animationClipIndex");
                loaded.playback.animationClipIndex = static_cast<std::size_t>(clip->GetUint64());
            }
            loaded.playback.prismEnabled = readBool(*playback, "prismEnabled", loaded.playback.prismEnabled);
            loaded.playback.prismCameraLocked = readBool(*playback, "prismCameraLocked", loaded.playback.prismCameraLocked);
            loaded.playback.prismPreset = static_cast<PrismOpticalPreset>(readInt(*playback, "prismPreset", static_cast<int>(loaded.playback.prismPreset)));
            if (const scene_json::Value* prism = optionalMember(*playback, "prismParameters")) {
                loaded.playback.prismParameters.beamAngleDegrees = readFloat(*prism, "beamAngleDegrees", loaded.playback.prismParameters.beamAngleDegrees);
                loaded.playback.prismParameters.centralIndexOfRefraction = readFloat(*prism, "centralIndexOfRefraction", loaded.playback.prismParameters.centralIndexOfRefraction);
                loaded.playback.prismParameters.dispersion = readFloat(*prism, "dispersion", loaded.playback.prismParameters.dispersion);
                loaded.playback.prismParameters.spectralSampleCount = readInt(*prism, "spectralSampleCount", loaded.playback.prismParameters.spectralSampleCount);
                loaded.playback.prismParameters.spectrumMode = static_cast<PrismSpectrumMode>(readInt(*prism, "spectrumMode", static_cast<int>(loaded.playback.prismParameters.spectrumMode)));
                loaded.playback.prismParameters.whitePointKelvin = readFloat(*prism, "whitePointKelvin", loaded.playback.prismParameters.whitePointKelvin);
                loaded.playback.prismParameters.attenuationDistance = readFloat(*prism, "attenuationDistance", loaded.playback.prismParameters.attenuationDistance);
                loaded.playback.prismParameters.attenuationColor = readVec3(*prism, "attenuationColor", loaded.playback.prismParameters.attenuationColor);
            }
        }
        const scene_json::Value* entities = optionalMember(root, "entities");
        if (entities == nullptr || !entities->IsArray()) throw std::runtime_error("Scene requires an entities array");
        loaded.entities.reserve(entities->Size());
        for (const scene_json::Value& item : entities->GetArray()) {
            if (!item.IsObject()) throw std::runtime_error("Expected scene entity object");
            const scene_json::Value* id = optionalMember(item, "id");
            const scene_json::Value* parent = optionalMember(item, "parent");
            if (id == nullptr || !id->IsUint64()) throw std::runtime_error("Entity requires unsigned id");
            if (parent != nullptr && !parent->IsUint64()) throw std::runtime_error("Entity parent must be unsigned");
            SceneDocumentEntity entity;
            entity.id = id->GetUint64();
            entity.parent = parent == nullptr ? invalidSceneEntityId : parent->GetUint64();
            entity.name = readString(item, "name", "Entity");
            entity.modelResource = readString(item, "model");
            if (const scene_json::Value* transform = optionalMember(item, "transform")) {
                entity.transform.translation = readVec3(*transform, "translation", entity.transform.translation);
                entity.transform.rotationDegrees = readVec3(*transform, "rotationDegrees", entity.transform.rotationDegrees);
                entity.transform.scale = readVec3(*transform, "scale", entity.transform.scale);
                entity.transform.assetTransform = readMatrix(*transform, "assetMatrix", entity.transform.assetTransform);
            }
            entity.visible = readBool(item, "visible", entity.visible);
            entity.tint = readVec3(item, "tint", entity.tint);
            entity.castsShadow = readBool(item, "castsShadow", entity.castsShadow);
            entity.instanceCandidate = readBool(item, "instanceCandidate", entity.instanceCandidate);
            loaded.entities.push_back(std::move(entity));
        }
        validateDocument(loaded);
        document = std::move(loaded);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

std::string makeSceneRelativeResource(
    const std::string& resource,
    const std::filesystem::path& scenePath
) {
    if (resource.empty() || resource.rfind("builtin:", 0U) == 0U) return resource;
    const std::filesystem::path value = std::filesystem::u8path(resource);
    if (!value.is_absolute()) return value.generic_u8string();
    const std::filesystem::path relative = value.lexically_normal().lexically_relative(
        scenePath.parent_path().lexically_normal()
    );
    return relative.empty() ? value.generic_u8string() : relative.generic_u8string();
}

std::filesystem::path resolveSceneResource(
    const std::string& resource,
    const std::filesystem::path& scenePath
) {
    if (resource.empty() || resource.rfind("builtin:", 0U) == 0U) return {};
    const std::filesystem::path value = std::filesystem::u8path(resource);
    if (value.is_absolute()) return value.lexically_normal();
    return std::filesystem::absolute(scenePath.parent_path() / value).lexically_normal();
}
