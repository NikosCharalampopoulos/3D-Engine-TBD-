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
//
// Phase 17b: the four hasX bools below are new -- editor_ui.cpp's
// renderSceneTreeNode() needs to know, per row, which of Font Awesome's
// mesh/lightbulb/sun/camera glyphs (if any) to draw next to that row's
// label (see editor_icons.hpp's own sceneNodeIconGlyph(), which these four
// flags feed directly). Computed once here, in buildSceneTree(), rather
// than re-checked every frame from inside the ImGui-facing row-drawing code
// -- the same "this file already has registry access and already visits
// every entity once per build" reasoning `name` itself is resolved with
// (nameOrFallback(), scene_hierarchy.cpp), just applied to component
// presence instead of a display name. Plain bools, not (say) a single
// "kind" enum: an entity can in principle carry more than one of these
// flags at once (see editor_icons.hpp's own precedence comment for why
// that's not actually reachable through any Create-menu path today but
// isn't schema-forbidden either) -- an enum would have to either lose that
// information or grow its own combinatorial cases, where sceneNodeIconGlyph()
// deciding the precedence from four independent bools is simpler and keeps
// this struct itself from needing to know anything about icon precedence at
// all.
struct SceneTreeNode {
    EntityId id;
    std::string name;
    bool hasModel = false;
    bool hasPointLight = false;
    bool hasDirectionalLight = false;
    bool hasCamera = false;
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
// Cycle/dangling-parent safety: mirrors resolveWorldMatrix()'s own three
// guards (transform_hierarchy.hpp), just applied to tree-building instead of
// matrix composition. A Parent whose id doesn't resolve to another entity
// this same call also sees (dangling, or pointing at something with no
// Transform) is treated as "this entity is a root". A cycle of Parent links
// (of any length) is broken at whichever entity in the cycle this function
// happens to reach first: that one becomes an extra top-level root, and
// every other entity in the same cycle nests normally beneath it, one level
// at a time, until the link back to the first entity is reached and simply
// dropped instead of re-adding a node for it a second time. A chain deeper
// than kMaxParentChainDepth (transform_hierarchy.hpp -- the exact same bound
// resolveWorldMatrix() itself enforces, reused rather than duplicated) is
// similarly split: the entity at the depth boundary becomes another extra
// top-level root instead of this function's own recursion (a real C++ call
// per tree level, unlike resolveWorldMatrix()'s iterative walk) descending
// arbitrarily deep and risking a stack overflow on a pathologically long,
// non-cyclic chain (or a cycle longer than the bound). There is no single
// "correct" tree shape once a cycle or an over-long chain makes one
// impossible, so the only real guarantee here is the one that matters:
// every entity registry.each<Transform>() visits appears in the returned
// forest exactly once, never zero or two times, and building the tree
// always terminates -- both without recursing unboundedly deep.
std::vector<SceneTreeNode> buildSceneTree(EntityRegistry& registry);

}  // namespace engine

#endif  // ENGINE_SCENE_HIERARCHY_HPP
