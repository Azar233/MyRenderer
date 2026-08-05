#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class Camera {
public:
    Camera();

    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspectRatio) const;
    glm::vec3 position() const;

    void orbit(float yawDelta, float pitchDelta);
    void pan(float xDelta, float yDelta);
    void zoom(float wheelDelta);
    void reset(const glm::vec3& target = glm::vec3(0.0f));

    float fieldOfView() const { return fieldOfViewDegrees_; }
    void setFieldOfView(float degrees);

private:
    glm::vec3 target_{0.0f};
    float yawRadians_{0.75f};
    float pitchRadians_{0.35f};
    float distance_{3.2f};
    float fieldOfViewDegrees_{45.0f};
};
