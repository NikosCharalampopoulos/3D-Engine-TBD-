#ifndef ENGINE_TRANSFORM_HIERARCHY_HPP
#define ENGINE_TRANSFORM_HIERARCHY_HPP

// Phase 14b: real parent/child transform hierarchy on top of Phase 8a's ECS
// (ecs.hpp) and Transform's own local TRS matrix (transform.hpp).
// transform.hpp's own header comment says Transform is "deliberately NOT a
// scene graph node -- no parent/child, no dirty-flag caching... [a later
// phase] is the right place to grow this into a tree" -- this is that
// phase. It grows the tree ALONGSIDE Transform, as a new opt-in component
// (Parent, below) plus one free function that walks it, rather than adding
// parent/child fields to Transform itself -- matching ecs.hpp's own "a new
// component type is registered without touching ecs.hpp itself" design, and
// following physics.hpp's own precedent exactly: RigidBody/Collider (Phase
// 8e) got their own header rather than being folded into ecs.hpp, and
// Parent gets the same treatment here rather than a third bespoke wrapper
// struct living in ecs.hpp alongside ModelComponent/NameComponent.
//
// --- Naming: Parent, not ParentComponent ---------------------------------
// ecs.hpp's own header comment explains ModelComponent/NameComponent's
// "Component" suffix: it exists specifically to avoid a bare `struct Model`
// component colliding with the already-named `Model` class (NameComponent
// keeps the suffix for symmetry, wrapping a plain label the same
// ECS-only-bespoke way). RigidBody/Collider (physics.hpp) deliberately drop
// it: each names a self-contained concept in its own right, the same
// treatment Transform itself gets. Parent follows RigidBody/Collider, not
// ModelComponent -- there is no pre-existing standalone `Parent` type for a
// bare name to collide with (unlike Model), and "this entity's parent" is a
// self-contained hierarchy concept in exactly the sense "this entity's
// rigid-body state" is, not a wrapper disambiguating against something else
// of the same unqualified name.
//
// --- What a Parent component means ---------------------------------------
// An entity with no Parent component is a root: its own Transform IS its
// world transform, unchanged from every phase before this one (every
// existing scene entity -- "scene", "falling_cube" -- stays exactly this
// way unless assets/scenes/default.json's own Phase 14b entity opts a new
// entity into having one). An entity WITH a Parent component has its world
// transform computed by resolveWorldMatrix() below: parentWorldMatrix *
// thisEntity'sOwnLocalMatrix, recursively up the chain to whichever
// ancestor is itself a root -- i.e. moving/rotating/scaling a parent moves
// every descendant with it, the same behavior Unity/Blender's own
// parent/child transforms have (the user's own explicit choice over a flat,
// non-transforming organizational "folder" grouping concept -- see this
// phase's own commit message).
//
// --- Physics stays local-space-only: NOT wired through here ---------------
// stepPhysics() (physics.hpp/physics.cpp) is deliberately NOT changed by
// this phase to consult Parent/resolveWorldMatrix() at all -- see
// physics.cpp's own Phase 14b comment for the full reasoning. In one
// sentence: a RigidBody entity's simulation (gravity integration, ground
// collision) continues to read/write only its own entity's local Transform,
// exactly as Phase 8e left it; a still-static (no RigidBody) entity can be
// parented under a moving RigidBody entity and will correctly ride along
// with it VISUALLY (resolveWorldMatrix() is called at render time, after
// stepPhysics() has already updated the parent's Transform for this frame),
// without stepPhysics() itself ever needing to know parenting exists.
// Making stepPhysics() itself parent-aware (e.g. a child inheriting a
// parent's velocity, or resolving collision in world rather than local
// space) is real, separate scope this phase's own brief explicitly declines
// to take on.
//
// --- Cycle and dangling-parent safety -------------------------------------
// A malformed scene (or a bug) could set up A.parent = B, B.parent = A, or
// a Parent whose id refers to an entity that no longer exists (not
// reachable yet -- there is no entity-destruction UI before Phase 14f -- but
// designed for now anyway). resolveWorldMatrix() walks the chain with both
// a visited-id check (a robust guard against a cycle of ANY length) and a
// hard max-depth bound (kMaxParentChainDepth, below -- a cheap second guard
// against a pathologically long, non-cyclic chain), and treats a dangling
// Parent::id (registry.getComponent<Transform> on it returns nullptr) as
// "this entity is an effective root from here" rather than crashing. Either
// case logs a warning (once per offending entity, not once per frame -- see
// transform_hierarchy.cpp's own comment on why) rather than silently
// producing a wrong-but-plausible matrix.

#include <glm/glm.hpp>

#include "engine/ecs.hpp"

namespace engine {

// A single-field wrapper around the EntityId this entity is parented to.
// Opt-in per entity like every other component here (ecs.hpp's own
// "components are opt-in per entity" design) -- an entity with no Parent
// component is a root, per this file's own header comment above.
struct Parent {
    EntityId id;
};

// The longest parent chain resolveWorldMatrix() will walk before giving up
// and logging a warning, on top of (not instead of) its own visited-id
// cycle check -- see this file's header comment. 64 is generously far
// beyond any hierarchy depth this engine's own hand-authored scenes need
// (today's deepest chain is one level: a single demo child under
// "falling_cube" -- see assets/scenes/default.json), while still cheap to
// walk in full for a genuine cycle that a plain visited-id check alone
// would also have caught -- the depth bound exists as a second, independent
// safety net (e.g. against a future bug that grows `visited` unboundedly
// without actually revisiting an id), not because 64 real levels of nesting
// is an expected scene shape.
constexpr int kMaxParentChainDepth = 64;

// Resolves entity `id`'s WORLD model matrix: its own local
// Transform::getModelMatrix() if it has no Parent (or an invalid/dangling
// one), otherwise parentWorldMatrix * ownLocalMatrix, recursively up the
// chain -- see this file's own header comment for the full design
// (including why the multiplication order is parent-then-local, not the
// reverse, and how cycles/dangling parents are handled).
//
// Returns glm::mat4(1.0f) (identity) if `id` itself has no Transform
// component at all -- matching every render() call site's own pre-existing
// `transform != nullptr ? transform->getModelMatrix() : glm::mat4(1.0f)`
// fallback (see application.cpp), which this function replaces at each of
// those call sites without changing that fallback's own behavior.
glm::mat4 resolveWorldMatrix(EntityRegistry& registry, EntityId id);

// Phase 14f: the editor's real "Delete Object" action -- destroys `id` (via
// ecs.hpp's own EntityRegistry::destroyEntity(), the generic, Parent-unaware
// primitive that removes an entity from every component pool that currently
// exists, see that function's own comment) while ALSO deciding what happens
// to any entities parented directly under it (a Parent component whose
// `id` field equals this `id`) -- the one thing destroyEntity() itself can't
// do, since ecs.hpp deliberately has no idea Parent (this file's own
// component) exists at all.
//
// --- Orphan-to-root, not cascading delete -----------------------------
// This phase's own design choice: a deleted entity's direct children are
// ORPHANED (promoted to top-level roots), never destroyed along with it.
// Cascading delete (removing every descendant too) is equally defensible in
// the abstract, but orphaning is the safer default for an interactive
// editor specifically: a user who deletes one entity and only then notices
// it had children loses exactly that one entity, not an entire subtree they
// may not have realized was nested underneath it -- there is no "undo" in
// this editor yet (a real, separate scope this phase doesn't take on), so
// the recoverable mistake (re-parent the orphaned children back, or delete
// them too, deliberately, one at a time) is preferable to the
// unrecoverable one (an unintended cascading wipeout). This mirrors
// resolveWorldMatrix()'s and buildSceneTree()'s own existing "a dangling
// Parent::id is an effective root, not an error" treatment (see this
// file's and scene_hierarchy.hpp's own header comments) -- orphaning here
// is that exact same end state, just reached deliberately (by removing the
// child's own Parent component outright) instead of incidentally (by
// leaving a Parent that now dangles).
//
// --- Orphaned children keep their current WORLD position, not their old
// LOCAL one -----------------------------------------------------------
// A naive "just remove the Parent component" would silently reinterpret
// each child's existing LOCAL transform (relative to the now-destroyed
// parent) as its new WORLD transform -- visibly teleporting it the instant
// the parent's own accumulated transform stops being composed in. Instead,
// this function resolves each direct child's current WORLD transform
// (resolveWorldMatrix(), while `id` -- the soon-to-be-destroyed parent --
// is still present to resolve against) BEFORE `id` is destroyed, decomposes
// that matrix back into position/rotation/scale (glm::decompose(), see
// transform_hierarchy.cpp's own comment on the one case -- a fully
// degenerate world matrix -- where that decomposition can fail, and how
// this function stays correct even then), and overwrites the child's own
// Transform with that decomposed result -- so the child visibly stays
// exactly where it was an instant ago, now simply no longer riding along
// with a parent that no longer exists, exactly the "keeping their own
// current world position" behavior this phase's own brief calls for.
//
// Post-14f bug-review fix: every direct child is orphaned (its Parent
// component removed) UNCONDITIONALLY, regardless of whether that
// decompose() call above actually succeeds -- only the Transform-overwrite
// itself is conditional on it. See transform_hierarchy.cpp's own comment on
// destroyEntityOrphaningChildren() for why: an earlier version of this
// function skipped orphaning entirely for a child whose world matrix failed
// to decompose, which left that child's Parent still pointing at `id` right
// up until `id` was destroyed -- reintroducing, on exactly the path meant to
// prevent it, the "child's stale LOCAL transform gets silently reinterpreted
// as a WORLD one" teleport bug this whole function exists to avoid.
//
// Only DIRECT children (Parent.id == id) are touched -- a grandchild
// parented under one of those direct children keeps its own existing Parent
// link untouched (still pointing at that now-orphaned, but very much still
// alive, direct child), so only the one level actually affected by `id`'s
// removal changes at all; the rest of the subtree keeps moving together
// exactly as before, just rooted one level higher up than it used to be.
void destroyEntityOrphaningChildren(EntityRegistry& registry, EntityId id);

}  // namespace engine

#endif  // ENGINE_TRANSFORM_HIERARCHY_HPP
