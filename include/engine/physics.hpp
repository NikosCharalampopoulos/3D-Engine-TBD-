#ifndef ENGINE_PHYSICS_HPP
#define ENGINE_PHYSICS_HPP

// Phase 8e: basic physics/collision on top of Phase 8a's ECS -- gravity plus
// ground-plane collision for a small number of hand-authored demo entities,
// not a general rigid-body solver (see "What this deliberately IS / IS NOT"
// below). RigidBody and Collider are two new component types, registered
// via EntityRegistry::addComponent<T>() exactly the way ecs.hpp's own header
// comment says a new component type should be -- without touching ecs.hpp
// itself, the same way ModelComponent/NameComponent already work. Deliberately
// no "Component" suffix on either name (unlike ModelComponent/NameComponent)
// -- see ecs.hpp's own ModelComponent comment for why: both are
// self-contained simulation concepts in their own right, reused directly the
// same way Transform already is, not a bespoke wrapper around some other
// engine type's pointer/label.
//
// --- Why an AABB (cube) collider, not a bounding sphere ------------------
// mesh.hpp already has a BoundingSphere (Phase 13b, for frustum culling),
// which could in principle be reused here. This phase's own demo object
// (see application.cpp's Phase 8e comment and assets/models/falling_cube.obj)
// is a cube, though, and the one collision this system resolves is against a
// flat, Y-constant ground plane -- which only ever needs the collider's own
// Y half-extent, regardless of whether the shape is "really" a box or a
// sphere. Given that, a box collider that visually matches the box being
// dropped (so the object visibly rests ON the ground the instant it stops
// falling, rather than floating with its corners still poking through, or
// stopping early with visible daylight under its corners -- either of which
// a sphere-vs-cube shape mismatch would produce) is strictly better here
// than reusing BoundingSphere, for the same one float's worth of storage.
//
// --- What this deliberately IS / IS NOT ----------------------------------
// IS: per-entity gravity integration (semi-implicit/"symplectic" Euler --
// velocity is updated from acceleration BEFORE it's used to move position,
// not after -- the standard basic-but-stable choice, cheap to reason about
// and immune to the energy-gaining blowup naive explicit Euler can produce)
// plus a ground-plane collision check/resolve: once an entity's collider
// would cross below the ground, its position is snapped to rest exactly on
// the surface and its vertical velocity is zeroed, rather than left sunk
// into the ground or bouncing off it.
// IS NOT: a general constraint solver, continuous collision detection, or
// entity-vs-entity collision -- this phase's own brief explicitly scopes
// those out; "ground collision + gravity" is the one load-bearing
// requirement. See stepPhysics()'s own comment below for why a single
// per-step position check is still enough to avoid tunneling through the
// ground specifically, without needing a general CCD sweep.

#include <glm/glm.hpp>

namespace engine {

class EntityRegistry;

// Gravity's constant downward (-Y) acceleration, world units/second^2 --
// exposed here (not a physics.cpp-local constant) so a caller computing an
// expected result by hand (see tests/physics_test.cpp) integrates with the
// exact same value stepPhysics() itself uses, rather than a second
// hardcoded copy that could silently drift out of sync with this one.
constexpr float kGravityAcceleration = 9.81f;

// A rigid body's own per-entity simulation state: current velocity (world
// units/second) and whether gravity applies to it at all.
//
// Deliberately no mass field: gravity's acceleration is the same for every
// body regardless of mass (a feather and a bowling ball fall at the same
// rate), and this phase does no entity-vs-entity impulse response -- the
// one place a real solver would actually need mass, to weight how an
// impulse splits between two colliding bodies. A mass field here would be
// stored but never read by anything, exactly the kind of speculative,
// nothing-consumes-it field this codebase's own established style avoids
// (see ecs.hpp's NameComponent comment for the same reasoning applied to a
// different field).
struct RigidBody {
    glm::vec3 velocity{0.0f};
    bool useGravity = true;
};

// A simple axis-aligned box collider: the box spans
// [center - halfExtent, center + halfExtent] on every one of x/y/z, where
// `center` is always the owning entity's own Transform::position() -- this
// component carries no offset of its own, since every entity this phase
// gives one to is a single simple mesh whose own local origin already sits
// at its geometric center (see assets/models/falling_cube.obj's own header
// comment). See this header's own top comment for why a box, not a sphere.
struct Collider {
    float halfExtent = 0.25f;
};

// Runs one physics step for every entity that has a RigidBody: applies
// gravity to its velocity (only if RigidBody::useGravity), integrates that
// velocity into its Transform's position (semi-implicit Euler -- see this
// header's own top comment), then -- only for entities that ALSO have a
// Collider -- checks the resulting position against `groundY` (the
// world-space height of a flat, effectively infinite ground plane;
// Application passes its own kGroundY here) and resolves a penetration by
// snapping the entity to rest exactly on the surface
// (position.y = groundY + collider->halfExtent) and zeroing velocity.y --
// rather than a bounce/restitution response, which this "basic" ground
// collision doesn't model.
//
// Checking the collider's already-integrated (predicted) position against
// groundY, rather than sweeping the volume it moved through this step, is
// what a general continuous-collision-detection system would do instead --
// but since the only collision surface here is one flat, Y-constant plane
// (never another moving/rotating body), a large single-step displacement
// can't "skip over" it the way it could tunnel through a thin moving
// object: any predicted position at or below the rest height is
// unambiguously a penetration of THIS plane, however large deltaTime was.
// So this function needs no timestep-based tunneling guard of its own --
// see application.cpp's own comment on why it still clamps deltaTime before
// calling this (a frame-timing/demo-pacing concern, not a correctness one).
//
// An entity with a RigidBody but no Transform is silently skipped (there's
// nothing to move) -- defensive, since nothing in this engine creates such
// an entity today (loadScene()'s own "rigidBody" block always accompanies a
// "transform" block, see scene_serialization.hpp), but addComponent<T>()
// itself never enforces that pairing.
void stepPhysics(EntityRegistry& registry, float deltaTime, float groundY);

}  // namespace engine

#endif  // ENGINE_PHYSICS_HPP
