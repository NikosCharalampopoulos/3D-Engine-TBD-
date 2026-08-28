#ifndef ENGINE_LIGHT_HPP
#define ENGINE_LIGHT_HPP

// Phase 15: the first half of the "Light/Camera" gap Phase 14f's own Create
// menu deliberately left BeginDisabled()'d (see editor_ui.cpp's Phase 14f
// comment) -- a real Light ECS component, so a point light can be a genuine
// user-creatable entity instead of only ever one of application.cpp's fixed
// compile-time kPointLights table entries. Directional Light and Camera stay
// deferred (see editor_ui.cpp's own Phase 15 comment for why each is a
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

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace engine {

class EntityRegistry;

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
// silently skipped (a warning logged once per call, not once per skipped
// entity, so a user who creates many extra lights doesn't flood the log)
// rather than overflowing that array -- growing MAX_POINT_LIGHTS itself is
// a shader-side change outside this function's own scope.
void collectPointLights(EntityRegistry& registry, std::size_t maxTotal, std::vector<PointLightSample>& out);

}  // namespace engine

#endif  // ENGINE_LIGHT_HPP
