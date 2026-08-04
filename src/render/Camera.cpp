#include "render/Camera.h"

#include <algorithm>
#include <cmath>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

Camera::Camera() = default;

glm::vec3 Camera::position() const {
    const float horizontal = std::cos(pitchRadians_);
    return target_ + distance_ * glm::vec3(
        horizontal * std::sin(yawRadians_),
        std::sin(pitchRadians_),
        horizontal * std::cos(yawRadians_)
    );
}

glm::mat4 Camera::viewMatrix() const {
    return glm::lookAt(position(), target_, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::projectionMatrix(float aspectRatio) const {
    return glm::perspective(glm::radians(fieldOfViewDegrees_), std::max(aspectRatio, 0.01f), 0.05f, 100.0f);
}

void Camera::orbit(float yawDelta, float pitchDelta) {
    yawRadians_ += yawDelta;
    pitchRadians_ = std::clamp(pitchRadians_ + pitchDelta, -1.5f, 1.5f);
}

void Camera::pan(float xDelta, float yDelta) {
    const glm::vec3 forward = glm::normalize(target_ - position());
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    const float speed = distance_ * 0.0015f;
    target_ += (-xDelta * right + yDelta * up) * speed;
}

void Camera::zoom(float wheelDelta) {
    distance_ *= std::pow(0.86f, wheelDelta);
    distance_ = std::clamp(distance_, 0.35f, 40.0f);
}

void Camera::reset() {
    target_ = glm::vec3(0.0f);
    yawRadians_ = 0.75f;
    pitchRadians_ = 0.35f;
    distance_ = 3.2f;
    fieldOfViewDegrees_ = 45.0f;
}

void Camera::setFieldOfView(float degrees) {
    fieldOfViewDegrees_ = std::clamp(degrees, 15.0f, 90.0f);
}
