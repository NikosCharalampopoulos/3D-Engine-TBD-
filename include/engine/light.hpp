#ifndef ENGINE_LIGHT_HPP
#define ENGINE_LIGHT_HPP

// Phase 15a: the first half of the "Light/Camera" gap Phase 14f's own Create
// menu deliberately left BeginDisabled()'d (see editor_ui.cpp's Phase 14f
// comment) -- a real Light ECS component, so a point light can be a genuine
// user-creatable entity instead of only ever one of application.cpp's fixed
// compile-time kPointLights table entries. Directional Light and Camera stay
// deferred (see editor_ui.cpp's own Phase 15a comment for why each is a
// separately-scoped follow-up, not folded into this one): a point light is
// the smallest of the three to add ECS support for, since basic.frag/
// pbr.frag already treat point lights as a uNumPointLights-counted array
// (see application.cpp's own kPointLights/uploadPointLight()), not a single
// fixed slot the way the directional "sun" light is.
//
// Deliberately its own header (like physics.hpp), not folded into ecs.hpp
// itself -- see ecs.hpp's own top comment: a new component type is meant to
// need no ecs.hpp change at all, just a new addComponent<T>(...) call
// somewhere with a new T.
//
// Phase 15b adds the second of those two: a DirectionalLight component (see
// its own comment below) plus resolveActiveDirectionalLight(), which decide
// what application.cpp's single uLightDirection/uLightColor uniform pair
// (and its one shadow-casting cascade frustum -- see application.cpp's
// renderShadowPass()/computeCascades()) actually use each frame. Unlike
// PointLight above, this is deliberately NOT "grow an array with live data" --
// basic.frag/pbr.frag read a single fixed uniform pair, not a
// uNumDirectionalLights-counted array, and there is exactly one shadow
// frustum in this engine, built from exactly one direction. So making a
// directional light ECS-driven needs a notion of which ONE entity (if any)
// is actually driving that single pair/frustum this frame -- see
// application.hpp's own activeDirectionalLight_ comment for the "most
// recently created" rule this phase settles on, and
// resolveActiveDirectionalLight() below for where that rule is applied.
// Camera stays deferred (editor_ui.cpp's own Phase 15b comment) -- a
// structurally similar "which entity is active" problem, but for this
// engine's actual rendered view rather than one uniform pair, which is
// enough of a different problem (see application.hpp's own Camera-related
// comments) to stay its own separately-scoped follow-up.

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace engine {

class EntityRegistry;
// Forward-declared for resolveActiveDirectionalLight()'s signature below --
// same "just the declaration, ecs.hpp itself stays a light.cpp-only
// #include" shape physics.hpp's own EntityId forward-declaration already
// establishes (see that header's own comment), so this file still depends
// on nothing but <glm/glm.hpp> for any type it actually defines
// (PointLight/PointLightSample/DirectionalLight).
class EntityId;

// A point light entity's own per-entity data: color and the same
// (constant, linear, quadratic) attenuation coefficients application.cpp's
// existing fixed kPointLights table already uses -- see that table's own
// comment for what these mean and the reference values they're usually
// drawn from. Deliberately no position field, the same "reused directly,
// no wrapper suffix" shape physics.hpp's Collider already establishes for
// the identical reason: every PointLight-bearing entity's own Transform IS
// its light's position, so duplicating it here would be redundant state
// that could drift out of sync with the Transform an ImGui drag or a
// parent-hierarchy move already updates. See collectPointLights() below for
// where the two are actually combined.
//
// Default field values (plain white, the same (1.0, 0.7, 1.8) ~7-unit-range
// attenuation profile kPointLights already uses) are what
// Application::spawnEntityFromCreateMenu() gives a freshly Create'd Point
// Light -- a sane, immediately-visible starting point an artist can then
// recolor/retune from the Inspector, not a placeholder value nothing ever
// reads.
struct PointLight {
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float constant = 1.0f;
    float linear = 0.7f;
    float quadratic = 1.8f;
};

// One point light's fully-resolved, ready-to-upload data -- a PointLight
// component's own fields plus the position pulled from that same entity's
// Transform. Field-for-field identical in shape to application.cpp's own
// (pre-Phase-15) local PointLightData struct, which this replaces there:
// both feed the exact same uploadPointLight()/ClusterLightInput consumers,
// so there is no reason for two structurally-identical types to exist, one
// public and one application.cpp-private.
struct PointLightSample {
    glm::vec3 position;
    glm::vec3 color;
    float constant;
    float linear;
    float quadratic;
};

// Appends one PointLightSample per entity that has BOTH a Transform and a
// PointLight component to `out` -- an entity with only one of the two
// (there is no UI path that creates that today) contributes nothing here,
// the same "opt-in, no implicit pairing enforced" tolerance ecs.hpp's own
// top comment already documents for every other component combination,
// e.g. RigidBody-without-Transform (physics.hpp).
//
// `out` is APPENDED to, not overwritten -- so a caller can seed it with
// this engine's own fixed/hand-authored lights first (see application.cpp's
// kPointLights) and have both compete for the same `maxTotal` budget:
// basic.frag/pbr.frag's uPointLights is a fixed-size MAX_POINT_LIGHTS
// uniform array (8, see application.cpp's own kMaxPointLights), so once
// `out.size()` would reach `maxTotal`, further PointLight entities are
// silently skipped rather than overflowing that array -- growing
// MAX_POINT_LIGHTS itself is a shader-side change outside this function's
// own scope.
//
// Returns true if at least one PointLight entity had to be skipped this
// call because `out.size()` had already reached `maxTotal`, false
// otherwise. This function does NOT log anything itself -- it stays pure
// logic with no state that outlives one call (see light.cpp's own header
// comment: no GL/Window dependency, exercised by tests/light_test.cpp
// against a fresh EntityRegistry per test case, no log-output assertions).
// Deciding whether an overflow is worth a LOG_WARN, and whether it's a
// NEW overflow or a continuation of one already warned about, needs state
// that persists across calls and is scoped to one particular scene/
// registry -- that's the caller's job, not this function's: see
// Application::render()'s own call site (application.cpp) and its
// pointLightOverflowActive_ member (application.hpp), which does that
// edge-detection (warn once when overflow is first entered, stay silent
// while it persists, warn again if it clears and re-triggers) against this
// return value, scoped correctly to Application's own single registry_,
// instead of this function tracking it globally/statically the way an
// earlier version of this fix did.
bool collectPointLights(EntityRegistry& registry, std::size_t maxTotal, std::vector<PointLightSample>& out);

// Phase 15b: a directional light entity's own per-entity data -- direction +
// color, mirroring application.cpp's kLightDirection/kLightColor's own
// meaning exactly (`direction` points *from* the light *toward* the scene,
// same "sun ray" convention as kLightDirection -- see basic.frag -- and need
// not be pre-normalized on write, since every consumer, computeCascades()
// and both shaders, already normalizes/handles it itself).
//
// Deliberately a plain vec3 `direction` field here, NOT derived from this
// entity's own Transform::rotation() the way PointLight's "reuse the
// Transform, don't duplicate state" precedent (this header's own top
// PointLight comment) might first suggest for a *light*. Two reasons that
// precedent doesn't transfer:
//   1. Transform::rotation() is a full quaternion, but editor_ui.cpp's
//      Inspector "Transform" section only ever edits a single Rot-Y degree
//      field (see renderInspectorPanel()'s own Phase 14e comment) -- there
//      is no UI path to author a downward pitch through Transform at all,
//      and kLightDirection's own default has most of its magnitude in
//      exactly the Y component a Rot-Y-only rotation can never express
//      (rotating a fixed "down" vector around world Y leaves it pointing
//      straight down, unchanged, for every possible Rot-Y value).
//   2. application.cpp's own SpotLightData already establishes the
//      precedent this follows instead: a light-direction field of its own,
//      entirely independent of any Transform, for the identical reason.
// A DirectionalLight-bearing entity still gets an ordinary Transform (see
// Application::spawnEntityFromCreateMenu()'s own Phase 15b comment) -- purely
// so it's a normal, selectable node in the Scene Hierarchy/Inspector like
// every other Create-menu entity (renderInspectorPanel()'s own "every
// selectable entity has a Transform" assumption) -- but that Transform's
// position/rotation are cosmetic bookkeeping only, never read by
// resolveActiveDirectionalLight() below or by anything render() does with
// its result.
//
// Default field values are deliberately NOT copied from kLightDirection/
// kLightColor, unlike PointLight{}'s own defaults above (which DO mirror
// kPointLights' shared attenuation profile). A point light is additive --
// a freshly Create'd one is a genuinely new light source, so matching an
// existing light's tuning is a reasonable "immediately visible, sane
// starting point". A directional light instead REPLACES kLightDirection/
// kLightColor's contribution outright once active (see
// resolveActiveDirectionalLight()'s own comment) -- defaults identical to
// the fixed sun they replace would make a freshly created-and-activated
// Directional Light entity visually indistinguishable from having done
// nothing at all, which defeats the point of an "immediately visible"
// default and (concretely) would leave README.md's own Phase 15b headless
// verification with no pixel difference to point at as proof the new code
// path is actually live. A steeper, narrower-shadowed "high noon" angle
// (still on kLightDirection's own +x, +z side, so its shadow stays on the
// camera-visible side of each caster -- see kLightDirection's own Phase 7a
// comment for why that sign matters) paired with a cool "moonlight" tint
// (instead of the fixed sun's warm one) gives a starting point that is
// obviously, immediately a different light, exactly like PointLight{}'s own
// plain-white default not matching any of kPointLights' own tinted entries.
struct DirectionalLight {
    glm::vec3 direction{0.3f, -0.9f, 0.15f};
    glm::vec3 color{0.55f, 0.7f, 1.0f};
};

// Phase 15b: resolves this frame's actual directional-light direction/color.
// Returns `active`'s own DirectionalLight component when `active` is
// valid() (see ecs.hpp's own EntityId comment -- a default-constructed
// EntityId{} is the established "nothing" sentinel this codebase already
// uses for exactly this shape, e.g. application.cpp's own
// findEntityByName(); deliberately NOT std::optional<EntityId>, which would
// force this header to make EntityId a complete type via an ecs.hpp
// #include it otherwise has no need for -- see this file's own EntityId
// forward-declaration comment above), that entity still actually has one,
// AND its direction hasn't degenerated to (at or near) the zero vector;
// `fallback` unchanged in every other case (see below for each).
//
// Returns a DirectionalLight BY VALUE rather than writing through
// direction/color output parameters -- unlike PointLight, which needs a
// PointLightSample assembled from two different sources (its own component
// plus its owning entity's Transform position), a DirectionalLight needs
// nothing this engine's rendering pipeline doesn't already carry on the
// component itself, so the resolved per-frame value has exactly the same
// shape as the component -- reusing DirectionalLight itself for both roles
// avoids a bespoke "DirectionalLightSample" that would just be a duplicate
// of this same struct.
//
// `fallback` is a parameter, not this header's own hardcoded default --
// application.cpp's own call site passes kLightDirection/kLightColor, since
// those two are application.cpp-local constants (this file must not depend
// on application.cpp) and passing them in keeps this function pure and
// testable (tests/light_test.cpp) with no dependency on those two exact
// numbers.
//
// `active` being invalid (the default EntityId{}), or naming an entity with
// no DirectionalLight component (including a stale id whose entity/
// component was since destroyed -- see ecs.hpp's own EntityId comment on why
// getComponent() on such an id is always a safe nullptr, never UB), both
// resolve to `fallback` -- this is what keeps a scene with zero Directional
// Light entities (every scene before this phase, and this engine's own default
// scene today) rendering EXACTLY as it did before this phase: no active
// entity means kLightDirection/kLightColor pass straight through unchanged.
//
// A near-zero-length direction ALSO resolves to `fallback`, deliberately --
// see DirectionalLight's own comment above for why the Inspector's
// DragFloat3 has no per-axis floor that could prevent this the way
// PointLight's own Constant field floor prevents ITS one degenerate value:
// any single axis of a direction can legitimately be exactly 0.0 (a purely
// axis-aligned direction), so only the VECTOR's overall length is the actual
// hazard, not any one component in isolation. glm::normalize() on an
// (at-or-near-)zero vector produces NaN/Inf, which would silently corrupt
// computeCascades()'s light-space matrices (application.cpp) and, from
// there, this frame's entire shadow pass -- so an entity whose direction has
// drifted this close to zero (e.g. a user dragging all three Inspector
// fields toward 0) is treated exactly like "no active light" instead of
// ever reaching glm::normalize() with an unusable input.
DirectionalLight resolveActiveDirectionalLight(EntityRegistry& registry, EntityId active,
                                                const DirectionalLight& fallback);

}  // namespace engine

#endif  // ENGINE_LIGHT_HPP
