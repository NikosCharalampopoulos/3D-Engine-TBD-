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

}  // namespace engine

#endif  // ENGINE_TRANSFORM_HIERARCHY_HPP
