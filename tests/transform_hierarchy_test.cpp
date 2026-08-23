// Phase 14b's own test: exercises engine::resolveWorldMatrix()
// (src/transform_hierarchy.cpp) against a hand-computable multi-level
// hierarchy, plus the cycle-safety and dangling-parent guards its own
// header comment documents. Same "plain executable, links only the pure
// logic file it's testing" shape as physics_test/scene_serialization_test
// (see those files' own header comments) -- transform_hierarchy.cpp depends
// only on ecs.hpp/transform.hpp/log.hpp, none of which touch GL/Window, so
// this needs no live window/GL context/GPU either.

#include "engine/transform_hierarchy.hpp"

#include "engine/ecs.hpp"
#include "engine/transform.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

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

void expectMat4Near(const glm::mat4& actual, const glm::mat4& expected, const std::string& what,
                     float epsilon = 1e-4f) {
    bool ok = true;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            if (!glm::epsilonEqual(actual[col][row], expected[col][row], epsilon)) {
                ok = false;
            }
        }
    }
    expectTrue(ok, what);
}

void expectVec3Near(const glm::vec3& actual, const glm::vec3& expected, const std::string& what,
                     float epsilon = 1e-4f) {
    expectTrue(glm::all(glm::epsilonEqual(actual, expected, epsilon)), what);
}

}  // namespace

int main() {
    // --- Three-level hierarchy: root -> middle -> leaf -------------------
    // Each level's own Transform is deliberately given a NON-identity
    // position, rotation, AND scale (not just a translation), so a bug that
    // dropped rotation or scale from the composition -- or multiplied the
    // chain in the wrong order (local * parent instead of parent * local) --
    // would show up as a mismatch rather than accidentally cancelling out.
    {
        engine::EntityRegistry registry;

        // Deliberately NOT holding onto the Transform& addComponent<T>()
        // returns across the sibling addComponent<Transform>() calls below:
        // ComponentPool<T>::add() (ecs.hpp) can reallocate its backing
        // std::vector<T> on any insertion into the SAME pool, which would
        // invalidate an earlier call's returned reference -- the exact
        // hazard every real call site in this engine avoids simply by using
        // the reference immediately and never keeping it around past the
        // next addComponent<Transform>() call. Each entity's own local
        // matrix is captured into a plain glm::mat4 right after its fields
        // are set instead, which stays valid regardless of what the pool's
        // vector does afterward.
        const engine::EntityId root = registry.create();
        {
            engine::Transform& rootTransform = registry.addComponent<engine::Transform>(root);
            rootTransform.setPosition(glm::vec3(2.0f, 0.0f, 0.0f));
        }
        // Root has no Parent component at all -- it's a root by omission,
        // the same "components are opt-in per entity" convention every
        // other component here follows.

        const engine::EntityId middle = registry.create();
        {
            engine::Transform& middleTransform = registry.addComponent<engine::Transform>(middle);
            middleTransform.setPosition(glm::vec3(0.0f, 1.0f, 0.0f));
            middleTransform.setRotation(glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
        }
        registry.addComponent<engine::Parent>(middle, engine::Parent{root});

        const engine::EntityId leaf = registry.create();
        {
            engine::Transform& leafTransform = registry.addComponent<engine::Transform>(leaf);
            leafTransform.setPosition(glm::vec3(1.0f, 0.0f, 0.0f));
            leafTransform.setScale(glm::vec3(2.0f, 2.0f, 2.0f));
        }
        registry.addComponent<engine::Parent>(leaf, engine::Parent{middle});

        // Fresh pointer look-ups, taken only now that every entity/
        // component this test creates already exists -- no more
        // addComponent<Transform>() calls happen after this point, so
        // these (and the references resolveWorldMatrix() itself takes
        // internally) can't be invalidated out from under the comparisons
        // below.
        const glm::mat4 rootLocal = registry.getComponent<engine::Transform>(root)->getModelMatrix();
        const glm::mat4 middleLocal = registry.getComponent<engine::Transform>(middle)->getModelMatrix();
        const glm::mat4 leafLocal = registry.getComponent<engine::Transform>(leaf)->getModelMatrix();

        // Hand-computed expected result: plain glm matrix multiplication,
        // entirely independent of resolveWorldMatrix()'s own
        // iterative-walk implementation -- worldMatrix = root's own local
        // matrix * middle's own local matrix * leaf's own local matrix,
        // the parent-then-local composition order this file's own header
        // comment documents.
        const glm::mat4 expectedLeafWorld = rootLocal * middleLocal * leafLocal;

        expectMat4Near(engine::resolveWorldMatrix(registry, root), rootLocal,
                        "root's own world matrix equals its local matrix (no parent)");
        expectMat4Near(engine::resolveWorldMatrix(registry, middle), rootLocal * middleLocal,
                        "middle's world matrix is root's local * middle's local");
        expectMat4Near(engine::resolveWorldMatrix(registry, leaf), expectedLeafWorld,
                        "leaf's world matrix is root's local * middle's local * leaf's local");

        // Also check a concrete transformed point, not just the raw matrix
        // -- a more intuitive sanity check that the composition actually
        // moves geometry where it should: leaf's own local origin (0,0,0),
        // pushed through its resolved world matrix, should land at the same
        // world position as pushing it through the hand-composed matrix.
        const glm::vec3 expectedWorldOrigin = glm::vec3(expectedLeafWorld * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        const glm::vec3 actualWorldOrigin =
            glm::vec3(engine::resolveWorldMatrix(registry, leaf) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        expectVec3Near(actualWorldOrigin, expectedWorldOrigin, "leaf's world-space origin matches hand computation");
    }

    // --- An entity with no Transform at all resolves to identity ---------
    {
        engine::EntityRegistry registry;
        const engine::EntityId noTransform = registry.create();
        expectMat4Near(engine::resolveWorldMatrix(registry, noTransform), glm::mat4(1.0f),
                       "an entity with no Transform resolves to identity, matching every render() call site's own "
                       "pre-existing fallback");
    }

    // --- Dangling parent: Parent::id names an entity with no Transform ---
    {
        engine::EntityRegistry registry;
        const engine::EntityId ghost = registry.create();  // never given a Transform -- simulates a destroyed entity

        const engine::EntityId child = registry.create();
        engine::Transform& childTransform = registry.addComponent<engine::Transform>(child);
        childTransform.setPosition(glm::vec3(5.0f, 0.0f, 0.0f));
        registry.addComponent<engine::Parent>(child, engine::Parent{ghost});

        // A dangling parent must be treated as "child is an effective
        // root" -- i.e. resolves to exactly the child's own local matrix --
        // rather than crashing on a null Transform* or silently producing
        // some other wrong matrix.
        expectMat4Near(engine::resolveWorldMatrix(registry, child), childTransform.getModelMatrix(),
                       "a dangling Parent (no Transform on the referenced id) is treated as a root");
    }

    // --- Cycle safety: A parents B, B parents A ---------------------------
    // The real assertion here is implicit: if resolveWorldMatrix() actually
    // looped forever or blew the call stack on this input, this test binary
    // would hang or crash rather than reach the expectTrue() calls below --
    // ctest would then report a timeout/crash failure, which is exactly the
    // signal this scenario is here to catch. Reaching this point at all is
    // already proof the guard worked; the finiteness checks below are a
    // secondary sanity check that whatever this call returns is well-formed
    // rather than only "didn't hang."
    {
        engine::EntityRegistry registry;

        const engine::EntityId a = registry.create();
        engine::Transform& aTransform = registry.addComponent<engine::Transform>(a);
        aTransform.setPosition(glm::vec3(1.0f, 0.0f, 0.0f));

        const engine::EntityId b = registry.create();
        engine::Transform& bTransform = registry.addComponent<engine::Transform>(b);
        bTransform.setPosition(glm::vec3(0.0f, 1.0f, 0.0f));

        registry.addComponent<engine::Parent>(a, engine::Parent{b});
        registry.addComponent<engine::Parent>(b, engine::Parent{a});

        const glm::mat4 worldA = engine::resolveWorldMatrix(registry, a);
        const glm::mat4 worldB = engine::resolveWorldMatrix(registry, b);

        bool allFinite = true;
        for (int col = 0; col < 4 && allFinite; ++col) {
            for (int row = 0; row < 4 && allFinite; ++row) {
                if (!std::isfinite(worldA[col][row]) || !std::isfinite(worldB[col][row])) {
                    allFinite = false;
                }
            }
        }
        expectTrue(allFinite, "resolveWorldMatrix() on a two-entity parent cycle returns without hanging/crashing, "
                              "and produces a finite (not NaN/inf) matrix for both entities");
    }

    // --- Deep-chain guard: a chain longer than kMaxParentChainDepth -------
    // Not a cycle (every id is genuinely distinct and only ever visited
    // once), but deep enough that resolveWorldMatrix() must give up via its
    // depth bound rather than its visited-set check -- exercising the
    // "second, independent guard" this file's own header comment describes.
    {
        engine::EntityRegistry registry;

        const int chainLength = engine::kMaxParentChainDepth + 20;
        engine::EntityId previous;  // invalid EntityId -- the first entity created below is a root
        for (int i = 0; i < chainLength; ++i) {
            const engine::EntityId id = registry.create();
            engine::Transform& transform = registry.addComponent<engine::Transform>(id);
            transform.setPosition(glm::vec3(0.0f, 0.1f, 0.0f));
            if (previous.valid()) {
                registry.addComponent<engine::Parent>(id, engine::Parent{previous});
            }
            previous = id;
        }

        // `previous` now names the very last (deepest) entity created.
        // Resolving it must return promptly (the real assertion, same as
        // the cycle case above) and produce a finite matrix.
        const glm::mat4 worldDeepest = engine::resolveWorldMatrix(registry, previous);
        bool allFinite = true;
        for (int col = 0; col < 4 && allFinite; ++col) {
            for (int row = 0; row < 4 && allFinite; ++row) {
                if (!std::isfinite(worldDeepest[col][row])) {
                    allFinite = false;
                }
            }
        }
        expectTrue(allFinite, "resolveWorldMatrix() on a parent chain deeper than kMaxParentChainDepth returns "
                              "without hanging/crashing, and produces a finite matrix");
    }

    if (failures == 0) {
        std::printf("transform_hierarchy_test: all checks passed\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "transform_hierarchy_test: %d check(s) failed\n", failures);
    return EXIT_FAILURE;
}
