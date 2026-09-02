#include "render/Renderer.h"

#include <algorithm>
#include <chrono>
#include <iterator>

#include <glad/gl.h>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>
#include <glm/vector_relational.hpp>

#include "render/Camera.h"
#include "render/CausticsMap.h"
#include "render/DebugGrid.h"
#include "render/EnvironmentMap.h"
#include "render/GBuffer.h"
#include "render/GpuModel.h"
#include "render/OpticalPathDebugRenderer.h"
#include "render/PostProcessor.h"
#include "render/RenderPassSequence.h"
#include "render/RenderTarget.h"
#include "render/SceneDrawList.h"
#include "render/Shader.h"
#include "render/ShadowMap.h"
#include "render/SsaoRenderer.h"
#include "render/SpectralBeamRenderer.h"
#include "render/Texture2D.h"

namespace {

struct InstanceBatch {
    const GpuModel* model{nullptr};
    glm::vec3 tint{1.0f};
    std::size_t lodLevel{0U};
    std::vector<glm::mat4> modelMatrices;
};

bool sameTint(const glm::vec3& left, const glm::vec3& right) {
    return glm::all(glm::equal(left, right));
}

float halton(std::size_t index, unsigned int base) {
    float result = 0.0f;
    float fraction = 1.0f;
    while (index > 0U) {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

float maximumMatrixDifference(const glm::mat4& left, const glm::mat4& right) {
    float difference = 0.0f;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            difference = std::max(difference, std::abs(left[column][row] - right[column][row]));
        }
    }
    return difference;
}

} // namespace

Renderer::Renderer(
    const std::filesystem::path& vertexShaderPath,
    const std::filesystem::path& fragmentShaderPath,
    const std::filesystem::path& debugVertexShaderPath,
    const std::filesystem::path& debugFragmentShaderPath
) : shader_(std::make_unique<Shader>(vertexShaderPath, fragmentShaderPath)),
    causticsMap_(std::make_unique<CausticsMap>(vertexShaderPath.parent_path())),
    debugGrid_(std::make_unique<DebugGrid>(debugVertexShaderPath, debugFragmentShaderPath)),
    environmentMap_(std::make_unique<EnvironmentMap>(
        vertexShaderPath.parent_path() / "fullscreen.vert",
        vertexShaderPath.parent_path() / "skybox.frag"
    )),
    gBuffer_(std::make_unique<GBuffer>()),
    opticalPathDebugRenderer_(std::make_unique<OpticalPathDebugRenderer>(
        debugVertexShaderPath,
        debugFragmentShaderPath
    )),
    shadowMap_(std::make_unique<ShadowMap>()),
    ssaoRenderer_(std::make_unique<SsaoRenderer>(vertexShaderPath.parent_path())),
    spectralBeamRenderer_(std::make_unique<SpectralBeamRenderer>(
        vertexShaderPath.parent_path() / "spectral_beam.vert",
        vertexShaderPath.parent_path() / "spectral_beam.frag"
    )),
    shadowShader_(std::make_unique<Shader>(
        vertexShaderPath.parent_path() / "shadow_depth.vert",
        vertexShaderPath.parent_path() / "shadow_depth.frag"
    )),
    transmissionShadowShader_(std::make_unique<Shader>(
        vertexShaderPath.parent_path() / "transmission_shadow.vert",
        vertexShaderPath.parent_path() / "transmission_shadow.frag"
    )),
    glassThicknessShader_(std::make_unique<Shader>(
        vertexShaderPath.parent_path() / "glass_thickness.vert",
        vertexShaderPath.parent_path() / "glass_thickness.frag"
    )),
    gBufferShader_(std::make_unique<Shader>(
        vertexShaderPath.parent_path() / "gbuffer.vert",
        vertexShaderPath.parent_path() / "gbuffer.frag"
    )),
    deferredLightingShader_(std::make_unique<Shader>(
        vertexShaderPath.parent_path() / "fullscreen.vert",
        vertexShaderPath.parent_path() / "deferred_lighting.frag"
    )),
    postProcessor_(std::make_unique<PostProcessor>(
        vertexShaderPath.parent_path() / "fullscreen.vert",
        vertexShaderPath.parent_path() / "bloom_extract.frag",
        vertexShaderPath.parent_path() / "bloom_blur.frag",
        vertexShaderPath.parent_path() / "postprocess.frag",
        vertexShaderPath.parent_path() / "temporal_aa.frag"
    )),
    renderTarget_(std::make_unique<RenderTarget>()),
    textureCache_(std::make_unique<TextureCache>()) {
    glGenQueries(static_cast<GLsizei>(timingQueries_.size()), timingQueries_.data());
    glGenQueries(static_cast<GLsizei>(beamStartQueries_.size()), beamStartQueries_.data());
    glGenQueries(static_cast<GLsizei>(beamEndQueries_.size()), beamEndQueries_.data());
    glGenQueries(static_cast<GLsizei>(causticsStartQueries_.size()), causticsStartQueries_.data());
    glGenQueries(static_cast<GLsizei>(causticsEndQueries_.size()), causticsEndQueries_.data());
    glGenQueries(static_cast<GLsizei>(passStartQueries_.size()), passStartQueries_.data());
    glGenQueries(static_cast<GLsizei>(passEndQueries_.size()), passEndQueries_.data());
    glGenVertexArrays(1, &fullscreenVertexArray_);
}

Renderer::~Renderer() {
    if (fullscreenVertexArray_ != 0U) glDeleteVertexArrays(1, &fullscreenVertexArray_);
    glDeleteQueries(static_cast<GLsizei>(timingQueries_.size()), timingQueries_.data());
    glDeleteQueries(static_cast<GLsizei>(beamStartQueries_.size()), beamStartQueries_.data());
    glDeleteQueries(static_cast<GLsizei>(beamEndQueries_.size()), beamEndQueries_.data());
    glDeleteQueries(static_cast<GLsizei>(causticsStartQueries_.size()), causticsStartQueries_.data());
    glDeleteQueries(static_cast<GLsizei>(causticsEndQueries_.size()), causticsEndQueries_.data());
    glDeleteQueries(static_cast<GLsizei>(passStartQueries_.size()), passStartQueries_.data());
    glDeleteQueries(static_cast<GLsizei>(passEndQueries_.size()), passEndQueries_.data());
}

void Renderer::render(
    const std::vector<RenderItem>& renderItems,
    const Camera& camera,
    const RendererSettings& settings,
    int width,
    int height
) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (settings.shaderHotReloadEnabled && shaderReloadPollFrame_++ % 15U == 0U) {
        const Shader::ReloadReport reload = Shader::reloadChangedShaders();
        if (reload.failed > 0U) {
            shaderReloadFailed_ = true;
            shaderReloadStatus_ = reload.message;
        } else if (reload.reloaded > 0U) {
            shaderReloadFailed_ = false;
            shaderReloadStatus_ = "Reloaded " + std::to_string(reload.reloaded)
                + " shader program(s)";
            previousViewProjectionValid_ = false;
        }
    }
    renderTarget_->resize(width, height, settings.msaaSamples);
    const bool deferredActive = settings.renderPath == RenderPath::Deferred;
    activeRenderPath_ = settings.renderPath;
    const bool gBufferDebugActive = deferredActive
        && settings.gBufferDebugView != GBufferDebugView::Final;
    if (deferredActive) {
        gBuffer_->resize(width, height, settings.msaaSamples);
    } else if (gBuffer_->framebuffer() != 0U) {
        // Keep GUI/benchmark memory comparisons honest after switching back
        // from Deferred instead of retaining inactive MRT allocations.
        gBuffer_->destroy();
    }

    for (std::size_t index = 0; index < timingQueries_.size(); ++index) {
        if (!timingQueryPending_[index]) {
            continue;
        }
        GLint available = GL_FALSE;
        glGetQueryObjectiv(timingQueries_[index], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == GL_TRUE) {
            GLuint64 elapsedNanoseconds = 0U;
            glGetQueryObjectui64v(timingQueries_[index], GL_QUERY_RESULT, &elapsedNanoseconds);
            const double measured = static_cast<double>(elapsedNanoseconds) / 1'000'000.0;
            latestGpuFrameMeasurementMilliseconds_ = measured;
            gpuFrameTimeMilliseconds_ = hasGpuFrameTime_
                ? gpuFrameTimeMilliseconds_ * 0.9 + measured * 0.1
                : measured;
            hasGpuFrameTime_ = true;
            ++gpuFrameMeasurementSerial_;
            timingQueryPending_[index] = false;
        }
    }
    for (std::size_t index = 0; index < beamTimingPending_.size(); ++index) {
        if (!beamTimingPending_[index]) continue;
        GLint startAvailable = GL_FALSE;
        GLint endAvailable = GL_FALSE;
        glGetQueryObjectiv(beamStartQueries_[index], GL_QUERY_RESULT_AVAILABLE, &startAvailable);
        glGetQueryObjectiv(beamEndQueries_[index], GL_QUERY_RESULT_AVAILABLE, &endAvailable);
        if (startAvailable == GL_TRUE && endAvailable == GL_TRUE) {
            GLuint64 startTimestamp = 0U;
            GLuint64 endTimestamp = 0U;
            glGetQueryObjectui64v(beamStartQueries_[index], GL_QUERY_RESULT, &startTimestamp);
            glGetQueryObjectui64v(beamEndQueries_[index], GL_QUERY_RESULT, &endTimestamp);
            const double measured = endTimestamp >= startTimestamp
                ? static_cast<double>(endTimestamp - startTimestamp) / 1'000'000.0
                : 0.0;
            latestPrismBeamMeasurementMilliseconds_ = measured;
            prismBeamGpuTimeMilliseconds_ = hasPrismBeamGpuTime_
                ? prismBeamGpuTimeMilliseconds_ * 0.9 + measured * 0.1
                : measured;
            hasPrismBeamGpuTime_ = true;
            ++prismBeamMeasurementSerial_;
            beamTimingPending_[index] = false;
        }
    }
    for (std::size_t index = 0; index < causticsTimingPending_.size(); ++index) {
        if (!causticsTimingPending_[index]) continue;
        GLint startAvailable = GL_FALSE;
        GLint endAvailable = GL_FALSE;
        glGetQueryObjectiv(causticsStartQueries_[index], GL_QUERY_RESULT_AVAILABLE, &startAvailable);
        glGetQueryObjectiv(causticsEndQueries_[index], GL_QUERY_RESULT_AVAILABLE, &endAvailable);
        if (startAvailable == GL_TRUE && endAvailable == GL_TRUE) {
            GLuint64 startTimestamp = 0U;
            GLuint64 endTimestamp = 0U;
            glGetQueryObjectui64v(causticsStartQueries_[index], GL_QUERY_RESULT, &startTimestamp);
            glGetQueryObjectui64v(causticsEndQueries_[index], GL_QUERY_RESULT, &endTimestamp);
            const double measured = endTimestamp >= startTimestamp
                ? static_cast<double>(endTimestamp - startTimestamp) / 1'000'000.0
                : 0.0;
            latestCausticsMeasurementMilliseconds_ = measured;
            causticsGpuTimeMilliseconds_ = hasCausticsGpuTime_
                ? causticsGpuTimeMilliseconds_ * 0.9 + measured * 0.1
                : measured;
            hasCausticsGpuTime_ = true;
            ++causticsMeasurementSerial_;
            causticsTimingPending_[index] = false;
        }
    }
    for (std::size_t index = 0; index < passTimingPending_.size(); ++index) {
        if (!passTimingPending_[index]) continue;
        GLint startAvailable = GL_FALSE;
        GLint endAvailable = GL_FALSE;
        glGetQueryObjectiv(passStartQueries_[index], GL_QUERY_RESULT_AVAILABLE, &startAvailable);
        glGetQueryObjectiv(passEndQueries_[index], GL_QUERY_RESULT_AVAILABLE, &endAvailable);
        if (startAvailable != GL_TRUE || endAvailable != GL_TRUE) continue;
        GLuint64 startTimestamp = 0U;
        GLuint64 endTimestamp = 0U;
        glGetQueryObjectui64v(passStartQueries_[index], GL_QUERY_RESULT, &startTimestamp);
        glGetQueryObjectui64v(passEndQueries_[index], GL_QUERY_RESULT, &endTimestamp);
        const double measured = endTimestamp >= startTimestamp
            ? static_cast<double>(endTimestamp - startTimestamp) / 1'000'000.0
            : 0.0;
        const std::string& name = passTimingNames_[index];
        auto timing = std::find_if(
            gpuPassTimings_.begin(),
            gpuPassTimings_.end(),
            [&](const GpuPassTiming& candidate) { return candidate.name == name; }
        );
        if (timing == gpuPassTimings_.end()) {
            gpuPassTimings_.push_back(GpuPassTiming{name, measured, measured, 1U});
        } else {
            timing->latestMilliseconds = measured;
            timing->milliseconds = timing->measurementSerial > 0U
                ? timing->milliseconds * 0.9 + measured * 0.1
                : measured;
            ++timing->measurementSerial;
        }
        passTimingPending_[index] = false;
    }

    std::size_t timingQuery = timingQueries_.size();
    for (std::size_t offset = 0; offset < timingQueries_.size(); ++offset) {
        const std::size_t candidate = (nextTimingQuery_ + offset) % timingQueries_.size();
        if (!timingQueryPending_[candidate]) {
            timingQuery = candidate;
            glBeginQuery(GL_TIME_ELAPSED, timingQueries_[candidate]);
            break;
        }
    }
    const glm::mat4 view = camera.viewMatrix();
    glm::mat4 projection = camera.projectionMatrix(
        static_cast<float>(width) / static_cast<float>(height)
    );
    const bool temporalAaActive = settings.temporalAaEnabled && !gBufferDebugActive;
    if (temporalAaActive) {
        const std::size_t sample = temporalFrameIndex_ % 8U + 1U;
        const float jitterX = halton(sample, 2U) - 0.5f;
        const float jitterY = halton(sample, 3U) - 0.5f;
        projection[2][0] += 2.0f * jitterX / static_cast<float>(width);
        projection[2][1] += 2.0f * jitterY / static_cast<float>(height);
    }
    const glm::mat4 currentViewProjection = projection * view;
    const bool resetTemporalHistory = !previousViewProjectionValid_
        || !lastTemporalAaEnabled_
        || width != lastTemporalWidth_
        || height != lastTemporalHeight_
        || (previousViewProjectionValid_
            && maximumMatrixDifference(currentViewProjection, previousViewProjection_) > 0.35f);
    submittedInstanceCount_ = 0U;
    visibleInstanceCount_ = 0U;
    culledInstanceCount_ = 0U;
    lodInstanceCounts_.fill(0U);
    renderedInstanceTriangleCount_ = 0U;
    std::vector<InstanceBatch> instanceBatches;
    const auto instancePreparationStart = std::chrono::steady_clock::now();
    const ViewFrustum viewFrustum = extractViewFrustum(projection * view);
    for (const RenderItem& item : renderItems) {
        if (!item.visible || item.model == nullptr || !item.instanceCandidate) continue;
        ++submittedInstanceCount_;
        std::size_t lodLevel = 0U;
        if (settings.instanceOptimizationEnabled) {
            const BoundingSphere worldBounds = transformBoundingSphere(
                BoundingSphere{item.model->boundsCenter(), item.model->boundsRadius()},
                item.modelMatrix
            );
            if (settings.frustumCullingEnabled
                && !intersectsViewFrustum(viewFrustum, worldBounds)) {
                ++culledInstanceCount_;
                continue;
            }
            if (settings.lodSelectionEnabled) {
                const float projectedRadius = projectedSphereRadiusPixels(
                    worldBounds,
                    camera.position(),
                    glm::radians(camera.fieldOfView()),
                    height
                );
                lodLevel = selectLodLevel(
                    projectedRadius,
                    settings.lodMediumThresholdPixels,
                    settings.lodHighThresholdPixels
                );
            }
            auto batch = std::find_if(
                instanceBatches.begin(),
                instanceBatches.end(),
                [&](const InstanceBatch& candidate) {
                    return candidate.model == item.model
                        && candidate.lodLevel == lodLevel
                        && sameTint(candidate.tint, item.tint);
                }
            );
            if (batch == instanceBatches.end()) {
                instanceBatches.push_back(InstanceBatch{item.model, item.tint, lodLevel, {}});
                batch = std::prev(instanceBatches.end());
            }
            batch->modelMatrices.push_back(item.modelMatrix);
        }
        ++visibleInstanceCount_;
        ++lodInstanceCounts_[lodLevel];
        renderedInstanceTriangleCount_ += item.model->lodTriangleCount(lodLevel);
    }
    instancePreparationMilliseconds_ = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - instancePreparationStart
    ).count();
    glm::vec3 lightDirection = settings.lightDirection;
    if (glm::dot(lightDirection, lightDirection) < 1e-8f) {
        lightDirection = glm::vec3(-0.45f, -0.8f, -0.35f);
    }
    lightDirection = glm::normalize(lightDirection);
    glm::vec3 sceneCenter(0.0f);
    bool hasVisibleItems = false;
    bool hasTransmissiveCasters = false;
    for (const RenderItem& item : renderItems) {
        if (!item.visible || item.model == nullptr) {
            continue;
        }
        if (!hasVisibleItems) {
            sceneCenter = glm::vec3(item.modelMatrix[3]);
            hasVisibleItems = true;
        }
        hasTransmissiveCasters |= item.castsShadow
            && item.model->transmissiveSubmeshCount() > 0U;
    }
    glm::vec3 lightUp(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(lightDirection, lightUp)) > 0.96f) lightUp = glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::mat4 lightView = glm::lookAt(sceneCenter - lightDirection * 6.0f, sceneCenter, lightUp);
    const glm::mat4 lightProjection = glm::ortho(-4.0f, 4.0f, -4.0f, 4.0f, 0.1f, 16.0f);
    const glm::mat4 lightViewProjection = lightProjection * lightView;

    std::vector<TransparentSortEntry> transparentDraws;
    for (std::size_t itemIndex = 0; itemIndex < renderItems.size(); ++itemIndex) {
        const RenderItem& item = renderItems[itemIndex];
        if (!item.visible || item.model == nullptr) {
            continue;
        }
        for (std::size_t submeshIndex = 0;
             submeshIndex < item.model->transparentSubmeshCount();
             ++submeshIndex) {
            const glm::vec3 worldCenter = glm::vec3(
                item.modelMatrix
                * glm::vec4(item.model->transparentSubmeshCenter(submeshIndex), 1.0f)
            );
            transparentDraws.push_back(TransparentSortEntry{
                itemIndex,
                submeshIndex,
                worldCenter
            });
        }
    }
    sortTransparentBackToFront(transparentDraws, camera.position());
    const bool causticsActive = settings.causticsEnabled
        && (settings.causticsMode == CausticsMode::Projector || hasTransmissiveCasters);

    constexpr std::size_t maximumLocalLights = 64U;
    const std::size_t localLightCount = std::min(
        settings.localLights.size(),
        maximumLocalLights
    );
    std::array<glm::vec4, maximumLocalLights> localLightPositionRadius{};
    std::array<glm::vec4, maximumLocalLights> localLightColorIntensity{};
    std::array<glm::vec4, maximumLocalLights> localLightDirectionOuter{};
    for (std::size_t index = 0; index < localLightCount; ++index) {
        const LocalLight& light = settings.localLights[index];
        const float radius = std::max(light.radius, 0.001f);
        localLightPositionRadius[index] = glm::vec4(
            light.position,
            light.type == LocalLightType::Spot ? -radius : radius
        );
        localLightColorIntensity[index] = glm::vec4(
            glm::max(light.color, glm::vec3(0.0f)),
            std::max(light.intensity, 0.0f)
        );
        glm::vec3 direction = light.direction;
        if (glm::dot(direction, direction) < 1e-8f) {
            direction = glm::vec3(0.0f, -1.0f, 0.0f);
        }
        localLightDirectionOuter[index] = glm::vec4(
            glm::normalize(direction),
            std::clamp(light.outerConeCosine, -0.99f, 0.99f)
        );
    }
    const auto bindLocalLights = [&](Shader& targetShader) {
        targetShader.setInt("uLocalLightCount", static_cast<int>(localLightCount));
        if (localLightCount == 0U) return;
        targetShader.setVec4Array(
            "uLocalLightPositionRadius[0]",
            localLightPositionRadius.data(),
            localLightCount
        );
        targetShader.setVec4Array(
            "uLocalLightColorIntensity[0]",
            localLightColorIntensity.data(),
            localLightCount
        );
        targetShader.setVec4Array(
            "uLocalLightDirectionOuter[0]",
            localLightDirectionOuter.data(),
            localLightCount
        );
    };

    const auto bindSceneShader = [&] {
        shader_->use();
        shader_->setMat4("uView", view);
        shader_->setMat4("uProjection", projection);
        shader_->setMat4("uLightViewProjection", lightViewProjection);
        shader_->setVec3("uLightDirection", lightDirection);
        bindLocalLights(*shader_);
        shader_->setVec3("uCameraPosition", camera.position());
        shader_->setFloat("uAmbientStrength", settings.ambientStrength);
        shader_->setFloat("uDiffuseStrength", settings.diffuseStrength);
        shader_->setFloat("uSpecularStrength", settings.specularStrength);
        shader_->setFloat("uShininess", settings.shininess);
        shader_->setBool("uNormalMappingEnabled", settings.normalMapping);
        shader_->setBool("uPbrEnabled", settings.pbrEnabled);
        shader_->setBool("uIblEnabled", settings.iblEnabled);
        shader_->setBool("uShadowsEnabled", settings.shadowsEnabled);
        shader_->setBool(
            "uColoredTransmissionShadowsEnabled",
            settings.coloredTransmissionShadowsEnabled
        );
        shader_->setBool("uCausticsEnabled", causticsActive);
        shader_->setBool("uTransmissionEnabled", settings.transmissionEnabled);
        shader_->setFloat("uRefractionScale", settings.refractionScale);
        shader_->setInt("uRefractionSteps", settings.refractionSteps);
        shader_->setFloat("uVolumeThicknessScale", settings.volumeThicknessScale);
        shader_->setBool("uVolumeGlassOverrideEnabled", settings.volumeGlassOverrideEnabled);
        shader_->setFloat("uVolumeGlassTransmission", settings.volumeGlassTransmission);
        shader_->setFloat("uVolumeGlassRoughness", settings.volumeGlassRoughness);
        shader_->setVec3(
            "uVolumeGlassAttenuationColor",
            settings.volumeGlassAttenuationColor
        );
        shader_->setFloat(
            "uVolumeGlassAttenuationDistance",
            settings.volumeGlassAttenuationDistance
        );
        shader_->setBool("uGeometricThicknessEnabled", settings.geometricThicknessEnabled);
        shader_->setBool(
            "uTwoInterfaceRefractionEnabled",
            settings.twoInterfaceRefractionEnabled
        );
        shader_->setBool("uDispersionEnabled", settings.dispersionEnabled);
        shader_->setFloat("uDispersionStrength", settings.dispersionStrength);
        shader_->setFloat("uIndexOfRefractionOverride", settings.indexOfRefractionOverride);
        shader_->setInt("uGlassDebugView", static_cast<int>(settings.glassDebugView));
        shader_->setFloat(
            "uOpaqueColorMaxMip",
            static_cast<float>(renderTarget_->opaqueColorMaximumMipLevel())
        );
        shader_->setFloat("uEnvironmentIntensity", settings.environmentIntensity);
        shader_->setFloat("uEnvironmentMaxMip", static_cast<float>(environmentMap_->maximumMipLevel()));
        shader_->setInt("uEnvironmentMap", 3);
        shader_->setInt("uShadowMap", 4);
        shader_->setInt("uOpaqueColorTexture", 5);
        shader_->setInt("uSceneDepthTexture", 6);
        shader_->setInt("uGlassFrontfaceDepthTexture", 8);
        shader_->setInt("uGlassBackfaceDepthTexture", 9);
        shader_->setInt("uGlassExitNormalTexture", 10);
        shader_->setInt("uGlassObjectIdTexture", 11);
        shader_->setInt("uIrradianceMap", 12);
        shader_->setInt("uPrefilteredEnvironmentMap", 13);
        shader_->setInt("uBrdfLut", 14);
        shader_->setInt("uCausticsMap", 15);
        shader_->setInt("uTransmissionShadowMap", 16);
        shader_->setBool("uHasGlassBackfaceData", false);
        shader_->setInt("uGlassObjectId", 0);
        environmentMap_->bind(3U);
        environmentMap_->bindIrradiance(12U);
        environmentMap_->bindPrefiltered(13U);
        environmentMap_->bindBrdfLut(14U);
        shadowMap_->bindTexture(4U);
        causticsMap_->bindTexture(15U);
        shadowMap_->bindTransmissionTexture(16U);
    };

    drawCallCount_ = 0U;
    const bool spectralBeamVisible = settings.showPrismIncidentBeam
        && settings.prismOpticalPathValid;
    std::size_t beamTimingQuery = beamTimingPending_.size();
    if (spectralBeamVisible) {
        for (std::size_t offset = 0; offset < beamTimingPending_.size(); ++offset) {
            const std::size_t candidate = (nextBeamTimingQuery_ + offset)
                % beamTimingPending_.size();
            if (!beamTimingPending_[candidate]) {
                beamTimingQuery = candidate;
                break;
            }
        }
    }
    std::size_t causticsTimingQuery = causticsTimingPending_.size();
    if (causticsActive) {
        for (std::size_t offset = 0; offset < causticsTimingPending_.size(); ++offset) {
            const std::size_t candidate = (nextCausticsTimingQuery_ + offset)
                % causticsTimingPending_.size();
            if (!causticsTimingPending_[candidate]) {
                causticsTimingQuery = candidate;
                break;
            }
        }
    }
    RenderPassSequence sequence(width, height);
    if (settings.shadowsEnabled) {
        sequence.add("Shadow map", [&] {
            shadowMap_->bindForWriting();
            glViewport(0, 0, shadowMap_->resolution(), shadowMap_->resolution());
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);
            glClear(GL_DEPTH_BUFFER_BIT);
            shadowShader_->use();
            shadowShader_->setMat4("uLightViewProjection", lightViewProjection);
            for (const RenderItem& item : renderItems) {
                if (!item.visible || !item.castsShadow || item.model == nullptr) {
                    continue;
                }
                shadowShader_->setMat4("uModel", item.modelMatrix);
                item.model->drawDepth(*shadowShader_);
                drawCallCount_ += item.model->opaqueSubmeshCount();
            }
            glCullFace(GL_BACK);
            glDisable(GL_CULL_FACE);
        });
    }
    if (settings.shadowsEnabled && settings.coloredTransmissionShadowsEnabled) {
        sequence.add("Colored transmission shadow", [&] {
            shadowMap_->bindTransmissionForWriting();
            glViewport(0, 0, shadowMap_->resolution(), shadowMap_->resolution());
            const std::array<float, 4> white{1.0f, 1.0f, 1.0f, 1.0f};
            glClearBufferfv(GL_COLOR, 0, white.data());
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glEnable(GL_BLEND);
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_ZERO, GL_SRC_COLOR);
            transmissionShadowShader_->use();
            transmissionShadowShader_->setMat4("uLightViewProjection", lightViewProjection);
            transmissionShadowShader_->setFloat(
                "uVolumeThicknessScale", settings.volumeThicknessScale
            );
            transmissionShadowShader_->setBool(
                "uVolumeGlassOverrideEnabled", settings.volumeGlassOverrideEnabled
            );
            transmissionShadowShader_->setFloat(
                "uVolumeGlassTransmission", settings.volumeGlassTransmission
            );
            transmissionShadowShader_->setVec3(
                "uVolumeGlassAttenuationColor", settings.volumeGlassAttenuationColor
            );
            transmissionShadowShader_->setFloat(
                "uVolumeGlassAttenuationDistance",
                settings.volumeGlassAttenuationDistance
            );
            for (const RenderItem& item : renderItems) {
                if (!item.visible || !item.castsShadow || item.model == nullptr) continue;
                transmissionShadowShader_->setMat4("uModel", item.modelMatrix);
                item.model->drawTransmissive(*transmissionShadowShader_, item.tint);
                drawCallCount_ += item.model->transmissiveSubmeshCount();
            }
            glDisable(GL_BLEND);
        });
    }
    if (causticsActive) {
        sequence.add(
            settings.causticsMode == CausticsMode::Projector
                ? "Caustics projector HDR"
                : "Light-space RGB caustics",
            [&] {
                if (causticsTimingQuery < causticsTimingPending_.size()) {
                    glQueryCounter(causticsStartQueries_[causticsTimingQuery], GL_TIMESTAMP);
                }
                causticsMap_->bindRawForWriting();
                glViewport(0, 0, causticsMap_->resolution(), causticsMap_->resolution());
                const std::array<float, 4> black{0.0f, 0.0f, 0.0f, 0.0f};
                glClearBufferfv(GL_COLOR, 0, black.data());
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_CULL_FACE);
                glEnable(GL_BLEND);
                glBlendEquation(GL_FUNC_ADD);
                glBlendFunc(GL_ONE, GL_ONE);
                if (settings.causticsMode == CausticsMode::Projector) {
                    causticsMap_->drawProjector(
                        settings.causticsStrength,
                        settings.causticsScale,
                        settings.causticsDirection,
                        settings.causticsSharpness,
                        settings.causticsAnimationPhase
                    );
                    ++drawCallCount_;
                } else {
                    Shader& causticsShader = causticsMap_->lightSpaceShader();
                    causticsShader.use();
                    causticsShader.setMat4("uLightViewProjection", lightViewProjection);
                    causticsShader.setVec3("uLightDirection", lightDirection);
                    causticsShader.setFloat(
                        "uReceiverPlaneY", settings.causticsReceiverPlaneY
                    );
                    causticsShader.setFloat("uCausticStrength", settings.causticsStrength);
                    causticsShader.setFloat("uCausticScale", settings.causticsScale);
                    causticsShader.setVec3("uCausticDirection", settings.causticsDirection);
                    causticsShader.setFloat(
                        "uDispersionStrength", settings.dispersionStrength
                    );
                    causticsShader.setBool(
                        "uDispersionEnabled", settings.dispersionEnabled
                    );
                    causticsShader.setFloat(
                        "uIndexOfRefractionOverride", settings.indexOfRefractionOverride
                    );
                    causticsShader.setFloat(
                        "uVolumeThicknessScale", settings.volumeThicknessScale
                    );
                    causticsShader.setBool(
                        "uVolumeGlassOverrideEnabled", settings.volumeGlassOverrideEnabled
                    );
                    causticsShader.setVec3(
                        "uVolumeGlassAttenuationColor",
                        settings.volumeGlassAttenuationColor
                    );
                    causticsShader.setFloat(
                        "uVolumeGlassTransmission", settings.volumeGlassTransmission
                    );
                    causticsShader.setFloat(
                        "uVolumeGlassAttenuationDistance",
                        settings.volumeGlassAttenuationDistance
                    );
                    for (int channel = 0; channel < 3; ++channel) {
                        causticsShader.setInt("uCausticChannel", channel);
                        for (const RenderItem& item : renderItems) {
                            if (!item.visible || !item.castsShadow || item.model == nullptr) continue;
                            causticsShader.setMat4("uModel", item.modelMatrix);
                            item.model->drawTransmissive(causticsShader, item.tint);
                            drawCallCount_ += item.model->transmissiveSubmeshCount();
                        }
                    }
                }
                glDisable(GL_BLEND);
                causticsMap_->filter(settings.causticsSharpness);
                drawCallCount_ += 2U;
                if (causticsTimingQuery < causticsTimingPending_.size()) {
                    glQueryCounter(causticsEndQueries_[causticsTimingQuery], GL_TIMESTAMP);
                    causticsTimingPending_[causticsTimingQuery] = true;
                    nextCausticsTimingQuery_ =
                        (causticsTimingQuery + 1U) % causticsTimingPending_.size();
                }
            }
        );
    }
    if (!deferredActive) sequence.add("Forward opaque HDR scene", [&] {
        renderTarget_->bindOpaqueScene();
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, 0);
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glClearColor(settings.backgroundColor.r, settings.backgroundColor.g, settings.backgroundColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        if (settings.skyboxEnabled) {
            glDisable(GL_DEPTH_TEST);
            environmentMap_->draw(glm::inverse(projection * view), camera.position(), settings.environmentIntensity);
            ++drawCallCount_;
            glEnable(GL_DEPTH_TEST);
        }
        if (settings.showGrid || settings.showAxes) {
        glDisable(GL_CULL_FACE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        debugGrid_->draw(view, projection, settings.showGrid, settings.showAxes);
        drawCallCount_ += (settings.showGrid ? 1U : 0U)
            + (settings.showAxes ? 1U : 0U);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        }

        if (hasVisibleItems) {
            glPolygonMode(GL_FRONT_AND_BACK, settings.wireframe ? GL_LINE : GL_FILL);
            bindSceneShader();
            for (const RenderItem& item : renderItems) {
                if (!item.visible || item.model == nullptr) {
                    continue;
                }
                if (settings.instanceOptimizationEnabled && item.instanceCandidate) continue;
                shader_->setMat4("uModel", item.modelMatrix);
                item.model->drawOpaque(*shader_, item.tint, settings.cullBackFaces);
                drawCallCount_ += item.model->opaqueSubmeshCount();
            }
            for (const InstanceBatch& batch : instanceBatches) {
                batch.model->drawOpaqueInstanced(
                    *shader_,
                    batch.tint,
                    batch.modelMatrices,
                    batch.lodLevel,
                    settings.cullBackFaces
                );
                drawCallCount_ += batch.model->opaqueSubmeshCount();
            }
        }

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_CULL_FACE);
        if (!spectralBeamVisible) {
            renderTarget_->resolveOpaqueScene();
        }
    });
    if (deferredActive) {
        sequence.add("G-buffer geometry", [&] {
            gBuffer_->bindForGeometry();
            glViewport(0, 0, width, height);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            const std::array<float, 4> clearAlbedo{0.0f, 0.0f, 0.0f, 0.0f};
            const std::array<float, 4> clearNormal{0.5f, 0.5f, 1.0f, 0.0f};
            const std::array<float, 4> clearMaterial{0.0f, 1.0f, 0.0f, 0.0f};
            const std::array<float, 4> clearMotion{0.0f, 0.0f, 0.0f, 0.0f};
            glClearBufferfv(GL_COLOR, 0, clearAlbedo.data());
            glClearBufferfv(GL_COLOR, 1, clearNormal.data());
            glClearBufferfv(GL_COLOR, 2, clearMaterial.data());
            glClearBufferfv(GL_COLOR, 3, clearMotion.data());
            glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
            glPolygonMode(GL_FRONT_AND_BACK, settings.wireframe ? GL_LINE : GL_FILL);

            gBufferShader_->use();
            gBufferShader_->setMat4("uView", view);
            gBufferShader_->setMat4("uProjection", projection);
            gBufferShader_->setMat4("uCurrentViewProjection", currentViewProjection);
            gBufferShader_->setMat4(
                "uPreviousViewProjection",
                previousViewProjectionValid_ ? previousViewProjection_ : currentViewProjection
            );
            gBufferShader_->setBool("uNormalMappingEnabled", settings.normalMapping);
            for (const RenderItem& item : renderItems) {
                if (!item.visible || item.model == nullptr) continue;
                if (settings.instanceOptimizationEnabled && item.instanceCandidate) continue;
                gBufferShader_->setMat4("uModel", item.modelMatrix);
                gBufferShader_->setMat4("uPreviousModel", item.previousModelMatrix);
                const bool transformMoved = maximumMatrixDifference(
                    item.modelMatrix,
                    item.previousModelMatrix
                ) > 1.0e-6f;
                gBufferShader_->setBool(
                    "uMotionHistoryValid",
                    item.motionHistoryValid
                        && (transformMoved || item.model->hasSkinningMotion())
                );
                item.model->drawOpaque(*gBufferShader_, item.tint, settings.cullBackFaces);
                drawCallCount_ += item.model->opaqueSubmeshCount();
            }
            for (const InstanceBatch& batch : instanceBatches) {
                gBufferShader_->setBool("uMotionHistoryValid", false);
                gBufferShader_->setMat4("uPreviousModel", glm::mat4(1.0f));
                batch.model->drawOpaqueInstanced(
                    *gBufferShader_,
                    batch.tint,
                    batch.modelMatrices,
                    batch.lodLevel,
                    settings.cullBackFaces
                );
                drawCallCount_ += batch.model->opaqueSubmeshCount();
            }
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glDisable(GL_CULL_FACE);
            gBuffer_->resolve();
        });

        const bool ssaoActive = settings.ssaoEnabled
            || settings.gBufferDebugView == GBufferDebugView::Ssao;
        if (ssaoActive) {
            sequence.add("SSAO", [&] {
                ssaoRenderer_->render(
                    *gBuffer_, view, projection,
                    settings.ssaoRadius,
                    settings.ssaoBias,
                    settings.ssaoStrength,
                    width, height
                );
                drawCallCount_ += 2U;
            });
        }

        sequence.add("Deferred lighting", [&] {
            renderTarget_->bindDeferredOpaqueScene(gBuffer_->framebuffer());
            glViewport(0, 0, width, height);
            glClearColor(
                settings.backgroundColor.r,
                settings.backgroundColor.g,
                settings.backgroundColor.b,
                1.0f
            );
            glClear(GL_COLOR_BUFFER_BIT);
            if (settings.skyboxEnabled && !gBufferDebugActive) {
                glDisable(GL_DEPTH_TEST);
                environmentMap_->draw(
                    glm::inverse(projection * view),
                    camera.position(),
                    settings.environmentIntensity
                );
                ++drawCallCount_;
            }

            deferredLightingShader_->use();
            deferredLightingShader_->setInt("uGAlbedo", 0);
            deferredLightingShader_->setInt("uGNormal", 1);
            deferredLightingShader_->setInt("uGMaterial", 2);
            deferredLightingShader_->setInt("uGDepth", 3);
            deferredLightingShader_->setInt("uIrradianceMap", 4);
            deferredLightingShader_->setInt("uPrefilteredEnvironmentMap", 5);
            deferredLightingShader_->setInt("uBrdfLut", 6);
            deferredLightingShader_->setInt("uShadowMap", 7);
            deferredLightingShader_->setInt("uTransmissionShadowMap", 8);
            deferredLightingShader_->setInt("uCausticsMap", 9);
            deferredLightingShader_->setInt("uSsao", 10);
            deferredLightingShader_->setBool("uSsaoEnabled", ssaoActive);
            deferredLightingShader_->setBool(
                "uSkinningDebugActive",
                settings.skinningDebugView != 0
            );
            deferredLightingShader_->setMat4(
                "uInverseViewProjection",
                glm::inverse(projection * view)
            );
            deferredLightingShader_->setMat4("uLightViewProjection", lightViewProjection);
            deferredLightingShader_->setVec3("uCameraPosition", camera.position());
            deferredLightingShader_->setVec3("uLightDirection", lightDirection);
            bindLocalLights(*deferredLightingShader_);
            deferredLightingShader_->setFloat("uAmbientStrength", settings.ambientStrength);
            deferredLightingShader_->setFloat("uDiffuseStrength", settings.diffuseStrength);
            deferredLightingShader_->setFloat("uSpecularStrength", settings.specularStrength);
            deferredLightingShader_->setFloat("uShininess", settings.shininess);
            deferredLightingShader_->setFloat(
                "uEnvironmentIntensity",
                settings.environmentIntensity
            );
            deferredLightingShader_->setFloat(
                "uEnvironmentMaxMip",
                static_cast<float>(environmentMap_->maximumMipLevel())
            );
            deferredLightingShader_->setBool("uPbrEnabled", settings.pbrEnabled);
            deferredLightingShader_->setBool("uIblEnabled", settings.iblEnabled);
            deferredLightingShader_->setBool("uShadowsEnabled", settings.shadowsEnabled);
            deferredLightingShader_->setBool(
                "uColoredTransmissionShadowsEnabled",
                settings.coloredTransmissionShadowsEnabled
            );
            deferredLightingShader_->setBool("uCausticsEnabled", causticsActive);
            deferredLightingShader_->setInt(
                "uGBufferDebugView",
                static_cast<int>(settings.gBufferDebugView)
            );
            gBuffer_->bindTextures(0U, 1U, 2U, 3U);
            environmentMap_->bindIrradiance(4U);
            environmentMap_->bindPrefiltered(5U);
            environmentMap_->bindBrdfLut(6U);
            shadowMap_->bindTexture(7U);
            shadowMap_->bindTransmissionTexture(8U);
            causticsMap_->bindTexture(9U);
            ssaoRenderer_->bindTexture(10U);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glBindVertexArray(fullscreenVertexArray_);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
            ++drawCallCount_;

            if (!gBufferDebugActive && (settings.showGrid || settings.showAxes)) {
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_FALSE);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                debugGrid_->draw(view, projection, settings.showGrid, settings.showAxes);
                drawCallCount_ += (settings.showGrid ? 1U : 0U)
                    + (settings.showAxes ? 1U : 0U);
                glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
            }
            if (!spectralBeamVisible) renderTarget_->finalizeOpaqueScene();
        });
    }
    if (spectralBeamVisible) {
        sequence.add("Spectral beam HDR", [&] {
            // The beam is composited after opaque geometry so the existing depth
            // buffer occludes it correctly, but before glass so refraction can
            // sample its radiance from the resolved opaque HDR texture.
            if (deferredActive) {
                renderTarget_->bindOpaqueResolvedScene();
            } else {
                renderTarget_->bindOpaqueScene();
            }
            glViewport(0, 0, width, height);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glEnable(GL_BLEND);
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_ONE, GL_ONE);
            if (beamTimingQuery < beamTimingPending_.size()) {
                glQueryCounter(beamStartQueries_[beamTimingQuery], GL_TIMESTAMP);
            }
            drawCallCount_ += spectralBeamRenderer_->draw(
                settings.prismSpectrum,
                camera.position(),
                view,
                projection,
                settings.prismBeamOutputLength,
                settings.prismBeamWidth,
                settings.prismBeamIntensity,
                settings.prismBeamEdgeSoftness,
                settings.prismBeamBloomContribution,
                settings.prismBeamWhitePoint
            );
            if (beamTimingQuery < beamTimingPending_.size()) {
                glQueryCounter(beamEndQueries_[beamTimingQuery], GL_TIMESTAMP);
                beamTimingPending_[beamTimingQuery] = true;
                nextBeamTimingQuery_ = (beamTimingQuery + 1U) % beamTimingPending_.size();
            }
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            if (deferredActive) {
                renderTarget_->finalizeOpaqueScene();
            } else {
                renderTarget_->resolveOpaqueScene();
            }
        });
    }
    sequence.add("Forward transparent / refractive scene", [&] {
        // Copy the opaque HDR result into a distinct output attachment. Future
        // transmissive materials can sample opaqueColorTexture/sceneDepthTexture
        // while drawing here without reading from the texture being written.
        renderTarget_->bindRefractiveScene();
        glViewport(0, 0, width, height);
        if (!transparentDraws.empty() && !gBufferDebugActive) {
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, renderTarget_->opaqueColorTexture());
            glActiveTexture(GL_TEXTURE6);
            glBindTexture(GL_TEXTURE_2D, renderTarget_->sceneDepthTexture());
            glActiveTexture(GL_TEXTURE8);
            glBindTexture(GL_TEXTURE_2D, renderTarget_->glassFrontfaceDepthTexture());
            glActiveTexture(GL_TEXTURE9);
            glBindTexture(GL_TEXTURE_2D, renderTarget_->glassBackfaceDepthTexture());
            glActiveTexture(GL_TEXTURE10);
            glBindTexture(GL_TEXTURE_2D, renderTarget_->glassExitNormalTexture());
            glActiveTexture(GL_TEXTURE11);
            glBindTexture(GL_TEXTURE_2D, renderTarget_->glassObjectIdTexture());
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFuncSeparate(
                GL_SRC_ALPHA,
                GL_ONE_MINUS_SRC_ALPHA,
                GL_ONE,
                GL_ONE_MINUS_SRC_ALPHA
            );
            glPolygonMode(GL_FRONT_AND_BACK, settings.wireframe ? GL_LINE : GL_FILL);
            bindSceneShader();
            std::size_t capturedRenderItem = renderItems.size();
            for (const TransparentSortEntry& draw : transparentDraws) {
                const RenderItem& item = renderItems[draw.renderItemIndex];
                const bool transmissive = settings.transmissionEnabled
                    && item.model->transparentSubmeshIsTransmissive(
                        draw.transparentSubmeshIndex
                    );
                if (transmissive && capturedRenderItem != draw.renderItemIndex) {
                    glViewport(0, 0, width, height);
                    glDisable(GL_DEPTH_TEST);
                    glDepthMask(GL_FALSE);
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_ONE, GL_ONE);
                    glDisable(GL_CULL_FACE);
                    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                    glassThicknessShader_->use();
                    glassThicknessShader_->setMat4("uView", view);
                    glassThicknessShader_->setMat4("uProjection", projection);
                    glassThicknessShader_->setMat4("uModel", item.modelMatrix);
                    glassThicknessShader_->setInt("uPassMode", 0);

                    renderTarget_->bindGlassFrontfaceThickness();
                    const std::array<float, 4> farClear{1.0e20f, 0.0f, 0.0f, 0.0f};
                    glClearBufferfv(GL_COLOR, 0, farClear.data());
                    glBlendEquation(GL_MIN);
                    item.model->drawTransmissiveDepth(*glassThicknessShader_);

                    renderTarget_->bindGlassBackfaceThickness();
                    const std::array<float, 4> nearClear{0.0f, 0.0f, 0.0f, 0.0f};
                    glClearBufferfv(GL_COLOR, 0, nearClear.data());
                    glBlendEquation(GL_MAX);
                    item.model->drawTransmissiveDepth(*glassThicknessShader_);

                    glBlendEquation(GL_FUNC_ADD);
                    glDisable(GL_BLEND);
                    renderTarget_->bindGlassExitSurfaceData();
                    const std::array<float, 4> invalidNormal{0.5f, 0.5f, 0.5f, 0.0f};
                    const std::array<unsigned int, 4> noObject{0U, 0U, 0U, 0U};
                    glClearBufferfv(GL_COLOR, 0, invalidNormal.data());
                    glClearBufferuiv(GL_COLOR, 1, noObject.data());
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(
                        GL_TEXTURE_2D,
                        renderTarget_->glassBackfaceDepthTexture()
                    );
                    glassThicknessShader_->setInt("uGlassBackfaceDepthTexture", 0);
                    glassThicknessShader_->setInt("uPassMode", 1);
                    glassThicknessShader_->setInt(
                        "uGlassObjectId",
                        static_cast<int>(draw.renderItemIndex + 1U)
                    );
                    item.model->drawTransmissiveDepth(*glassThicknessShader_);
                    drawCallCount_ += item.model->transmissiveSubmeshCount() * 3U;

                    renderTarget_->bindHdrSceneForOverlay();
                    glActiveTexture(GL_TEXTURE5);
                    glBindTexture(GL_TEXTURE_2D, renderTarget_->opaqueColorTexture());
                    glActiveTexture(GL_TEXTURE6);
                    glBindTexture(GL_TEXTURE_2D, renderTarget_->sceneDepthTexture());
                    glActiveTexture(GL_TEXTURE8);
                    glBindTexture(GL_TEXTURE_2D, renderTarget_->glassFrontfaceDepthTexture());
                    glActiveTexture(GL_TEXTURE9);
                    glBindTexture(GL_TEXTURE_2D, renderTarget_->glassBackfaceDepthTexture());
                    glActiveTexture(GL_TEXTURE10);
                    glBindTexture(GL_TEXTURE_2D, renderTarget_->glassExitNormalTexture());
                    glActiveTexture(GL_TEXTURE11);
                    glBindTexture(GL_TEXTURE_2D, renderTarget_->glassObjectIdTexture());
                    glEnable(GL_DEPTH_TEST);
                    glDepthMask(GL_FALSE);
                    glEnable(GL_BLEND);
                    glBlendEquation(GL_FUNC_ADD);
                    glBlendFuncSeparate(
                        GL_SRC_ALPHA,
                        GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE,
                        GL_ONE_MINUS_SRC_ALPHA
                    );
                    glPolygonMode(
                        GL_FRONT_AND_BACK,
                        settings.wireframe ? GL_LINE : GL_FILL
                    );
                    bindSceneShader();
                    capturedRenderItem = draw.renderItemIndex;
                }
                shader_->setBool("uHasGlassBackfaceData", transmissive);
                shader_->setInt(
                    "uGlassObjectId",
                    transmissive ? static_cast<int>(draw.renderItemIndex + 1U) : 0
                );
                shader_->setMat4("uModel", item.modelMatrix);
                item.model->drawTransparentSubmesh(
                    *shader_,
                    item.tint,
                    draw.transparentSubmeshIndex,
                    settings.cullBackFaces
                );
                ++drawCallCount_;
            }
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            glDisable(GL_CULL_FACE);
            glActiveTexture(GL_TEXTURE6);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE9);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE11);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE10);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE8);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE7);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    });
    if (settings.showPrismOpticalPathDebug
        && settings.prismOpticalPathValid
        && !gBufferDebugActive) {
        sequence.add("Optical path debug", [&] {
            renderTarget_->bindHdrSceneForOverlay();
            glViewport(0, 0, width, height);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            drawCallCount_ += opticalPathDebugRenderer_->draw(
                settings.prismSpectrum,
                view,
                projection,
                settings.prismBeamOutputLength
            );
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
        });
    }
    sequence.add(
        gBufferDebugActive
            ? "G-buffer debug output"
            : (temporalAaActive
                ? (settings.bloom ? "TAA + bloom + tone map" : "TAA + tone map")
                : (settings.bloom ? "Bloom + tone map" : "Tone map")),
        [&] {
        PostProcessSettings postSettings;
        postSettings.toneMapping = gBufferDebugActive ? false : settings.toneMapping;
        postSettings.bloom = gBufferDebugActive ? false : settings.bloom;
        postSettings.encodeSrgb = !gBufferDebugActive;
        postSettings.exposure = gBufferDebugActive ? 1.0f : settings.exposure;
        postSettings.bloomThreshold = settings.bloomThreshold;
        postSettings.bloomIntensity = settings.bloomIntensity;
        postSettings.temporalAa = temporalAaActive;
        postSettings.temporalDebugView = temporalAaActive
            ? settings.temporalDebugView
            : 0;
        postSettings.resetTemporalHistory = resetTemporalHistory;
        postSettings.temporalHistoryWeight = settings.temporalHistoryWeight;
        postSettings.depthTexture = renderTarget_->sceneDepthTexture();
        postSettings.objectMotionTexture = deferredActive ? gBuffer_->motionTexture() : 0U;
        postSettings.inverseCurrentViewProjection = glm::inverse(currentViewProjection);
        postSettings.previousViewProjection = previousViewProjectionValid_
            ? previousViewProjection_
            : currentViewProjection;
        postProcessor_->process(*renderTarget_, postSettings);
        drawCallCount_ += (!gBufferDebugActive && settings.bloom ? 10U : 1U)
            + (temporalAaActive ? 1U : 0U);
    });
    activePassNames_ = sequence.names();
    activePassContexts_ = sequence.contexts();
    std::array<std::size_t, maxProfiledPasses_> activePassQuerySlots{};
    activePassQuerySlots.fill(passTimingPending_.size());
    const bool hasDebugGroups = GLAD_GL_KHR_debug != 0
        && glPushDebugGroup != nullptr
        && glPopDebugGroup != nullptr;
    sequence.run(
        [&](std::size_t passIndex, const RenderPassContext& context) {
            stateCache_.invalidate();
            stateCache_.apply(context.state);
            if (context.viewportWidth > 0 && context.viewportHeight > 0) {
                glViewport(0, 0, context.viewportWidth, context.viewportHeight);
            }
            if (context.clearMask != 0U) glClear(context.clearMask);
            if (hasDebugGroups) {
                glPushDebugGroup(
                    GL_DEBUG_SOURCE_APPLICATION,
                    static_cast<GLuint>(passIndex + 1U),
                    -1,
                    context.name.c_str()
                );
            }
            if (passIndex >= maxProfiledPasses_) return;
            const std::size_t ring = nextPassTimingRing_[passIndex];
            const std::size_t slot = passIndex * passTimingRingSize_ + ring;
            if (passTimingPending_[slot]) return;
            glQueryCounter(passStartQueries_[slot], GL_TIMESTAMP);
            activePassQuerySlots[passIndex] = slot;
        },
        [&](std::size_t passIndex, const RenderPassContext& context) {
            if (passIndex < maxProfiledPasses_) {
                const std::size_t slot = activePassQuerySlots[passIndex];
                if (slot < passTimingPending_.size()) {
                    glQueryCounter(passEndQueries_[slot], GL_TIMESTAMP);
                    passTimingPending_[slot] = true;
                    passTimingNames_[slot] = context.name;
                    nextPassTimingRing_[passIndex] =
                        (nextPassTimingRing_[passIndex] + 1U) % passTimingRingSize_;
                }
            }
            if (hasDebugGroups) glPopDebugGroup();
        }
    );
    stateCache_.invalidate();
    stateCache_.apply(RenderState{});
    if (timingQuery < timingQueries_.size()) {
        glEndQuery(GL_TIME_ELAPSED);
        timingQueryPending_[timingQuery] = true;
        nextTimingQuery_ = (timingQuery + 1U) % timingQueries_.size();
    }
    previousViewProjectionValid_ = temporalAaActive;
    lastTemporalAaEnabled_ = temporalAaActive;
    lastTemporalWidth_ = width;
    lastTemporalHeight_ = height;
    if (temporalAaActive) {
        previousViewProjection_ = currentViewProjection;
        ++temporalFrameIndex_;
    } else {
        temporalFrameIndex_ = 0U;
    }
}

unsigned int Renderer::colorTexture() const {
    return renderTarget_->colorTexture();
}

TextureCache& Renderer::textureCache() {
    return *textureCache_;
}

bool Renderer::saveScreenshot(const std::filesystem::path& path, std::string& error) const {
    return renderTarget_->savePng(path, error);
}

int Renderer::activeMsaaSamples() const {
    return renderTarget_->samples();
}

int Renderer::shadowResolution() const {
    return shadowMap_->resolution();
}

int Renderer::renderWidth() const {
    return renderTarget_->width();
}

int Renderer::renderHeight() const {
    return renderTarget_->height();
}

std::size_t Renderer::estimatedRenderMemoryBytes() const {
    return renderTarget_->estimatedBytes()
        + gBuffer_->estimatedBytes()
        + postProcessor_->estimatedBytes()
        + environmentMap_->estimatedBytes()
        + shadowMap_->estimatedBytes()
        + ssaoRenderer_->estimatedBytes()
        + causticsMap_->estimatedBytes()
        + spectralBeamRenderer_->vertexBufferBytes();
}

std::size_t Renderer::estimatedOpaqueTrafficBytesPerFrame() const {
    const std::size_t pixels = static_cast<std::size_t>(std::max(renderTarget_->width(), 0))
        * static_cast<std::size_t>(std::max(renderTarget_->height(), 0));
    const std::size_t samples = static_cast<std::size_t>(
        std::max(renderTarget_->samples(), 1)
    );
    if (activeRenderPath_ == RenderPath::Forward) {
        constexpr std::size_t hdrColorAndDepthBytes = 8U + 4U;
        std::size_t traffic = pixels * hdrColorAndDepthBytes * samples;
        if (samples > 1U) {
            traffic += pixels * hdrColorAndDepthBytes * (samples + 1U);
        }
        return traffic;
    }

    constexpr std::size_t gBufferBytes = 4U + 8U + 2U + 4U;
    constexpr std::size_t opaqueHdrBytes = 8U;
    constexpr std::size_t depthCopyBytes = 4U + 4U;
    std::size_t traffic = pixels * gBufferBytes * samples;
    if (samples > 1U) {
        traffic += pixels * gBufferBytes * (samples + 1U);
    }
    traffic += pixels * (gBufferBytes + opaqueHdrBytes + depthCopyBytes);
    return traffic;
}
