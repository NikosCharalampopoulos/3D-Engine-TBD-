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

    if (failures == 0) {
        std::printf("physics_test: all checks passed\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "physics_test: %d check(s) failed\n", failures);
    return EXIT_FAILURE;
}
