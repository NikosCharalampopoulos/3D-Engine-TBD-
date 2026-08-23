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

        glm::vec3 position = transform->position() + body.velocity * deltaTime;

        const Collider* collider = registry.getComponent<Collider>(id);
        if (collider != nullptr) {
            const float restY = groundY + collider->halfExtent;
            if (position.y <= restY) {
                position.y = restY;
                body.velocity.y = 0.0f;
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
