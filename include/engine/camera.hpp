#ifndef ENGINE_CAMERA_HPP
#define ENGINE_CAMERA_HPP

// A standard yaw/pitch free-fly camera: a position plus an orientation
// derived from two angles (yaw around world-up, pitch around the resulting
// local right axis) rather than a full quaternion/matrix orientation --
// simpler to reason about and to drive from WASD + mouse-look, and there's
// no roll in this phase so the gimbal-lock risk that yaw/pitch normally
// carries is confined to a single, well-understood failure mode: pitch
// approaching +/-90 degrees. That's handled by clamping pitch away from the
// poles (see kMaxPitchDegrees) so front_/right_/up_ never degenerate from a
// near-zero cross product and the view never flips.
//
// getViewMatrix()/getProjectionMatrix() are pure functions of the camera's
// current state -- Application calls them fresh every frame rather than
// caching a combined matrix, matching Shader's "no premature caching" style
// from Phase 2.

#include <glm/glm.hpp>

#include "engine/input.hpp"

namespace engine {

class Camera {
public:
    // yawDeg/pitchDeg follow the usual convention: yaw 0 = facing +X, -90 =
    // facing -Z (so a fresh camera at the origin with yaw=-90/pitch=0 looks
    // down -Z, matching GL's default "camera looks down -Z" expectation).
    explicit Camera(const glm::vec3& position = glm::vec3(0.0f, 0.0f, 3.0f), float yawDeg = -90.0f,
                     float pitchDeg = 0.0f);

    glm::mat4 getViewMatrix() const;
    // aspectRatio = framebuffer width / height; fovYDeg/near/far are fixed
    // per-camera (set at construction or via the setters below) rather than
    // parameters here, so every call site doesn't have to know/repeat them.
    glm::mat4 getProjectionMatrix(float aspectRatio) const;

    // Moves position_ along the camera's own front/right/worldUp axes based
    // on whichever of `input`'s movement flags are set, scaled by deltaTime
    // so movement speed is independent of frame rate. `input` is a snapshot
    // Application polls once per frame from Window (see input.hpp) --
    // Camera itself no longer reaches into Window/GLFW key constants
    // directly (Phase 3-5's processKeyboard(const Window&, float) did);
    // decoupling Camera from the windowing layer this way is what lets it
    // be driven identically by real input or a future non-Window input
    // source (recorded input, a test harness, ...) without Camera itself
    // changing. Safe to call every frame even when nothing is actually
    // pressed (e.g. headless Xvfb, where every InputState flag is simply
    // false) -- it's a no-op in that case, same as before.
    void processMovement(const InputState& input, float deltaTime);

    // Mouse-look, driven by the *absolute* cursor position read this frame
    // (e.g. from Window::getCursorPos()); the camera itself tracks the last
    // position and derives a delta, so callers don't need to manage that
    // state. The very first call after construction (or after
    // resetMouseTracking()) only primes lastX_/lastY_ and applies no
    // rotation, avoiding the classic "huge jump on the first frame" bug that
    // comes from differencing against an uninitialized last-position.
    void processMouseInput(double xpos, double ypos);

    // Discards the tracked last cursor position so the next
    // processMouseInput() call primes instead of jumping. Useful if a caller
    // stops driving the camera from real input for a while (e.g. switching
    // into the scripted demo path) and later resumes.
    void resetMouseTracking() { firstMouse_ = true; }

    // Directly places the camera and re-derives yaw/pitch so it faces
    // `target`. Used for the initial camera pose and for the headless demo
    // path (ENGINE_APPLICATION's scripted orbit) -- both want "stand here,
    // look at the cube" without hand-computing yaw/pitch.
    void setPositionLookingAt(const glm::vec3& position, const glm::vec3& target);

    const glm::vec3& position() const { return position_; }
    const glm::vec3& front() const { return front_; }
    float yaw() const { return yawDeg_; }
    float pitch() const { return pitchDeg_; }

    void setMovementSpeed(float unitsPerSecond) { movementSpeed_ = unitsPerSecond; }
    void setMouseSensitivity(float degreesPerPixel) { mouseSensitivity_ = degreesPerPixel; }
    void setFov(float fovYDeg) { fovYDeg_ = fovYDeg; }
    void setClipPlanes(float nearPlane, float farPlane) {
        nearPlane_ = nearPlane;
        farPlane_ = farPlane;
    }

private:
    // Recomputes front_/right_/up_ from yawDeg_/pitchDeg_. right_ is always
    // front_ x worldUp_, then up_ is right_ x front_ -- deriving up_ this way
    // (rather than using worldUp_ directly as the camera's up) keeps it
    // orthogonal to front_ even as pitch changes, and because pitch is
    // clamped below +/-90 degrees, front_ never becomes parallel to worldUp_,
    // so that cross product never degenerates to a near-zero vector.
    void updateVectors();

    glm::vec3 position_;
    glm::vec3 front_{0.0f, 0.0f, -1.0f};
    glm::vec3 up_{0.0f, 1.0f, 0.0f};
    glm::vec3 right_{1.0f, 0.0f, 0.0f};
    // World-space up, used only to derive right_ = front_ x worldUp_ each
    // time updateVectors() runs (not a class constant: GLM's vec3 isn't
    // reliably usable in a static constexpr class member across compilers,
    // and this is cheap enough to just store as a regular member).
    const glm::vec3 worldUp_{0.0f, 1.0f, 0.0f};

    float yawDeg_;
    float pitchDeg_;
    // Kept strictly less than 90 so front_'s y-component never reaches +/-1,
    // which is what would make front_ parallel to worldUp_ and degenerate
    // the right_ = front_ x worldUp_ cross product to (0,0,0).
    static constexpr float kMaxPitchDegrees = 89.0f;

    float movementSpeed_ = 3.0f;       // world units per second
    float mouseSensitivity_ = 0.1f;    // degrees per pixel of mouse movement
    float fovYDeg_ = 60.0f;
    float nearPlane_ = 0.1f;
    float farPlane_ = 100.0f;

    bool firstMouse_ = true;
    double lastMouseX_ = 0.0;
    double lastMouseY_ = 0.0;
};

}  // namespace engine

#endif  // ENGINE_CAMERA_HPP
