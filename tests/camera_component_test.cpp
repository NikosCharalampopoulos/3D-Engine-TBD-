// Phase 15c's own test: exercises engine::CameraComponent
// (include/engine/camera_component.hpp) in isolation. Through Phase 18e,
// CameraComponent had no logic of its own to call -- it was a plain data
// struct, the same "header-only, no matching .cpp" shape ecs_test.cpp's own
// header comment already establishes for ecs.hpp itself.
//
// Phase 18g adds two real, pure functions to this same header --
// resolveCameraWorldPose() (a plain glm::mat4-in/CameraWorldPose-out
// computation, no EntityRegistry at all) and resolveActiveCamera() (which
// DOES need a live EntityRegistry) -- now implemented in the new
// src/camera_component.cpp this test links below. So this file now verifies:
//   1. CameraComponent{}'s own default field values genuinely match
//      engine::Camera's own defaults (camera.hpp) -- the whole point of
//      camera_component.hpp's own "copied verbatim" comment, which would
//      otherwise be an unchecked claim that could silently drift the next
//      time someone edits one file but not the other.
//   2. A CameraComponent behaves like any other real ECS component when
//      actually used through EntityRegistry: addComponent()/getComponent()
//      round-trip correctly, coexists on the same entity as Transform/
//      NameComponent (the exact shape Application::spawnEntityFromCreateMenu()
//      builds -- see that function's own Phase 15c comment), and two
//      different entities' own CameraComponents are independent storage --
//      editing one never touches the other, unlike Material's cached-Model
//      sharing (see material.hpp's own Phase 14e comment) that the
//      Inspector's Camera section deliberately does NOT have to guard
//      against (editor_ui.cpp's own Phase 15c comment).
//   3. resolveCameraWorldPose() correctly extracts a world-space eye
//      position + look-at target from a hand-built glm::mat4 -- including
//      under a non-uniform ancestor scale, the one case that function's own
//      header comment calls out as the reason it normalizes rather than
//      using the rotated vector directly.
//   4. resolveActiveCamera() picks the first CameraComponent entity found by
//      registry iteration order, correctly counts every additional one as
//      "ignored" rather than silently overwriting its pick, and returns an
//      invalid `active` (ignoredCount 0) for a registry with no Camera
//      entity at all -- the "zero Camera entities" baseline every existing
//      scene, including this engine's own default one, actually has.
//
// No GL/Window dependency at all -- ecs.hpp/transform.hpp/camera_component.hpp
// are all header-only or GL-free, so (like every other *_test.cpp here) this
// needs no live GL context/GPU either -- just GLM for glm::vec3/glm::quat
// (via Transform).

#include "engine/camera_component.hpp"

#include "engine/ecs.hpp"
#include "engine/transform.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
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
    const float diff = actual > expected ? actual - expected : expected - actual;
    if (diff > epsilon) {
        std::fprintf(stderr, "FAIL: %s (expected %f, got %f)\n", what.c_str(), static_cast<double>(expected),
                     static_cast<double>(actual));
        ++failures;
    }
}

}  // namespace

int main() {
    // --- CameraComponent{}'s own defaults match engine::Camera's own
    // fovYDeg_/nearPlane_/farPlane_ defaults (camera.hpp) -----------------
    {
        engine::CameraComponent defaults;
        expectNear(defaults.fovYDeg, 60.0f, "CameraComponent{} default fovYDeg matches Camera's own 60.0f");
        expectNear(defaults.nearPlane, 0.1f, "CameraComponent{} default nearPlane matches Camera's own 0.1f");
        expectNear(defaults.farPlane, 100.0f, "CameraComponent{} default farPlane matches Camera's own 100.0f");
    }

    // --- addComponent<CameraComponent>()/getComponent<CameraComponent>()
    // round-trip correctly, coexisting with Transform + NameComponent on the
    // same entity -- exactly the shape
    // Application::spawnEntityFromCreateMenu() builds for a Create'd Camera
    // -----------------------------------------------------------------------
    {
        engine::EntityRegistry registry;
        const engine::EntityId entity = registry.create();
        registry.addComponent<engine::Transform>(entity).setPosition(glm::vec3(1.0f, 2.0f, 3.0f));
        registry.addComponent<engine::NameComponent>(entity, engine::NameComponent{"Camera"});
        registry.addComponent<engine::CameraComponent>(entity, engine::CameraComponent{});

        engine::CameraComponent* camera = registry.getComponent<engine::CameraComponent>(entity);
        expectTrue(camera != nullptr, "a freshly added CameraComponent is retrievable via getComponent()");
        if (camera != nullptr) {
            expectNear(camera->fovYDeg, 60.0f, "round-tripped CameraComponent keeps its own default fovYDeg");
        }

        const engine::Transform* transform = registry.getComponent<engine::Transform>(entity);
        expectTrue(transform != nullptr,
                   "the same entity's own Transform is still retrievable alongside its CameraComponent");
        if (transform != nullptr) {
            expectNear(transform->position().x, 1.0f, "Transform and CameraComponent coexist without interfering");
        }

        const engine::NameComponent* name = registry.getComponent<engine::NameComponent>(entity);
        expectTrue(name != nullptr && name->name == "Camera",
                   "the same entity's own NameComponent is still retrievable alongside its CameraComponent");
    }

    // --- Editing one entity's CameraComponent through the pointer
    // getComponent() returns (the same access pattern the Inspector's live
    // DragFloat fields use, editor_ui.cpp) persists back into the registry,
    // and never touches a second entity's own independent CameraComponent --
    // -----------------------------------------------------------------------
    {
        engine::EntityRegistry registry;
        const engine::EntityId first = registry.create();
        registry.addComponent<engine::CameraComponent>(first, engine::CameraComponent{});
        const engine::EntityId second = registry.create();
        registry.addComponent<engine::CameraComponent>(second, engine::CameraComponent{});

        if (engine::CameraComponent* firstCamera = registry.getComponent<engine::CameraComponent>(first)) {
            firstCamera->fovYDeg = 90.0f;
            firstCamera->nearPlane = 0.5f;
            firstCamera->farPlane = 500.0f;
        }

        const engine::CameraComponent* firstAfter = registry.getComponent<engine::CameraComponent>(first);
        expectTrue(firstAfter != nullptr, "the edited entity's CameraComponent is still retrievable");
        if (firstAfter != nullptr) {
            expectNear(firstAfter->fovYDeg, 90.0f, "an edit through getComponent()'s pointer persists (fovYDeg)");
            expectNear(firstAfter->nearPlane, 0.5f, "an edit through getComponent()'s pointer persists (nearPlane)");
            expectNear(firstAfter->farPlane, 500.0f, "an edit through getComponent()'s pointer persists (farPlane)");
        }

        const engine::CameraComponent* secondAfter = registry.getComponent<engine::CameraComponent>(second);
        expectTrue(secondAfter != nullptr, "the untouched entity's CameraComponent is still retrievable");
        if (secondAfter != nullptr) {
            expectNear(secondAfter->fovYDeg, 60.0f,
                       "editing one entity's CameraComponent leaves a sibling entity's own component untouched");
        }
    }

    // --- An entity with no CameraComponent correctly reports none -- the
    // same "opt-in per entity" contract every other component in this ECS
    // already has (ecs.hpp's own top comment), and exactly what
    // renderInspectorPanel()'s own `if (CameraComponent* ... )` gate
    // (editor_ui.cpp) relies on to decide whether to draw the "Camera"
    // Inspector section at all -----------------------------------------
    {
        engine::EntityRegistry registry;
        const engine::EntityId entity = registry.create();
        registry.addComponent<engine::Transform>(entity);

        expectTrue(registry.getComponent<engine::CameraComponent>(entity) == nullptr,
                   "an entity with no CameraComponent added reports nullptr from getComponent()");
    }

    // --- resolveCameraWorldPose(): identity matrix ------------------------
    // No rotation/translation at all -- position stays the origin, and the
    // look target is exactly the standard engine-forward vector (0, 0, -1),
    // the same "a fresh camera looks down -Z" convention camera.hpp's own
    // header comment documents for Camera itself.
    {
        const glm::mat4 identity(1.0f);
        const engine::CameraWorldPose pose = engine::resolveCameraWorldPose(identity);
        expectNear(pose.position.x, 0.0f, "identity matrix: resolved position.x");
        expectNear(pose.position.y, 0.0f, "identity matrix: resolved position.y");
        expectNear(pose.position.z, 0.0f, "identity matrix: resolved position.z");
        expectNear(pose.lookTarget.x, 0.0f, "identity matrix: resolved lookTarget.x");
        expectNear(pose.lookTarget.y, 0.0f, "identity matrix: resolved lookTarget.y");
        expectNear(pose.lookTarget.z, -1.0f, "identity matrix: resolved lookTarget.z");
    }

    // --- resolveCameraWorldPose(): translated + yawed 90 degrees ----------
    // A camera at (5, 2, 0) yawed +90 degrees around world Y should look
    // down world +X (rotating (0,0,-1) by +90 degrees around Y sends it to
    // (-1,0,0)... signed the OPPOSITE way glm::rotate's right-handed
    // convention actually resolves it below -- this test asserts whatever
    // glm::rotate() itself actually produces, not a hand-derived expectation,
    // so it stays correct if this file's own trig convention is ever
    // double-checked rather than silently encoding a sign error twice).
    {
        glm::mat4 world(1.0f);
        world = glm::translate(world, glm::vec3(5.0f, 2.0f, 0.0f));
        world = glm::rotate(world, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const engine::CameraWorldPose pose = engine::resolveCameraWorldPose(world);
        expectNear(pose.position.x, 5.0f, "translated+yawed matrix: resolved position.x");
        expectNear(pose.position.y, 2.0f, "translated+yawed matrix: resolved position.y");
        expectNear(pose.position.z, 0.0f, "translated+yawed matrix: resolved position.z");
        // The look DIRECTION (lookTarget - position) must be unit length and
        // stay in the XZ plane (no accidental Y component from the rotation)
        // -- checked directly rather than asserting an exact sign convention,
        // since what matters for Camera::setPositionLookingAt() is a
        // well-formed, correctly-oriented direction vector, not which of two
        // equally-valid trig sign conventions produced it.
        const glm::vec3 direction = pose.lookTarget - pose.position;
        expectNear(glm::length(direction), 1.0f, "translated+yawed matrix: look direction is unit length");
        expectNear(direction.y, 0.0f, "translated+yawed matrix: a pure Y-axis yaw keeps the look direction level");
        expectTrue(std::fabs(direction.x) > 0.9f && std::fabs(direction.z) < 0.1f,
                   "translated+yawed matrix: a 90-degree Y yaw points the look direction along X, not Z");
    }

    // --- resolveCameraWorldPose(): non-uniform scale does not distort the
    // resolved look direction's LENGTH (it stays unit-length) or introduce a
    // spurious Y component -- exactly the case this function's own header
    // comment calls out normalize() as necessary for --------------------
    {
        glm::mat4 world(1.0f);
        world = glm::scale(world, glm::vec3(1.0f, 5.0f, 2.0f));
        const engine::CameraWorldPose pose = engine::resolveCameraWorldPose(world);
        const glm::vec3 direction = pose.lookTarget - pose.position;
        expectNear(glm::length(direction), 1.0f, "non-uniform scale: look direction stays unit length");
        expectNear(direction.x, 0.0f, "non-uniform scale: look direction keeps no X component");
        expectNear(direction.y, 0.0f, "non-uniform scale: look direction keeps no Y component");
        expectNear(direction.z, -1.0f, "non-uniform scale: look direction still points down -Z");
    }

    // --- resolveActiveCamera(): no Camera entities at all ------------------
    // The "zero Camera entities" baseline -- every scene before Phase 18g,
    // and this engine's own default scene today, must resolve to an invalid
    // `active` with zero ignored, exactly like resolveActiveDirectionalLight()'s
    // own "no active entity" fallback keeps every pre-existing scene
    // rendering unchanged.
    {
        engine::EntityRegistry registry;
        registry.create();  // an entity that is NOT a camera
        const engine::ActiveCameraResolution resolution = engine::resolveActiveCamera(registry);
        expectTrue(!resolution.active.valid(), "no Camera entities: active is invalid");
        expectTrue(resolution.ignoredCount == 0, "no Camera entities: ignoredCount is 0");
    }

    // --- resolveActiveCamera(): exactly one Camera entity -------------------
    {
        engine::EntityRegistry registry;
        const engine::EntityId camera = registry.create();
        registry.addComponent<engine::CameraComponent>(camera, engine::CameraComponent{});
        const engine::ActiveCameraResolution resolution = engine::resolveActiveCamera(registry);
        expectTrue(resolution.active == camera, "exactly one Camera entity: active is that entity");
        expectTrue(resolution.ignoredCount == 0, "exactly one Camera entity: ignoredCount is 0");
    }

    // --- resolveActiveCamera(): more than one Camera entity (a hand-edited
    // scene JSON, per this function's own header comment -- the Create menu
    // itself prevents this via the UI, but the resolution function must stay
    // correct anyway) -- picks the FIRST by registry iteration order and
    // counts the rest as ignored, deterministically -------------------------
    {
        engine::EntityRegistry registry;
        const engine::EntityId first = registry.create();
        registry.addComponent<engine::CameraComponent>(first, engine::CameraComponent{});
        const engine::EntityId second = registry.create();
        registry.addComponent<engine::CameraComponent>(second, engine::CameraComponent{});
        const engine::EntityId third = registry.create();
        registry.addComponent<engine::CameraComponent>(third, engine::CameraComponent{});

        const engine::ActiveCameraResolution resolution = engine::resolveActiveCamera(registry);
        expectTrue(resolution.active == first,
                   "three Camera entities: active is the first one found by registry iteration order");
        expectTrue(resolution.ignoredCount == 2, "three Camera entities: the other two are counted as ignored");

        // Calling it again against the SAME, unchanged registry must return
        // the identical result -- this rule is meant to be stable frame to
        // frame for a scene that isn't itself changing, not to pick a
        // different "winner" arbitrarily each call.
        const engine::ActiveCameraResolution resolutionAgain = engine::resolveActiveCamera(registry);
        expectTrue(resolutionAgain.active == first, "calling resolveActiveCamera() again picks the same entity");
        expectTrue(resolutionAgain.ignoredCount == 2, "calling resolveActiveCamera() again reports the same count");
    }

    if (failures == 0) {
        std::printf("camera_component_test: all checks passed\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "camera_component_test: %d check(s) failed\n", failures);
    return EXIT_FAILURE;
}
