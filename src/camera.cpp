#include "engine/camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace engine {

Camera::Camera(const glm::vec3& position, float yawDeg, float pitchDeg)
    : position_(position), yawDeg_(yawDeg), pitchDeg_(std::clamp(pitchDeg, -kMaxPitchDegrees, kMaxPitchDegrees)) {
    updateVectors();
}

void Camera::updateVectors() {
    const float yawRad = glm::radians(yawDeg_);
    const float pitchRad = glm::radians(pitchDeg_);

    const glm::vec3 newFront{
        std::cos(yawRad) * std::cos(pitchRad),
        std::sin(pitchRad),
        std::sin(yawRad) * std::cos(pitchRad),
    };
    front_ = glm::normalize(newFront);
    // pitchDeg_ is always kept within +/-kMaxPitchDegrees (< 90), so front_
    // never becomes parallel to worldUp_ here -- this cross product is never
    // degenerate.
    right_ = glm::normalize(glm::cross(front_, worldUp_));
    up_ = glm::normalize(glm::cross(right_, front_));
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(position_, position_ + front_, up_);
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio) const {
    return getProjectionMatrix(aspectRatio, nearPlane_, farPlane_);
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio, float nearPlane, float farPlane) const {
    return glm::perspective(glm::radians(fovYDeg_), aspectRatio, nearPlane, farPlane);
}

void Camera::processMovement(const InputState& input, float deltaTime) {
    const float distance = movementSpeed_ * deltaTime;

    if (input.moveForward) {
        position_ += front_ * distance;
    }
    if (input.moveBackward) {
        position_ -= front_ * distance;
    }
    if (input.moveRight) {
        position_ += right_ * distance;
    }
    if (input.moveLeft) {
        position_ -= right_ * distance;
    }
    // Up/down along world-up (not the camera's local up_) so climbing/diving
    // stays vertical regardless of pitch -- the usual free-fly convention.
    if (input.moveUp) {
        position_ += worldUp_ * distance;
    }
    if (input.moveDown) {
        position_ -= worldUp_ * distance;
    }
}

void Camera::processMouseInput(double xpos, double ypos) {
    if (firstMouse_) {
        // Prime the tracked position without producing a rotation -- the
        // first real reading has no prior sample to diff against, so
        // treating it as a (huge, arbitrary) delta would snap the camera to
        // a random orientation on the very first frame.
        lastMouseX_ = xpos;
        lastMouseY_ = ypos;
        firstMouse_ = false;
        return;
    }

    const float xoffset = static_cast<float>(xpos - lastMouseX_) * mouseSensitivity_;
    // Screen-space Y grows downward; subtracting (rather than adding) so
    // moving the mouse up pitches the view up, matching the usual FPS-camera
    // convention.
    const float yoffset = static_cast<float>(lastMouseY_ - ypos) * mouseSensitivity_;
    lastMouseX_ = xpos;
    lastMouseY_ = ypos;

    yawDeg_ += xoffset;
    pitchDeg_ = std::clamp(pitchDeg_ + yoffset, -kMaxPitchDegrees, kMaxPitchDegrees);
    updateVectors();
}

void Camera::setPositionLookingAt(const glm::vec3& position, const glm::vec3& target) {
    position_ = position;
    const glm::vec3 direction = target - position;
    // A degenerate (near-zero-length) direction has no well-defined yaw/
    // pitch -- leave the existing orientation alone rather than normalizing
    // a near-zero vector into garbage.
    const float length = glm::length(direction);
    if (length < 1e-6f) {
        return;
    }
    const glm::vec3 dir = direction / length;

    pitchDeg_ = std::clamp(glm::degrees(std::asin(std::clamp(dir.y, -1.0f, 1.0f))), -kMaxPitchDegrees, kMaxPitchDegrees);
    yawDeg_ = glm::degrees(std::atan2(dir.z, dir.x));
    updateVectors();
}

}  // namespace engine
