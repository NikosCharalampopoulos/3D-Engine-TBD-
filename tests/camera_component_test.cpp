// Phase 15c's own test: exercises engine::CameraComponent
// (include/engine/camera_component.hpp) in isolation. Unlike light_test.cpp
// (engine::collectPointLights()/resolveActiveDirectionalLight(), both real
// functions in src/light.cpp), CameraComponent has no logic of its own to
// call -- it's a plain data struct, the same "header-only, no matching .cpp"
// shape ecs_test.cpp's own header comment already establishes for ecs.hpp
// itself. So what this file actually verifies is:
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
//
// No GL/Window dependency at all -- ecs.hpp/transform.hpp/camera_component.hpp
// are all header-only or GL-free, so (like every other *_test.cpp here) this
// needs no live GL context/GPU either -- just GLM for glm::vec3/glm::quat
// (via Transform).

#include "engine/camera_component.hpp"

#include "engine/ecs.hpp"
#include "engine/transform.hpp"

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

    if (failures == 0) {
        std::printf("camera_component_test: all checks passed\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "camera_component_test: %d check(s) failed\n", failures);
    return EXIT_FAILURE;
}
