// Phase 14f's own test: exercises engine::EntityRegistry::destroyEntity()
// (include/engine/ecs.hpp -- fully header-only/templated, unlike every other
// component logic this project tests, so there is no matching src/ecs.cpp to
// link here; this test #includes only the header, the same way any other
// translation unit that uses EntityRegistry does). Same "plain executable,
// needs no live window/GL context/GPU" shape as physics_test/
// transform_hierarchy_test/scene_hierarchy_test (see those files' own header
// comments) -- ecs.hpp depends on nothing but the C++ standard library.
//
// Deliberately uses its own small test-only component types (Position/
// Velocity/Tag below), NOT Transform/ModelComponent/NameComponent from this
// engine's real component set -- proving destroyEntity() generically clears
// EVERY pool a registry happens to hold, for ANY T a caller ever registered,
// without this test (or destroyEntity() itself) needing to hardcode a single
// real component type by name. That genericity is the whole point of this
// phase's ComponentPoolBase design (see ecs.hpp's own Phase 14f comment) --
// a test built only out of Transform/ModelComponent/etc. would leave that
// central claim unverified.

#include "engine/ecs.hpp"

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

// Three unrelated, this-test-only component types (deliberately not reusing
// each other's shape) so a bug that only cleared the FIRST pool
// destroyEntity() happened to iterate (e.g. an early `return` instead of
// continuing the loop) would still leave the other two behind and get
// caught.
struct Position {
    float x = 0.0f;
};
struct Velocity {
    float dx = 0.0f;
};
struct Tag {
    std::string label;
};

}  // namespace

int main() {
    // --- destroyEntity() clears every pool the destroyed entity was in ----
    {
        engine::EntityRegistry registry;

        const engine::EntityId victim = registry.create();
        registry.addComponent<Position>(victim, Position{1.0f});
        registry.addComponent<Velocity>(victim, Velocity{2.0f});
        registry.addComponent<Tag>(victim, Tag{"victim"});

        expectTrue(registry.hasComponent<Position>(victim) && registry.hasComponent<Velocity>(victim) &&
                       registry.hasComponent<Tag>(victim),
                   "sanity: victim has all three components before destroyEntity()");

        registry.destroyEntity(victim);

        expectTrue(!registry.hasComponent<Position>(victim), "destroyEntity() removes Position");
        expectTrue(!registry.hasComponent<Velocity>(victim), "destroyEntity() removes Velocity");
        expectTrue(!registry.hasComponent<Tag>(victim), "destroyEntity() removes Tag");
        expectTrue(registry.getComponent<Position>(victim) == nullptr,
                   "getComponent<Position>() returns nullptr for a destroyed entity, not a dangling pointer");
    }

    // --- destroyEntity() only touches the targeted entity -----------------
    // A swap-remove (ComponentPool::remove()'s own sparse-set mechanism)
    // moves a DIFFERENT entity's data into the removed slot -- this proves
    // that move preserves the surviving entity's actual values, not just its
    // presence.
    {
        engine::EntityRegistry registry;

        const engine::EntityId victim = registry.create();
        registry.addComponent<Position>(victim, Position{1.0f});
        registry.addComponent<Tag>(victim, Tag{"victim"});

        const engine::EntityId survivor = registry.create();
        registry.addComponent<Position>(survivor, Position{99.0f});
        registry.addComponent<Velocity>(survivor, Velocity{7.5f});
        registry.addComponent<Tag>(survivor, Tag{"survivor"});

        registry.destroyEntity(victim);

        expectTrue(registry.hasComponent<Position>(survivor) && registry.hasComponent<Velocity>(survivor) &&
                       registry.hasComponent<Tag>(survivor),
                   "destroying victim leaves survivor's components intact");
        const Position* survivorPosition = registry.getComponent<Position>(survivor);
        const Tag* survivorTag = registry.getComponent<Tag>(survivor);
        expectTrue(survivorPosition != nullptr && survivorPosition->x == 99.0f,
                   "survivor's Position value is unchanged (99.0), not corrupted by victim's swap-removal");
        expectTrue(survivorTag != nullptr && survivorTag->label == "survivor",
                   "survivor's Tag value is unchanged (\"survivor\"), not corrupted by victim's swap-removal");
    }

    // --- destroyEntity() on an entity with NO components is a safe no-op --
    {
        engine::EntityRegistry registry;
        // Touch every pool type once (on a different entity) so pools_ is
        // non-empty -- the scenario destroyEntity() actually has to loop
        // over pools and find nothing to remove in each one, not just an
        // empty pools_ map trivially having nothing to iterate at all.
        const engine::EntityId other = registry.create();
        registry.addComponent<Position>(other, Position{5.0f});

        const engine::EntityId empty = registry.create();
        registry.destroyEntity(empty);  // must not crash

        expectTrue(registry.hasComponent<Position>(other),
                   "destroying a component-less entity leaves an unrelated entity's components untouched");
    }

    // --- destroyEntity() is idempotent -------------------------------------
    {
        engine::EntityRegistry registry;
        const engine::EntityId victim = registry.create();
        registry.addComponent<Position>(victim, Position{3.0f});

        registry.destroyEntity(victim);
        registry.destroyEntity(victim);  // second call: must not crash or misbehave

        expectTrue(!registry.hasComponent<Position>(victim), "calling destroyEntity() twice is a harmless no-op the "
                                                              "second time");
    }

    if (failures == 0) {
        std::printf("ecs_test: all checks passed\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "ecs_test: %d check(s) failed\n", failures);
    return EXIT_FAILURE;
}
