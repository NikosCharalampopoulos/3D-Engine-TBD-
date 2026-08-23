// Phase 14d's own test: exercises engine::buildSceneTree()
// (src/scene_hierarchy.cpp) against a hand-checkable multi-level Parent
// hierarchy, plus the naming-fallback, dangling-parent, and cycle-safety
// cases scene_hierarchy.hpp's own header comment documents. Same "plain
// executable, links only the pure logic file it's testing" shape as
// transform_hierarchy_test/physics_test (see those files' own header
// comments) -- scene_hierarchy.cpp depends only on ecs.hpp/transform.hpp/
// transform_hierarchy.hpp, none of which touch GL/ImGui/Window, so this
// needs no live window/GL context/GPU/Dear ImGui frame either.

#include "engine/scene_hierarchy.hpp"

#include "engine/ecs.hpp"
#include "engine/transform.hpp"
#include "engine/transform_hierarchy.hpp"

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

void expectEqual(const std::string& actual, const std::string& expected, const std::string& what) {
    expectTrue(actual == expected, what + " (expected \"" + expected + "\", got \"" + actual + "\")");
}

// Finds the first direct child of `node` named `name`, or nullptr -- a small
// test-only helper so the assertions below can read as "root's child named
// X" rather than hardcoding index-0/1 positions everywhere.
const engine::SceneTreeNode* findChild(const engine::SceneTreeNode& node, const std::string& name) {
    for (const engine::SceneTreeNode& child : node.children) {
        if (child.name == name) {
            return &child;
        }
    }
    return nullptr;
}

// Same lookup, one level up -- among the forest's own top-level roots
// (buildSceneTree()'s own return value) rather than one node's children.
const engine::SceneTreeNode* findRoot(const std::vector<engine::SceneTreeNode>& roots, const std::string& name) {
    for (const engine::SceneTreeNode& root : roots) {
        if (root.name == name) {
            return &root;
        }
    }
    return nullptr;
}

}  // namespace

int main() {
    // --- Three-level real nesting: root -> middle -> leaf -----------------
    // Mirrors transform_hierarchy_test's own root/middle/leaf shape, but
    // checks tree STRUCTURE (who's nested under whom) rather than composed
    // matrices -- this is the "an entity with children visually acts as a
    // folder" property the Scene Hierarchy panel relies on (see
    // scene_hierarchy.hpp's own header comment).
    {
        engine::EntityRegistry registry;

        const engine::EntityId root = registry.create();
        registry.addComponent<engine::Transform>(root);
        registry.addComponent<engine::NameComponent>(root, engine::NameComponent{"root"});

        const engine::EntityId middle = registry.create();
        registry.addComponent<engine::Transform>(middle);
        registry.addComponent<engine::NameComponent>(middle, engine::NameComponent{"middle"});
        registry.addComponent<engine::Parent>(middle, engine::Parent{root});

        const engine::EntityId leaf = registry.create();
        registry.addComponent<engine::Transform>(leaf);
        registry.addComponent<engine::NameComponent>(leaf, engine::NameComponent{"leaf"});
        registry.addComponent<engine::Parent>(leaf, engine::Parent{middle});

        // A second, entirely separate root -- proves the forest can hold
        // more than one root-level entity, not just one all-encompassing
        // tree (this engine's own default.json scene has exactly this
        // shape: "scene" and "falling_cube" are both roots).
        const engine::EntityId otherRoot = registry.create();
        registry.addComponent<engine::Transform>(otherRoot);
        registry.addComponent<engine::NameComponent>(otherRoot, engine::NameComponent{"other_root"});

        const std::vector<engine::SceneTreeNode> tree = engine::buildSceneTree(registry);

        expectTrue(tree.size() == 2, "two root-level entities (root, other_root) produce two forest entries");

        const engine::SceneTreeNode* rootNode = findRoot(tree, "root");
        expectTrue(rootNode != nullptr, "\"root\" appears at the top level of the forest");
        if (rootNode != nullptr) {
            expectTrue(rootNode->id == root, "\"root\" node's id matches the entity it was built from");
            expectTrue(rootNode->children.size() == 1, "\"root\" has exactly one child (\"middle\")");
            const engine::SceneTreeNode* middleNode = findChild(*rootNode, "middle");
            expectTrue(middleNode != nullptr, "\"middle\" is nested under \"root\", not top-level");
            if (middleNode != nullptr) {
                expectTrue(middleNode->id == middle, "\"middle\" node's id matches the entity it was built from");
                expectTrue(middleNode->children.size() == 1, "\"middle\" has exactly one child (\"leaf\")");
                const engine::SceneTreeNode* leafNode = findChild(*middleNode, "leaf");
                expectTrue(leafNode != nullptr, "\"leaf\" is nested two levels under \"root\"");
                if (leafNode != nullptr) {
                    expectTrue(leafNode->id == leaf, "\"leaf\" node's id matches the entity it was built from");
                    expectTrue(leafNode->children.empty(), "\"leaf\" has no children of its own");
                }
            }
        }

        const engine::SceneTreeNode* otherRootNode = findRoot(tree, "other_root");
        expectTrue(otherRootNode != nullptr, "\"other_root\" appears at the top level of the forest, unnested");
        if (otherRootNode != nullptr) {
            expectTrue(otherRootNode->children.empty(), "\"other_root\" (no children parented to it) has none");
        }
    }

    // --- Name fallback: no NameComponent -> "entity_<index>" --------------
    // Matches scene_serialization.hpp's own saveScene() placeholder-name
    // convention exactly -- see scene_hierarchy.hpp's own header comment.
    {
        engine::EntityRegistry registry;
        const engine::EntityId nameless = registry.create();
        registry.addComponent<engine::Transform>(nameless);
        // Deliberately no NameComponent added.

        const std::vector<engine::SceneTreeNode> tree = engine::buildSceneTree(registry);
        expectTrue(tree.size() == 1, "one entity, no Parent, produces one root");
        if (tree.size() == 1) {
            expectEqual(tree[0].name, "entity_" + std::to_string(nameless.index()),
                        "a nameless entity falls back to \"entity_<index>\", matching saveScene()'s own convention");
        }
    }

    // --- Dangling parent: Parent::id names an entity with no Transform ----
    // Mirrors transform_hierarchy_test's own dangling-parent case, applied
    // to tree-building instead of matrix composition -- resolveWorldMatrix()
    // treats this as "effective root"; buildSceneTree() must do the same so
    // the entity still appears (at the top level) rather than vanishing.
    {
        engine::EntityRegistry registry;
        const engine::EntityId ghost = registry.create();  // never given a Transform

        const engine::EntityId child = registry.create();
        registry.addComponent<engine::Transform>(child);
        registry.addComponent<engine::NameComponent>(child, engine::NameComponent{"child"});
        registry.addComponent<engine::Parent>(child, engine::Parent{ghost});

        const std::vector<engine::SceneTreeNode> tree = engine::buildSceneTree(registry);
        expectTrue(tree.size() == 1, "a dangling-parent entity is promoted to a top-level root, not dropped");
        if (tree.size() == 1) {
            expectEqual(tree[0].name, "child", "the dangling-parent entity itself still appears, by name");
        }
    }

    // --- Cycle safety: A parents B, B parents A ----------------------------
    // As with transform_hierarchy_test's own cycle case, the real assertion
    // is implicit: if buildSceneTree() recursed forever on this input, this
    // test binary would hang/crash rather than reach the checks below.
    // Reaching this point at all is already proof the cycle guard worked;
    // the count/name checks below additionally confirm BOTH entities still
    // appear exactly once in the returned forest rather than one silently
    // vanishing.
    {
        engine::EntityRegistry registry;
        const engine::EntityId a = registry.create();
        registry.addComponent<engine::Transform>(a);
        registry.addComponent<engine::NameComponent>(a, engine::NameComponent{"a"});

        const engine::EntityId b = registry.create();
        registry.addComponent<engine::Transform>(b);
        registry.addComponent<engine::NameComponent>(b, engine::NameComponent{"b"});

        registry.addComponent<engine::Parent>(a, engine::Parent{b});
        registry.addComponent<engine::Parent>(b, engine::Parent{a});

        const std::vector<engine::SceneTreeNode> tree = engine::buildSceneTree(registry);

        // Count every entity anywhere in the returned forest (roots plus
        // however deep any of them nest) -- a plain recursive count, not
        // buildSceneTree()'s own logic, so this is an independent check.
        std::size_t totalNodes = 0;
        struct Counter {
            std::size_t& total;
            void operator()(const engine::SceneTreeNode& node) {
                ++total;
                for (const engine::SceneTreeNode& child : node.children) {
                    (*this)(child);
                }
            }
        } counter{totalNodes};
        for (const engine::SceneTreeNode& root : tree) {
            counter(root);
        }

        expectTrue(totalNodes == 2, "a two-entity parent cycle still yields exactly 2 total nodes (none dropped)");
    }

    // --- Deep-chain guard: a chain longer than kMaxParentChainDepth -------
    // Bug-review addition (this phase's own review): mirrors
    // transform_hierarchy_test's own "Deep-chain guard" case exactly (same
    // chain length, same "not a cycle -- every id genuinely distinct and
    // visited only once, but deep enough to need the depth bound rather
    // than the visited-set check" shape) -- see buildSceneTree()'s own
    // header comment on why it reuses resolveWorldMatrix()'s
    // kMaxParentChainDepth rather than recursing arbitrarily deep. As with
    // the cycle case above, the real assertion is implicit: build() is
    // genuine C++ recursion (unlike resolveWorldMatrix()'s iterative walk),
    // so if this depth guard didn't exist, a chain this long would risk a
    // real stack overflow rather than just hanging -- reaching the checks
    // below at all is already proof the guard works.
    {
        engine::EntityRegistry registry;

        const int chainLength = engine::kMaxParentChainDepth + 20;
        engine::EntityId previous;  // invalid EntityId -- the first entity created below is a root
        for (int i = 0; i < chainLength; ++i) {
            const engine::EntityId id = registry.create();
            registry.addComponent<engine::Transform>(id);
            if (previous.valid()) {
                registry.addComponent<engine::Parent>(id, engine::Parent{previous});
            }
            previous = id;
        }

        const std::vector<engine::SceneTreeNode> tree = engine::buildSceneTree(registry);

        // Count every entity anywhere in the returned forest, the same
        // independent recursive counter the cycle case above uses.
        std::size_t totalNodes = 0;
        struct Counter {
            std::size_t& total;
            void operator()(const engine::SceneTreeNode& node) {
                ++total;
                for (const engine::SceneTreeNode& child : node.children) {
                    (*this)(child);
                }
            }
        } counter{totalNodes};
        for (const engine::SceneTreeNode& root : tree) {
            counter(root);
        }

        expectTrue(totalNodes == static_cast<std::size_t>(chainLength),
                   "a chain deeper than kMaxParentChainDepth still yields exactly chainLength total nodes across "
                   "the whole forest (none dropped, none duplicated)");
        expectTrue(tree.size() > 1,
                   "a chain deeper than kMaxParentChainDepth is split across more than one top-level forest entry "
                   "(the depth guard actually engaged, not just happened to not matter)");
    }

    if (failures == 0) {
        std::printf("scene_hierarchy_test: all checks passed\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "scene_hierarchy_test: %d check(s) failed\n", failures);
    return EXIT_FAILURE;
}
