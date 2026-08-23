// Phase 8e's own test: exercises engine::stepPhysics() (src/physics.cpp)
// against a deterministic, hand-computable scenario -- same "plain
// executable, links only the pure logic file it's testing" shape as
// scene_serialization_test/input_action_map_test (see those files' own
// header comments). physics.cpp depends only on ecs.hpp/transform.hpp,
// neither of which touch GL/Window at all (see ecs.hpp's own header
// comment), so this needs no live window/GL context/GPU either.
//
// Scenario: one entity, a RigidBody (gravity on, zero initial velocity) and
// a Collider (halfExtent 0.5), whose Transform starts at y = 2.0 above a
// ground plane at y = 0.0 -- so it must fall exactly 1.5 world units
// (2.0 - restY, restY = groundY + halfExtent = 0.5) before its collider's
// bottom face reaches the ground. stepPhysics() is called with a fixed,
// known deltaTime (1/60 s) enough times to compute the exact expected
// trajectory by hand via the same semi-implicit-Euler recurrence
// physics.hpp documents (v += g*dt; y -= v*dt each step) -- run first for a
// small number of steps still short of landing (checked against that exact
// hand-computed position/velocity), then for enough additional steps that
// it must have landed by then (checked against the exact rest condition:
// position.y == groundY + halfExtent, velocity.y == 0).

#include "engine/physics.hpp"

#include "engine/ecs.hpp"
#include "engine/transform.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int failures = 0;

void expectTrue(bool condition, const std::string& what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

void expectNear(float actual, float expected, const std::string& what, float epsilon = 1e-4f) {
    expectTrue(glm::epsilonEqual(actual, expected, epsilon), what + " (expected " + std::to_string(expected) +
                                                                   ", got " + std::to_string(actual) + ")");
}

// Computes the exact semi-implicit-Euler trajectory stepPhysics() itself
// follows (see physics.hpp's own top comment: velocity updated from
// acceleration BEFORE it moves position), so the test's expectations are
// derived from the same recurrence being tested, not a different
// (e.g. closed-form continuous) approximation of it -- an object falling
// under discrete semi-implicit Euler does not sit exactly on the closed-form
// analytic curve, so comparing against that would be comparing against the
// wrong number.
struct ExpectedState {
    float y;
    float v;
};

ExpectedState expectedFreeFall(float y0, float deltaTime, int steps) {
    float y = y0;
    float v = 0.0f;
    for (int i = 0; i < steps; ++i) {
        v += engine::kGravityAcceleration * deltaTime;
        y -= v * deltaTime;
    }
    return ExpectedState{y, -v};  // stepPhysics stores velocity.y negative while falling
}

}  // namespace

int main() {
    constexpr float kDeltaTime = 1.0f / 60.0f;
    constexpr float kGroundY = 0.0f;
    constexpr float kHalfExtent = 0.5f;
    constexpr float kStartY = 2.0f;
    constexpr float kRestY = kGroundY + kHalfExtent;

    // --- Still falling: a handful of steps, nowhere near the ground yet ---
    {
        engine::EntityRegistry registry;
        const engine::EntityId id = registry.create();
        registry.addComponent<engine::Transform>(id).setPosition(glm::vec3(0.0f, kStartY, 0.0f));
        registry.addComponent<engine::RigidBody>(id, engine::RigidBody{});
        registry.addComponent<engine::Collider>(id, engine::Collider{kHalfExtent});

        constexpr int kSteps = 5;
        for (int i = 0; i < kSteps; ++i) {
            engine::stepPhysics(registry, kDeltaTime, kGroundY);
        }

        const engine::Transform* transform = registry.getComponent<engine::Transform>(id);
        const engine::RigidBody* body = registry.getComponent<engine::RigidBody>(id);
        const ExpectedState expected = expectedFreeFall(kStartY, kDeltaTime, kSteps);

        expectTrue(transform != nullptr && body != nullptr, "entity still has its Transform/RigidBody after 5 steps");
        if (transform != nullptr && body != nullptr) {
            expectNear(transform->position().y, expected.y, "y after 5 steps matches hand-computed free fall");
            expectNear(body->velocity.y, expected.v, "velocity.y after 5 steps matches hand-computed free fall");
            expectTrue(transform->position().y > kRestY, "still above the ground after only 5 steps");
            expectTrue(body->velocity.y < 0.0f, "still moving downward after only 5 steps");
        }
    }

    // --- Settled: enough steps that it must have landed by now -------------
    {
        engine::EntityRegistry registry;
        const engine::EntityId id = registry.create();
        registry.addComponent<engine::Transform>(id).setPosition(glm::vec3(0.0f, kStartY, 0.0f));
        registry.addComponent<engine::RigidBody>(id, engine::RigidBody{});
        registry.addComponent<engine::Collider>(id, engine::Collider{kHalfExtent});

        // 1.5 world units of free fall under 9.81 m/s^2 lands in well under
        // a second of simulated time (~0.55s); 90 steps of 1/60s (1.5s
        // simulated) is comfortably past that, matching this phase's own
        // headless verification's ENGINE_MAX_FRAMES=90 checkpoint.
        constexpr int kSteps = 90;
        for (int i = 0; i < kSteps; ++i) {
            engine::stepPhysics(registry, kDeltaTime, kGroundY);
        }

        const engine::Transform* transform = registry.getComponent<engine::Transform>(id);
        const engine::RigidBody* body = registry.getComponent<engine::RigidBody>(id);

        expectTrue(transform != nullptr && body != nullptr, "entity still has its Transform/RigidBody after 90 steps");
        if (transform != nullptr && body != nullptr) {
            expectNear(transform->position().y, kRestY, "settled exactly at groundY + halfExtent, not sunk in/floating");
            expectNear(body->velocity.y, 0.0f, "velocity.y zeroed once at rest");
        }
    }

    // --- A RigidBody with no Collider never collides, just keeps falling --
    {
        engine::EntityRegistry registry;
        const engine::EntityId id = registry.create();
        registry.addComponent<engine::Transform>(id).setPosition(glm::vec3(0.0f, kStartY, 0.0f));
        registry.addComponent<engine::RigidBody>(id, engine::RigidBody{});

        for (int i = 0; i < 90; ++i) {
            engine::stepPhysics(registry, kDeltaTime, kGroundY);
        }

        const engine::Transform* transform = registry.getComponent<engine::Transform>(id);
        expectTrue(transform != nullptr, "collider-less entity still has its Transform after 90 steps");
        if (transform != nullptr) {
            expectTrue(transform->position().y < kGroundY,
                       "a RigidBody with no Collider falls straight through groundY (no collision check runs)");
        }
    }

    // --- useGravity = false: no acceleration, no motion at all -------------
    {
        engine::EntityRegistry registry;
        const engine::EntityId id = registry.create();
        registry.addComponent<engine::Transform>(id).setPosition(glm::vec3(0.0f, kStartY, 0.0f));
        registry.addComponent<engine::RigidBody>(id, engine::RigidBody{glm::vec3(0.0f), /*useGravity=*/false});

        for (int i = 0; i < 10; ++i) {
            engine::stepPhysics(registry, kDeltaTime, kGroundY);
        }

        const engine::Transform* transform = registry.getComponent<engine::Transform>(id);
        expectTrue(transform != nullptr, "gravity-disabled entity still has its Transform");
        if (transform != nullptr) {
            expectNear(transform->position().y, kStartY, "gravity-disabled body with zero velocity never moves");
        }
    }

    // --- Phase 14e: setEntityStatic() -- dynamic -> static -----------------
    // A RigidBody+Collider entity (like "falling_cube"): toggling static
    // removes the RigidBody (so a later stepPhysics() call no longer visits
    // it at all) and leaves the existing Collider's halfExtent untouched.
    {
        engine::EntityRegistry registry;
        const engine::EntityId id = registry.create();
        registry.addComponent<engine::Transform>(id).setPosition(glm::vec3(0.0f, kStartY, 0.0f));
        registry.addComponent<engine::RigidBody>(id, engine::RigidBody{glm::vec3(0.0f, -3.0f, 0.0f), true});
        registry.addComponent<engine::Collider>(id, engine::Collider{kHalfExtent});

        engine::setEntityStatic(registry, id, /*makeStatic=*/true);

        expectTrue(!registry.hasComponent<engine::RigidBody>(id), "static: RigidBody removed");
        expectTrue(registry.hasComponent<engine::Collider>(id), "static: existing Collider kept");
        const engine::Collider* collider = registry.getComponent<engine::Collider>(id);
        if (collider != nullptr) {
            expectNear(collider->halfExtent, kHalfExtent, "static: existing Collider::halfExtent untouched");
        }

        // Confirm the effect, not just the component bookkeeping: a
        // subsequent stepPhysics() call must leave this entity's Transform
        // completely alone now that it has no RigidBody -- the exact
        // property the Inspector's "stops responding to gravity" claim
        // depends on.
        for (int i = 0; i < 30; ++i) {
            engine::stepPhysics(registry, kDeltaTime, kGroundY);
        }
        const engine::Transform* transform = registry.getComponent<engine::Transform>(id);
        expectTrue(transform != nullptr, "static: still has its Transform after stepPhysics()");
        if (transform != nullptr) {
            expectNear(transform->position().y, kStartY, "static: position never moves once made static");
        }
    }

    // --- Phase 14e: setEntityStatic() -- static (Collider-only) -> dynamic -
    // The exact case this phase's own brief calls out: an entity with a
    // Collider but no RigidBody today ("parented_demo_cube" in its
    // unmodified scene state has neither, but this test targets the
    // Collider-only static case specifically) becomes dynamic again with
    // sensible defaults (zero velocity, gravity on), and starts falling on
    // the very next stepPhysics() call.
    {
        engine::EntityRegistry registry;
        const engine::EntityId id = registry.create();
        registry.addComponent<engine::Transform>(id).setPosition(glm::vec3(0.0f, kStartY, 0.0f));
        registry.addComponent<engine::Collider>(id, engine::Collider{kHalfExtent});

        engine::setEntityStatic(registry, id, /*makeStatic=*/false);

        expectTrue(registry.hasComponent<engine::RigidBody>(id), "dynamic: RigidBody added");
        expectTrue(registry.hasComponent<engine::Collider>(id), "dynamic: existing Collider untouched");
        const engine::RigidBody* body = registry.getComponent<engine::RigidBody>(id);
        if (body != nullptr) {
            expectNear(body->velocity.y, 0.0f, "dynamic: freshly-added RigidBody starts at zero velocity");
            expectTrue(body->useGravity, "dynamic: freshly-added RigidBody has gravity on by default");
        }

        for (int i = 0; i < 5; ++i) {
            engine::stepPhysics(registry, kDeltaTime, kGroundY);
        }
        const engine::Transform* transform = registry.getComponent<engine::Transform>(id);
        expectTrue(transform != nullptr, "dynamic: still has its Transform after stepPhysics()");
        if (transform != nullptr) {
            expectTrue(transform->position().y < kStartY, "dynamic: starts falling immediately once made dynamic");
        }
    }

    // --- Phase 14e: setEntityStatic() -- an entity with NEITHER component --
    // (e.g. "scene" or "parented_demo_cube" today) can still be turned into
    // a physics object from scratch: makeStatic=true adds a fresh
    // default-halfExtent Collider (no RigidBody to remove -- removeComponent
    // on an absent component is just a well-defined no-op, per ecs.hpp).
    {
        engine::EntityRegistry registry;
        const engine::EntityId id = registry.create();
        registry.addComponent<engine::Transform>(id).setPosition(glm::vec3(0.0f, kStartY, 0.0f));

        engine::setEntityStatic(registry, id, /*makeStatic=*/true);

        expectTrue(!registry.hasComponent<engine::RigidBody>(id), "no-physics -> static: still no RigidBody");
        expectTrue(registry.hasComponent<engine::Collider>(id), "no-physics -> static: Collider added");
        const engine::Collider* collider = registry.getComponent<engine::Collider>(id);
        if (collider != nullptr) {
            expectNear(collider->halfExtent, 0.25f, "no-physics -> static: Collider uses its own struct default");
        }
    }

    // --- Phase 14e: setEntityStatic() is idempotent -----------------------
    // Calling makeStatic=true on an already-static entity must not reset an
    // already-customized Collider::halfExtent back to the struct default.
    {
        engine::EntityRegistry registry;
        const engine::EntityId id = registry.create();
        registry.addComponent<engine::Transform>(id).setPosition(glm::vec3(0.0f, kStartY, 0.0f));
        registry.addComponent<engine::Collider>(id, engine::Collider{0.75f});

        engine::setEntityStatic(registry, id, /*makeStatic=*/true);

        const engine::Collider* collider = registry.getComponent<engine::Collider>(id);
        expectTrue(collider != nullptr, "idempotent static: Collider still present");
        if (collider != nullptr) {
            expectNear(collider->halfExtent, 0.75f, "idempotent static: pre-existing custom halfExtent untouched");
        }
    }

    if (failures == 0) {
        std::printf("physics_test: all checks passed\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "physics_test: %d check(s) failed\n", failures);
    return EXIT_FAILURE;
}
