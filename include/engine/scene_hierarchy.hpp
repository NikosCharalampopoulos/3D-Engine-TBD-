#ifndef ENGINE_SCENE_HIERARCHY_HPP
#define ENGINE_SCENE_HIERARCHY_HPP

// Phase 14d: turns registry_'s flat, opaque-EntityId entity data into the
// actual nested tree shape the Scene Hierarchy panel displays (see
// editor_ui.cpp's Phase 14d comment for the ImGui-facing half). Deliberately
// its own small header/.cpp pair, not folded directly into editor_ui.cpp, for
// the same reason scene_serialization.hpp's own header comment gives for its
// own "pure data vs. GL/ImGui-facing" split: buildSceneTree() below depends
// only on ecs.hpp/transform_hierarchy.hpp (both GL- and ImGui-free), so its
// own nesting/name-resolution/cycle-safety logic can be exercised by a plain
// unit test (tests/scene_hierarchy_test.cpp) without a live GL context or
// Dear ImGui frame -- exactly the same "link only the pure logic file" shape
// physics_test/transform_hierarchy_test already establish (see those files'
// own header comments and tests/CMakeLists.txt).
//
// --- Real parent/child nesting, not a flat "folder" label ------------------
// The user explicitly chose real Phase 14b `Parent`-component grouping over a
// flat, non-transforming organizational "folder" concept (see
// transform_hierarchy.hpp's own header comment) -- so this tree is built
// directly from each entity's actual `Parent` component, not a separate
// invented grouping field. An entity with children parented to it *is* the
// tree's own "folder": there is no other node kind. This mirrors
// resolveWorldMatrix()'s own reading of `Parent` exactly (same "no Parent /
// dangling Parent => root" fallback, same "guard against a cycle" instinct --
// see this header's own comments below), just building a tree structure to
// display instead of composing a matrix to draw with.
//
// --- Which entities appear at all ------------------------------------------
// Every entity that has a Transform component (registry.each<Transform>(),
// the same enumeration Application::renderDebugUI()'s own "Scene Entities"
// panel already uses as its "does this entity meaningfully exist" test --
// see application.cpp) is one row. Not ModelComponent specifically: this
// engine's `Parent` component only makes sense on something that has a
// Transform to resolve a world matrix for in the first place (see
// transform_hierarchy.hpp), and a future entity with a Transform but no
// Model (e.g. an empty "grouping" node, or a future light/camera entity) is
// exactly the kind of thing a real editor's hierarchy panel still needs to
// list -- so this deliberately doesn't require ModelComponent, even though
// every entity in today's scene happens to have one.
//
// --- Naming ------------------------------------------------------------
// Uses NameComponent when present, falling back to "entity_<index>" --
// matching scene_serialization.hpp's own saveScene() placeholder-name
// convention exactly (see that header's own comment), so a hierarchy row's
// label is never surprising relative to what a save would have written for
// the same nameless entity.

#include <string>
#include <vector>

#include "engine/ecs.hpp"

namespace engine {

// One row of the Scene Hierarchy tree. Owns its children by value (mirrors
// model.hpp's own ModelNode -- a tree built once per call, walked top-down,
// never needing parent pointers).
struct SceneTreeNode {
    EntityId id;
    std::string name;
    std::vector<SceneTreeNode> children;
};

// Builds the whole forest of root-level SceneTreeNodes (usually a forest of
// one or a handful of roots, not literally one tree) from `registry`'s
// current Transform/NameComponent/Parent components -- see this header's own
// comments above for exactly which entities appear and how they're named.
//
// Ordering: roots (and each node's own children) are emitted in
// registry.each<Transform>()'s own dense-pool iteration order (creation
// order, for this engine's own never-removes-entities lifetime -- see
// ecs.hpp) -- deterministic and stable across calls within the same process
// run, so a caller (or a test) can rely on it without needing to sort.
//
// Cycle/dangling-parent safety: mirrors resolveWorldMatrix()'s own two
// guards (transform_hierarchy.hpp), just applied to tree-building instead of
// matrix composition. A Parent whose id doesn't resolve to another entity
// this same call also sees (dangling, or pointing at something with no
// Transform) is treated as "this entity is a root". A cycle of Parent links
// (of any length) is broken at whichever entity in the cycle this function
// happens to reach first: that one becomes an extra top-level root, and
// every other entity in the same cycle nests normally beneath it, one level
// at a time, until the link back to the first entity is reached and simply
// dropped instead of re-adding a node for it a second time -- there is no
// single "correct" tree shape for a cycle, so the only real guarantee here
// is the one that matters: every entity registry.each<Transform>() visits
// appears in the returned forest exactly once, never zero or two times, and
// building the tree always terminates rather than recursing forever.
std::vector<SceneTreeNode> buildSceneTree(EntityRegistry& registry);

}  // namespace engine

#endif  // ENGINE_SCENE_HIERARCHY_HPP
