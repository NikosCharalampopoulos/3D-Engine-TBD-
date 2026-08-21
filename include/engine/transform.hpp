#ifndef ENGINE_TRANSFORM_HPP
#define ENGINE_TRANSFORM_HPP

// A plain position/rotation/scale bundle plus the model matrix built from
// it. Deliberately NOT a scene graph node -- no parent/child, no dirty-flag
// caching, just data + one matrix-building function. Phase 5 (or whenever
// hierarchical transforms are actually needed) is the right place to grow
// this into a tree.
//
// Rotation is stored as a glm::quat rather than Euler angles specifically to
// avoid gimbal lock: composing several rotations (as Camera-adjacent code or
// a later animation system will want to) is well-defined and commutes the
// way you'd expect for quaternions, whereas Euler angles silently lose a
// degree of freedom when two axes align. The tradeoff is that quaternions
// aren't directly human-readable (no "45 degrees around Y" field to print or
// tweak in isolation) -- setRotationEuler()/rotate() below exist so callers
// can still think in degrees-around-an-axis without storing Euler state.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine {

class Transform {
public:
    Transform() = default;

    void setPosition(const glm::vec3& position) { position_ = position; }
    void setRotation(const glm::quat& rotation) { rotation_ = rotation; }
    void setScale(const glm::vec3& scale) { scale_ = scale; }

    void translate(const glm::vec3& delta) { position_ += delta; }

    // Applies an additional angleDeg-around-axis rotation on top of whatever
    // rotation is already stored, composed so it happens in world space
    // *after* the existing rotation (i.e. new_rotation = incoming * old) --
    // the standard "spin the object a bit more" convention.
    void rotate(float angleDeg, const glm::vec3& axis) {
        rotation_ = glm::angleAxis(glm::radians(angleDeg), glm::normalize(axis)) * rotation_;
    }

    const glm::vec3& position() const { return position_; }
    const glm::quat& rotation() const { return rotation_; }
    const glm::vec3& scale() const { return scale_; }

    // Builds translate * rotate * scale, in that order (right-to-left
    // application to a vertex: scale first, then rotate, then translate --
    // the standard TRS order). Getting this order wrong is a classic silent
    // bug: with uniform scale and a rotation-then-translation it can look
    // fine by coincidence, but non-uniform scale combined with rotation (or
    // translate-then-rotate, which orbits the object around the origin
    // instead of spinning it in place) reveals the mistake immediately.
    glm::mat4 getModelMatrix() const {
        const glm::mat4 t = glm::translate(glm::mat4(1.0f), position_);
        const glm::mat4 r = glm::mat4_cast(rotation_);
        const glm::mat4 s = glm::scale(glm::mat4(1.0f), scale_);
        return t * r * s;
    }

private:
    glm::vec3 position_{0.0f, 0.0f, 0.0f};
    glm::quat rotation_{1.0f, 0.0f, 0.0f, 0.0f};  // identity (w, x, y, z)
    glm::vec3 scale_{1.0f, 1.0f, 1.0f};
};

}  // namespace engine

#endif  // ENGINE_TRANSFORM_HPP
