#include "render/Renderer.h"

#include <algorithm>

#include <glad/gl.h>
#include <glm/geometric.hpp>

#include "render/Camera.h"
#include "render/GpuModel.h"
#include "render/RenderTarget.h"
#include "render/Shader.h"

Renderer::Renderer(
    const std::filesystem::path& vertexShaderPath,
    const std::filesystem::path& fragmentShaderPath
) : shader_(std::make_unique<Shader>(vertexShaderPath, fragmentShaderPath)),
    renderTarget_(std::make_unique<RenderTarget>()) {
}

Renderer::~Renderer() = default;

void Renderer::render(
    const GpuModel* model,
    const Camera& camera,
    const glm::mat4& modelMatrix,
    const RendererSettings& settings,
    int width,
    int height
) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    renderTarget_->resize(width, height);
    renderTarget_->bind();

    glViewport(0, 0, width, height);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClearColor(
        settings.backgroundColor.r,
        settings.backgroundColor.g,
        settings.backgroundColor.b,
        1.0f
    );
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (model != nullptr) {
        if (settings.cullBackFaces) {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glFrontFace(GL_CCW);
        } else {
            glDisable(GL_CULL_FACE);
        }
        glPolygonMode(GL_FRONT_AND_BACK, settings.wireframe ? GL_LINE : GL_FILL);

        glm::vec3 lightDirection = settings.lightDirection;
        if (glm::dot(lightDirection, lightDirection) < 1e-8f) {
            lightDirection = glm::vec3(-0.45f, -0.8f, -0.35f);
        }

        shader_->use();
        shader_->setMat4("uModel", modelMatrix);
        shader_->setMat4("uView", camera.viewMatrix());
        shader_->setMat4(
            "uProjection",
            camera.projectionMatrix(static_cast<float>(width) / static_cast<float>(height))
        );
        shader_->setVec3("uBaseColor", settings.baseColor);
        shader_->setVec3("uLightDirection", glm::normalize(lightDirection));
        shader_->setVec3("uCameraPosition", camera.position());
        shader_->setFloat("uAmbientStrength", settings.ambientStrength);
        shader_->setFloat("uDiffuseStrength", settings.diffuseStrength);
        shader_->setFloat("uSpecularStrength", settings.specularStrength);
        shader_->setFloat("uShininess", settings.shininess);
        model->draw();
    }

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_CULL_FACE);
    RenderTarget::unbind();
}

unsigned int Renderer::colorTexture() const {
    return renderTarget_->colorTexture();
}
