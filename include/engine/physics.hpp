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
// into the ground or bouncing off it. Phase 18c adds two small, deliberately
// bounded refinements on top of that same shape -- a hard clamp on fall
// speed (kTerminalFallSpeed) and ground friction decelerating horizontal
// velocity toward zero while resting (kGroundFriction) -- see each
// constant's own comment above and stepPhysics()'s own comment below for
// exactly where they apply; neither changes the ground-snap/zero-vertical-
// velocity behavior itself.
// IS NOT: a general constraint solver, continuous collision detection,
// entity-vs-entity collision, bounce/restitution, or air resistance modeled
// as a continuous drag force -- this phase's own brief explicitly scopes
// those out; "ground collision + gravity" (plus, since Phase 18c, a fall-
// speed clamp and ground friction, both still just per-entity, still no
// entity-vs-entity interaction of any kind) is the one load-bearing
// requirement. See stepPhysics()'s own comment below for why a single
// per-step position check is still enough to avoid tunneling through the
// ground specifically, without needing a general CCD sweep.

#include <glm/glm.hpp>

namespace engine {

class EntityRegistry;
// Phase 14e: forward-declared for setEntityStatic()'s signature below, the
// same "just the declaration, ecs.hpp itself stays a physics.cpp-only
// #include" shape EntityRegistry above already has -- this header still
// depends on nothing but <glm/glm.hpp> for any type it actually defines
// (RigidBody/Collider), matching physics.cpp's own "no GL/Window dependency
// at all" header comment.
class EntityId;

// Gravity's constant downward (-Y) acceleration, world units/second^2 --
// exposed here (not a physics.cpp-local constant) so a caller computing an
// expected result by hand (see tests/physics_test.cpp) integrates with the
// exact same value stepPhysics() itself uses, rather than a second
// hardcoded copy that could silently drift out of sync with this one.
constexpr float kGravityAcceleration = 9.81f;

// Phase 18c: the fastest an entity is ever allowed to fall, world
// units/second, applied as a hard clamp on RigidBody::velocity.y before it's
// integrated into position (see stepPhysics()'s own comment below for
// exactly where). A real falling object doesn't accelerate forever -- air
// resistance grows with speed until it exactly balances gravity, at which
// point the object stops accelerating and falls at a constant "terminal
// velocity" (a skydiver in a belly-to-earth position reaches roughly
// 50-55 m/s / ~120 mph). This engine has no drag force of its own (see this
// header's own top comment on what stepPhysics() deliberately IS/IS NOT --
// modeling air resistance as a continuous, speed-dependent force is real,
// separate scope this phase does not add), so a hard clamp is a deliberately
// simplified stand-in for that same real-world effect: it bounds how fast
// ANY fall gets, from any starting height or initial velocity (including one
// authored directly via a scene's "rigidBody" "velocity" block -- see
// scene_serialization.hpp), without needing a second force to compute or a
// per-entity drag coefficient to store. 40.0f sits comfortably below a real
// skydiver's terminal velocity (this is a simplified game-physics stand-in,
// not a real drag simulation, so it doesn't need to match that number
// exactly) while still being far above anything this engine's own existing
// demo content actually reaches before landing (assets/scenes/default.json's
// "falling_cube" falls 1.5-2.5 world units and lands under 6 m/s -- see
// physics_test.cpp's own hand-computed free-fall expectations) -- so this
// clamp is provably inert for every scene this engine ships today, and only
// engages for a deliberately extreme case (see tests/physics_test.cpp's own
// Phase 18c terminal-velocity cases), exactly the "sane maximum, not a
// gameplay-visible speed limit" this constant is meant to be.
constexpr float kTerminalFallSpeed = 40.0f;

// Phase 18c: ground friction's deceleration rate, world units/second^2 --
// how quickly an entity's HORIZONTAL (X/Z) velocity is reduced toward zero
// while it is resting on the ground (see stepPhysics()'s own comment below
// for exactly where this applies). Exposed here as a named constant for the
// exact same reason kGravityAcceleration above is: so a caller computing an
// expected result by hand (tests/physics_test.cpp) integrates with the
// identical value stepPhysics() itself uses. Real kinetic friction is
// (coefficient of friction) * (gravitational acceleration) -- independent of
// the sliding object's own mass, the same "every body decelerates at the
// same rate regardless of how heavy it is" property kGravityAcceleration
// already has above, and for the identical underlying reason this header's
// own "Deliberately no mass field" RigidBody comment gives: a mass field
// would be stored but never read by anything, since neither this constant
// nor kGravityAcceleration ever needs one to compute a per-entity
// deceleration. 4.9f is (kGravityAcceleration * 0.5f), i.e. a friction
// coefficient of ~0.5 -- a middle-of-the-road value for a dry, moderately
// grippy surface (rubber-on-concrete sits closer to 0.7-1.0, polished
// wood-on-wood or metal-on-metal closer to 0.2-0.4), chosen so a moderate
// sliding speed (a brisk 3 world-units/second) settles to a full stop in
// roughly half a second -- fast enough to read as "friction is clearly
// acting," not an near-imperceptibly slow crawl to zero, but not the
// instant, unnatural dead-stop a naive velocity.x = velocity.z = 0.0f would
// produce either.
constexpr float kGroundFriction = 4.9f;

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
// gravity to its velocity (only if RigidBody::useGravity), clamps
// velocity.y to never fall faster than -kTerminalFallSpeed (Phase 18c --
// applied unconditionally, whether or not this step's own velocity change
// came from gravity or from a scene-authored initial "velocity", so this is
// a cap on how fast the entity IS falling, not specifically on how fast
// gravity alone can accelerate it), integrates that velocity into its
// Transform's position (semi-implicit Euler -- see this header's own top
// comment), then -- only for entities that ALSO have a Collider -- checks
// the resulting position against `groundY` (the world-space height of a
// flat, effectively infinite ground plane; Application passes its own
// kGroundY here) and resolves a penetration by snapping the entity to rest
// exactly on the surface (position.y = groundY + collider->halfExtent) and
// zeroing velocity.y -- rather than a bounce/restitution response, which
// this "basic" ground collision doesn't model. That same "resting on the
// ground this step" branch is also where Phase 18c's ground friction
// applies: the entity's horizontal (X/Z) velocity is decelerated toward
// zero at kGroundFriction world-units/second^2, clamped so a single step
// can never overshoot past zero and reverse the direction of motion --
// friction opposes the resultant X/Z velocity vector as a whole (not each
// axis independently), so a diagonal slide decelerates at the same rate as
// an axis-aligned one of the same speed, matching how real kinetic friction
// opposes the actual direction of sliding motion rather than its
// component-wise projections. An entity with a RigidBody but no Collider
// (see below) never resolves ground collision at all, so it never receives
// friction either -- there is no "ground" for it to be considered resting
// on.
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

// Phase 14e: the "Static (Immovable)" mechanism the Inspector panel's toggle
// uses (see editor_ui.cpp's Phase 14e comment) -- deliberately living here,
// in the physics module itself, rather than as editor-only/ImGui-facing
// code. That placement is a direct consequence of this file's own top
// comment: stepPhysics() only ever iterates registry.each<RigidBody>(), so
// "static" in this engine's actual architecture already IS "has no
// RigidBody" -- there is no separate isStatic flag anywhere to set. Toggling
// that IS exactly this module's own domain, no different in kind from
// stepPhysics() itself reading/writing these same two component types; a
// dedicated function here (rather than inlined ImGui-adjacent code that
// calls addComponent/removeComponent directly) is also what lets this exact
// mutation be exercised by tests/physics_test.cpp with no live GL context or
// Dear ImGui frame, the same "pure logic, its own small function" shape this
// project already applies to stepPhysics() itself.
//
// makeStatic == true: removes `id`'s RigidBody component if it has one (so
// stepPhysics() stops moving it), then ensures it has a Collider -- adding
// one with Collider{}'s own default halfExtent (0.25) only if it doesn't
// already have one; an existing Collider's halfExtent is left untouched.
// This is deliberately a REAL Collider-only physics state, not a flag on
// RigidBody -- see physics.hpp's own top comment for why "has a Collider but
// no RigidBody" already means "static" today, with no code change needed
// here to make that true.
//
// makeStatic == false: adds a default-constructed RigidBody back (zero
// velocity, useGravity = true) if `id` doesn't already have one -- i.e.
// makes it fall under gravity again starting from wherever its Transform
// currently sits, not wherever it was when it was last dynamic. Its
// Collider (if any) is left completely untouched either way: an entity can
// be dynamic with or without a Collider, exactly as stepPhysics() already
// tolerates (see physics_test.cpp's own "RigidBody with no Collider" case) --
// this function has no opinion on whether a newly-dynamic entity should also
// gain a Collider, since ground collision is a separate, independently
// opt-in concern from "does gravity move this entity at all".
//
// Idempotent either way: calling with the state it's already in is a
// harmless no-op (addComponent<T>() on an id that already has one just
// overwrites it with an identical default-constructed value; this function
// only calls addComponent<RigidBody>()/addComponent<Collider>() when the
// component is actually absent, so an already-static entity keeps its exact
// existing Collider::halfExtent rather than having it silently reset).
// Well-defined (not a crash) for an id unknown to `registry` -- ecs.hpp's
// own addComponent()/removeComponent() already tolerate that themselves, so
// this function needs no extra guard of its own.
void setEntityStatic(EntityRegistry& registry, EntityId id, bool makeStatic);

}  // namespace engine

#endif  // ENGINE_PHYSICS_HPP
