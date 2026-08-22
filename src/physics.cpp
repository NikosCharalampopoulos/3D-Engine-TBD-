// Phase 8e: stepPhysics()'s own implementation -- see physics.hpp for the
// full design writeup. Deliberately depends on nothing but ecs.hpp and
// transform.hpp (both plain data, no GL/Window dependency at all -- see
// ecs.hpp's own header comment), the same way scene_serialization.cpp
// stays GL-free so tests/physics_test.cpp can link against this file alone
// and exercise the real gravity/collision code with no live OpenGL context.

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

}  // namespace engine
