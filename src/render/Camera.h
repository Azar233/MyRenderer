#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

struct CameraOrbitState {
    glm::vec3 target{0.0f};
    float yawDegrees{42.9718f};
    float pitchDegrees{20.0535f};
    float distance{3.2f};
    float fieldOfViewDegrees{45.0f};
};

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
    void setOrbitPose(
        const glm::vec3& target,
        float yawDegrees,
        float pitchDegrees,
        float distance,
        float fieldOfViewDegrees
    );

    CameraOrbitState orbitState() const;
    void setOrbitState(const CameraOrbitState& state);

    float fieldOfView() const { return fieldOfViewDegrees_; }
    void setFieldOfView(float degrees);

private:
    glm::vec3 target_{0.0f};
    float yawRadians_{0.75f};
    float pitchRadians_{0.35f};
    float distance_{3.2f};
    float fieldOfViewDegrees_{45.0f};
};
