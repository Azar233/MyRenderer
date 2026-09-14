#pragma once

#include <filesystem>
#include <stdexcept>
#include <vector>
#include <glad/gl.h>
#include "render/Camera.h"
#include "render/GpuModel.h"
#include "render/RenderItem.h"
#include "render/RenderTarget.h"
#include "render/Shader.h"

// Expand only the selected object silhouette; occlusion must not create new edges.
class SelectionOutline {
public:
    explicit SelectionOutline(const std::filesystem::path& shaders)
        : maskShader_(shaders / "basic.vert", shaders / "selection_mask.frag"),
          outlineShader_(shaders / "fullscreen.vert", shaders / "selection_outline.frag") {
        glGenFramebuffers(1, &framebuffer_);
        glGenTextures(1, &mask_);
        glGenTextures(1, &depth_);
        glGenTextures(1, &silhouette_);
        glGenVertexArrays(1, &vao_);
    }
    ~SelectionOutline() {
        glDeleteVertexArrays(1, &vao_);
        glDeleteTextures(1, &mask_);
        glDeleteTextures(1, &depth_);
        glDeleteTextures(1, &silhouette_);
        glDeleteFramebuffers(1, &framebuffer_);
    }
    SelectionOutline(const SelectionOutline&) = delete;
    SelectionOutline& operator=(const SelectionOutline&) = delete;

    std::size_t estimatedBytes() const { return static_cast<std::size_t>(width_) * height_ * 13U; }

    void draw(RenderTarget& target, const std::vector<RenderItem>& items,
              const Camera& camera, std::uint64_t selected, bool cullBackFaces) {
        const RenderItem* object = nullptr;
        for (const auto& item : items) {
            if (item.entityId == selected && item.visible && item.model) { object = &item; break; }
        }
        if (!selected || !object) return;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mask_);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        if (width_ != target.width() || height_ != target.height()) {
            width_ = target.width(); height_ = target.height();
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width_, height_, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mask_, 0);
            glBindTexture(GL_TEXTURE_2D, silhouette_);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, width_, height_, 0, GL_RG, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glBindTexture(GL_TEXTURE_2D, depth_);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width_, height_, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                throw std::runtime_error("Selection mask framebuffer is incomplete");
        }
        glViewport(0, 0, width_, height_);
        glDisable(GL_SCISSOR_TEST);
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
        glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        const float clear[4]{};
        const float farDepth = 1.0f;
        maskShader_.use();
        maskShader_.setMat4("uView", camera.viewMatrix());
        maskShader_.setMat4("uProjection", camera.projectionMatrix(static_cast<float>(width_) / height_));
        const auto drawItem = [&](const RenderItem& item) {
            maskShader_.setMat4("uModel", item.modelMatrix);
            maskShader_.setBool("uSelected", item.entityId == selected);
            item.model->drawOpaque(maskShader_, glm::vec3(1.0f), cullBackFaces);
            for (std::size_t i = 0; i < item.model->transparentSubmeshCount(); ++i)
                item.model->drawTransparentSubmesh(maskShader_, glm::vec3(1.0f), i, cullBackFaces);
        };
        // Full coverage prevents foreground objects from punching outline-producing holes.
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, silhouette_, 0);
        glClearBufferfv(GL_COLOR, 0, clear);
        glClearBufferfv(GL_DEPTH, 0, &farDepth);
        drawItem(*object);
        // Visible selection plus scene depth determine which true silhouette edges may show.
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mask_, 0);
        glClearBufferfv(GL_COLOR, 0, clear);
        glClearBufferfv(GL_DEPTH, 0, &farDepth);
        for (const auto& item : items) {
            if (item.visible && item.model) drawItem(item);
        }

        target.bindFinal();
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
        outlineShader_.use();
        outlineShader_.setInt("uMask", 0);
        outlineShader_.setInt("uSilhouette", 1);
        outlineShader_.setInt("uSceneDepth", 2);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, silhouette_);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, depth_);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, mask_);
        glBindVertexArray(vao_); glDrawArrays(GL_TRIANGLES, 0, 3); glBindVertexArray(0);
        glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
        target.unbind();
    }
private:
    Shader maskShader_;
    Shader outlineShader_;
    GLuint framebuffer_{0}, mask_{0}, depth_{0}, silhouette_{0}, vao_{0};
    int width_{0}, height_{0};
};
