#include "render/Renderer.h"

#include <algorithm>

#include <glad/gl.h>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

#include "render/Camera.h"
#include "render/DebugGrid.h"
#include "render/EnvironmentMap.h"
#include "render/GpuModel.h"
#include "render/OpticalPathDebugRenderer.h"
#include "render/PostProcessor.h"
#include "render/RenderPassSequence.h"
#include "render/RenderTarget.h"
#include "render/SceneDrawList.h"
#include "render/Shader.h"
#include "render/ShadowMap.h"
#include "render/SpectralBeamRenderer.h"
#include "render/Texture2D.h"

Renderer::Renderer(
    const std::filesystem::path& vertexShaderPath,
    const std::filesystem::path& fragmentShaderPath,
    const std::filesystem::path& debugVertexShaderPath,
    const std::filesystem::path& debugFragmentShaderPath
) : shader_(std::make_unique<Shader>(vertexShaderPath, fragmentShaderPath)),
    debugGrid_(std::make_unique<DebugGrid>(debugVertexShaderPath, debugFragmentShaderPath)),
    environmentMap_(std::make_unique<EnvironmentMap>(
        vertexShaderPath.parent_path() / "fullscreen.vert",
        vertexShaderPath.parent_path() / "skybox.frag"
    )),
    opticalPathDebugRenderer_(std::make_unique<OpticalPathDebugRenderer>(
        debugVertexShaderPath,
        debugFragmentShaderPath
    )),
    shadowMap_(std::make_unique<ShadowMap>()),
    spectralBeamRenderer_(std::make_unique<SpectralBeamRenderer>(
        vertexShaderPath.parent_path() / "spectral_beam.vert",
        vertexShaderPath.parent_path() / "spectral_beam.frag"
    )),
    shadowShader_(std::make_unique<Shader>(
        vertexShaderPath.parent_path() / "shadow_depth.vert",
        vertexShaderPath.parent_path() / "shadow_depth.frag"
    )),
    glassThicknessShader_(std::make_unique<Shader>(
        vertexShaderPath.parent_path() / "glass_thickness.vert",
        vertexShaderPath.parent_path() / "glass_thickness.frag"
    )),
    postProcessor_(std::make_unique<PostProcessor>(
        vertexShaderPath.parent_path() / "fullscreen.vert",
        vertexShaderPath.parent_path() / "bloom_extract.frag",
        vertexShaderPath.parent_path() / "bloom_blur.frag",
        vertexShaderPath.parent_path() / "postprocess.frag"
    )),
    renderTarget_(std::make_unique<RenderTarget>()),
    textureCache_(std::make_unique<TextureCache>()) {
    glGenQueries(static_cast<GLsizei>(timingQueries_.size()), timingQueries_.data());
    glGenQueries(static_cast<GLsizei>(beamStartQueries_.size()), beamStartQueries_.data());
    glGenQueries(static_cast<GLsizei>(beamEndQueries_.size()), beamEndQueries_.data());
}

Renderer::~Renderer() {
    glDeleteQueries(static_cast<GLsizei>(timingQueries_.size()), timingQueries_.data());
    glDeleteQueries(static_cast<GLsizei>(beamStartQueries_.size()), beamStartQueries_.data());
    glDeleteQueries(static_cast<GLsizei>(beamEndQueries_.size()), beamEndQueries_.data());
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
    renderTarget_->resize(width, height, settings.msaaSamples);

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
    const glm::mat4 projection = camera.projectionMatrix(
        static_cast<float>(width) / static_cast<float>(height)
    );
    glm::vec3 lightDirection = settings.lightDirection;
    if (glm::dot(lightDirection, lightDirection) < 1e-8f) {
        lightDirection = glm::vec3(-0.45f, -0.8f, -0.35f);
    }
    lightDirection = glm::normalize(lightDirection);
    glm::vec3 sceneCenter(0.0f);
    bool hasVisibleItems = false;
    bool hasShadowCasters = false;
    for (const RenderItem& item : renderItems) {
        if (!item.visible || item.model == nullptr) {
            continue;
        }
        if (!hasVisibleItems) {
            sceneCenter = glm::vec3(item.modelMatrix[3]);
            hasVisibleItems = true;
        }
        hasShadowCasters |= item.castsShadow && item.model->opaqueSubmeshCount() > 0U;
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

    const auto bindSceneShader = [&] {
        shader_->use();
        shader_->setMat4("uView", view);
        shader_->setMat4("uProjection", projection);
        shader_->setMat4("uLightViewProjection", lightViewProjection);
        shader_->setVec3("uLightDirection", lightDirection);
        shader_->setVec3("uCameraPosition", camera.position());
        shader_->setFloat("uAmbientStrength", settings.ambientStrength);
        shader_->setFloat("uDiffuseStrength", settings.diffuseStrength);
        shader_->setFloat("uSpecularStrength", settings.specularStrength);
        shader_->setFloat("uShininess", settings.shininess);
        shader_->setBool("uNormalMappingEnabled", settings.normalMapping);
        shader_->setBool("uPbrEnabled", settings.pbrEnabled);
        shader_->setBool("uIblEnabled", settings.iblEnabled);
        shader_->setBool("uShadowsEnabled", settings.shadowsEnabled);
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
        shader_->setBool("uHasGlassBackfaceData", false);
        shader_->setInt("uGlassObjectId", 0);
        environmentMap_->bind(3U);
        environmentMap_->bindIrradiance(12U);
        environmentMap_->bindPrefiltered(13U);
        environmentMap_->bindBrdfLut(14U);
        shadowMap_->bindTexture(4U);
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
    RenderPassSequence sequence;
    if (settings.shadowsEnabled && hasShadowCasters) {
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
                item.model->drawDepth();
                drawCallCount_ += item.model->opaqueSubmeshCount();
            }
            glCullFace(GL_BACK);
            glDisable(GL_CULL_FACE);
        });
    }
    sequence.add("Opaque HDR scene", [&] {
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
                shader_->setMat4("uModel", item.modelMatrix);
                item.model->drawOpaque(*shader_, item.tint, settings.cullBackFaces);
                drawCallCount_ += item.model->opaqueSubmeshCount();
            }
        }

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_CULL_FACE);
        if (!spectralBeamVisible) {
            renderTarget_->resolveOpaqueScene();
        }
    });
    if (spectralBeamVisible) {
        sequence.add("Spectral beam HDR", [&] {
            // The beam is composited after opaque geometry so the existing depth
            // buffer occludes it correctly, but before glass so refraction can
            // sample its radiance from the resolved opaque HDR texture.
            renderTarget_->bindOpaqueScene();
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
            renderTarget_->resolveOpaqueScene();
        });
    }
    sequence.add("Forward transparent / refractive scene", [&] {
        // Copy the opaque HDR result into a distinct output attachment. Future
        // transmissive materials can sample opaqueColorTexture/sceneDepthTexture
        // while drawing here without reading from the texture being written.
        renderTarget_->bindRefractiveScene();
        glViewport(0, 0, width, height);
        if (!transparentDraws.empty()) {
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
                    item.model->drawTransmissiveDepth();

                    renderTarget_->bindGlassBackfaceThickness();
                    const std::array<float, 4> nearClear{0.0f, 0.0f, 0.0f, 0.0f};
                    glClearBufferfv(GL_COLOR, 0, nearClear.data());
                    glBlendEquation(GL_MAX);
                    item.model->drawTransmissiveDepth();

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
                    item.model->drawTransmissiveDepth();
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
    if (settings.showPrismOpticalPathDebug && settings.prismOpticalPathValid) {
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
    sequence.add(settings.bloom ? "Bloom + tone map" : "Tone map", [&] {
        postProcessor_->process(*renderTarget_, PostProcessSettings{
            settings.toneMapping,
            settings.bloom,
            settings.exposure,
            settings.bloomThreshold,
            settings.bloomIntensity
        });
        drawCallCount_ += settings.bloom ? 10U : 1U;
    });
    activePassNames_ = sequence.names();
    sequence.run();
    if (timingQuery < timingQueries_.size()) {
        glEndQuery(GL_TIME_ELAPSED);
        timingQueryPending_[timingQuery] = true;
        nextTimingQuery_ = (timingQuery + 1U) % timingQueries_.size();
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
        + postProcessor_->estimatedBytes()
        + environmentMap_->estimatedBytes()
        + shadowMap_->estimatedBytes()
        + spectralBeamRenderer_->vertexBufferBytes();
}
