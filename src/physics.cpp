// Phase 8e: stepPhysics()'s own implementation -- see physics.hpp for the
// full design writeup. Deliberately depends on nothing but ecs.hpp and
// transform.hpp (both plain data, no GL/Window dependency at all -- see
// ecs.hpp's own header comment), the same way scene_serialization.cpp
// stays GL-free so tests/physics_test.cpp can link against this file alone
// and exercise the real gravity/collision code with no live OpenGL context.
//
// Phase 14b design decision: this function is deliberately NOT made aware
// of the new Parent/resolveWorldMatrix() hierarchy (transform_hierarchy.hpp)
// -- it still reads and writes only a RigidBody entity's own LOCAL
// Transform, exactly as Phase 8e left it, with no #include of
// transform_hierarchy.hpp added here at all. A parented, physics-simulated
// entity (e.g. a falling object whose Parent is some moving platform) would
// need gravity/collision to reason in the parent's world space rather than
// the child's local space -- relative-velocity transforms, converting
// world-space gravity into whatever space the parent's own motion defines,
// etc. -- which is real, separate scope this phase's own "basic
// physics/collision" boundary (see physics.hpp's own "What this
// deliberately IS/IS NOT" comment) doesn't call for. What this phase DOES
// support: a static (no RigidBody) entity parented under a RigidBody entity
// correctly rides along with it VISUALLY, because resolveWorldMatrix() is
// consulted at render time, after stepPhysics() has already updated the
// parent's own local Transform for this frame -- see
// assets/scenes/default.json's "parented_demo_cube" entity (parented to
// "falling_cube") for exactly that demonstration. If a later phase actually
// needs parent-aware physics, this function -- and this comment -- is where
// that scope begins.

#include "engine/physics.hpp"

#include "engine/ecs.hpp"
#include "engine/transform.hpp"

namespace engine {

void stepPhysics(EntityRegistry& registry, float deltaTime, float groundY) {
    registry.each<RigidBody>([&](EntityId id, RigidBody& body) {
        Transform* transform = registry.getComponent<Transform>(id);
        if (transform == nullptr) {
            return;
        }

        if (body.useGravity) {
            body.velocity.y -= kGravityAcceleration * deltaTime;
        }

        // Phase 18c: terminal-velocity clamp -- see kTerminalFallSpeed's own
        // physics.hpp comment for why this is unconditional (not gated on
        // useGravity above): a scene-authored initial "velocity" could hand
        // an entity an already-huge downward speed with useGravity=false,
        // and this clamp should bound that just as much as gravity-driven
        // acceleration. Only ever clamps a downward (negative) velocity.y --
        // an upward velocity.y (e.g. a scene-authored upward launch) is left
        // completely untouched, since "terminal fall speed" only ever
        // describes how fast something can be FALLING.
        if (body.velocity.y < -kTerminalFallSpeed) {
            body.velocity.y = -kTerminalFallSpeed;
        }

        glm::vec3 position = transform->position() + body.velocity * deltaTime;

        const Collider* collider = registry.getComponent<Collider>(id);
        if (collider != nullptr) {
            const float restY = groundY + collider->halfExtent;
            if (position.y <= restY) {
                position.y = restY;
                body.velocity.y = 0.0f;

                // Phase 18c: ground friction -- decelerate horizontal (X/Z)
                // velocity toward zero while resting on the ground this
                // step. See kGroundFriction's own physics.hpp comment for
                // the deceleration rate's rationale, and stepPhysics()'s own
                // physics.hpp comment for why this treats X/Z as one
                // resultant vector rather than decelerating each axis
                // independently. Clamped ("clamped kinetic friction," not a
                // `velocity *= factor` exponential decay) so a single step's
                // deceleration can never exceed the horizontal speed it's
                // being applied to -- that would overshoot past zero and
                // reverse the direction of motion, which real friction never
                // does: it brings a sliding object to a stop and then holds
                // it there, it doesn't fling it backward.
                const glm::vec3 horizontalVelocity(body.velocity.x, 0.0f, body.velocity.z);
                const float horizontalSpeed = glm::length(horizontalVelocity);
                if (horizontalSpeed > 0.0f) {
                    const float deceleration = kGroundFriction * deltaTime;
                    const float newSpeed = (horizontalSpeed > deceleration) ? (horizontalSpeed - deceleration) : 0.0f;
                    const glm::vec3 newHorizontalVelocity = horizontalVelocity * (newSpeed / horizontalSpeed);
                    body.velocity.x = newHorizontalVelocity.x;
                    body.velocity.z = newHorizontalVelocity.z;
                }
            }
        }

        transform->setPosition(position);
    });
}

// Phase 14e: see physics.hpp's own extensive comment on this function for
// the full design rationale -- this is deliberately the one place that ever
// calls addComponent<RigidBody>()/addComponent<Collider>()/
// removeComponent<RigidBody>() in service of the Inspector's "Static
// (Immovable)" toggle, rather than editor_ui.cpp reaching into
// EntityRegistry's template API directly.
void setEntityStatic(EntityRegistry& registry, EntityId id, bool makeStatic) {
    if (makeStatic) {
        registry.removeComponent<RigidBody>(id);
        if (!registry.hasComponent<Collider>(id)) {
            registry.addComponent<Collider>(id, Collider{});
        }
    } else {
        if (!registry.hasComponent<RigidBody>(id)) {
            registry.addComponent<RigidBody>(id, RigidBody{});
        }
    }
}

}  // namespace engine
