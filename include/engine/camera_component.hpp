#ifndef ENGINE_CAMERA_COMPONENT_HPP
#define ENGINE_CAMERA_COMPONENT_HPP

// Phase 15c: the third and last of the "Light/Camera" Create-menu gaps
// Phase 14f's own Create menu left BeginDisabled()'d (see editor_ui.cpp's
// Phase 14f comment) -- a real Camera ECS component, so "Camera" can be a
// genuine, inspectable, user-creatable entity like Phase 15a's PointLight
// and Phase 15b's DirectionalLight before it.
//
// Deliberately its own header, NOT folded into light.hpp -- even though
// light.hpp already holds this arc's two prior Phase 15 components and its
// own top comment explicitly narrates the Point-Light-then-Directional-
// Light-then-Camera arc. A camera is not a light by any stretch (nothing
// about it is consumed by basic.frag/pbr.frag's lighting math), so growing
// light.hpp to also mean "and also Camera" would make that file's own name
// stop matching its contents for no real benefit -- light.hpp's own
// Deliberately-its-own-header comment already sets the precedent this
// follows: a new component type gets a new header when it is a genuinely
// different KIND of thing, exactly like light.hpp itself split off from
// physics.hpp/ecs.hpp rather than growing either of those.
//
// Fields mirror engine::Camera's (camera.hpp) own tunable, purely-OPTICAL
// properties: field of view and near/far clip planes, the three values that
// together define what a camera's own getProjectionMatrix() actually
// produces. Deliberately does NOT mirror Camera's position/orientation
// (yawDeg_/pitchDeg_) -- exactly like PointLight's own "no position field"
// precedent (this file's light.hpp counterpart), a CameraComponent-bearing
// entity's Transform (added alongside it by
// Application::spawnEntityFromCreateMenu(), same as every other Create-menu
// entity) already carries a position/rotation, so duplicating either here
// would be redundant state that could drift out of sync with an Inspector-
// edited Transform. Also deliberately does NOT mirror Camera's
// movementSpeed_/mouseSensitivity_ -- those describe how free-fly INPUT
// drives a camera, not what a camera optically IS, and this entity never
// receives input directly (Application::camera_ still owns all real WASD/
// mouse handling, see below); carrying "control feel" fields nothing reads
// would be exactly the kind of speculative field this file's own Phase 15c
// original comment already argued against.
//
// Default field values (60-degree vertical FOV, 0.1/100.0 near/far) are
// copied VERBATIM from engine::Camera's own fovYDeg_/nearPlane_/farPlane_
// defaults (camera.hpp) -- not because anything currently reads them
// together with the real camera_, but so that a freshly Create'd Camera
// entity's Inspector fields start out describing a plausible, ordinary
// camera (matching this engine's actual one) rather than an arbitrary
// placeholder a user would have to already know how to correct.
//
// --- Phase 18g: this component stopped being inert -----------------------
// Phase 15c's own original version of this comment described this struct as
// deliberately NOT a way to make an ECS entity control the rendered view --
// "there is no 'active camera entity' concept anywhere in this file or its
// callers... nothing in this engine's rendering pipeline reads one at all
// yet" -- and named that as real, separate, future scope. This phase is
// that future scope, closing the gap the same way Phase 15b closed the
// equivalent one for DirectionalLight: resolveActiveCamera() below decides
// which (at most one) Camera entity is "active," the same "which entity of
// possibly several is the one that matters" problem
// resolveActiveDirectionalLight() (light.hpp) already solves for its own
// component type, just with a DIFFERENT resolution rule (see
// resolveActiveCamera()'s own comment for why "first found by registry
// iteration order," not "most recently created," and why AT MOST ONE Camera
// entity can ever exist at all rather than merely picking a winner among
// several).
//
// What is STILL true, unchanged from Phase 15c: Application::camera_ (the
// free-fly engine::Camera, camera.hpp) remains this engine's Camera for
// EVERY frame Edit mode is active, completely unaffected by whether a
// Camera entity exists at all -- see application.hpp's own camera_ comment.
// Only Play mode's main render pass ever substitutes a Camera entity's own
// resolved pose/optics in camera_'s place (Application::render(), guarded on
// `physicsRunning_ && resolveActiveCamera(registry_).active.valid()`) -- see
// that method's own Phase 18g comment for the full mechanism, including why
// it builds a temporary engine::Camera value from the entity's data rather
// than threading a second view/projection pair through every render() sub-
// pass by hand. There is still no "possess this entity, WASD/mouse input
// transfers to it" feature -- a Camera entity's Transform can only be edited
// the ordinary Inspector/gizmo way, never flown -- that remains real,
// separate, undone scope, exactly as Phase 15c's own comment already argued
// (nothing about free-fly INPUT handling changes here, only which camera's
// optics the main pass reads FROM for that one frame).

#include <glm/glm.hpp>

// Unlike light.hpp's own forward-declare-only EntityId/EntityRegistry
// (light.hpp's functions only ever take an EntityId as a PARAMETER, never
// embed one in a struct they define), this header needs EntityId as a
// COMPLETE type: ActiveCameraResolution below embeds one as a real by-value
// member, and an incomplete type cannot be a data member. So, unlike
// light.hpp, this file pulls in the full engine/ecs.hpp -- a header-only,
// GL-free file (see its own header comment), so this adds no new .cpp link
// dependency to anything that already includes camera_component.hpp.
#include "engine/ecs.hpp"

namespace engine {

struct CameraComponent {
    float fovYDeg = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

// Phase 18g: the world-space eye position + look-at target a Camera
// entity's own RESOLVED WORLD transform (transform_hierarchy.hpp's
// resolveWorldMatrix(), which already folds in parenting) implies -- pure
// glm::mat4-in, no EntityRegistry/ECS dependency at all, so it's testable
// (tests/camera_component_test.cpp) against a hand-built matrix, no live
// scene required.
//
// `lookTarget` (not a bare direction vector) is deliberately what this
// returns -- `position + forward`, ready to hand straight to
// engine::Camera::setPositionLookingAt(position, lookTarget) (camera.hpp),
// which already knows how to turn a position+target pair into the
// yaw/pitch pair Camera's own getViewMatrix() needs. Reusing that existing,
// already-tested function (rather than hand-deriving a view matrix here) is
// what lets Application::render() build a temporary Camera value from a
// Camera entity's pose and then reuse EVERY other render()/renderShadowPass()/
// computeCascades() call site completely unmodified -- see application.cpp's
// own Phase 18g render() comment.
struct CameraWorldPose {
    glm::vec3 position{0.0f};
    glm::vec3 lookTarget{0.0f, 0.0f, -1.0f};
};

// `worldMatrix` is a Camera entity's own resolveWorldMatrix() result --
// position is its translation column; the look direction is the standard
// engine-forward basis vector (0, 0, -1) rotated through the matrix's own
// rotation, exactly the same "rotate the canonical forward vector by this
// object's orientation" convention engine::Camera's own front_ already
// represents, just derived from a world matrix instead of yaw/pitch angles.
//
// Normalizing `rotationScale * (0, 0, -1)` (rather than using that product
// directly) is what keeps this correct even when the entity (or an ancestor
// it's parented under) has a non-uniform Transform::scale(): a TRS matrix's
// upper-left 3x3 is `rotation * diag(scale)`, whose three COLUMNS stay
// mutually ORTHOGONAL regardless of how non-uniform `scale` is (each column
// is the rotation's own orthogonal column times one scalar, and scaling
// orthogonal vectors by independent scalars cannot make them non-
// perpendicular) but are no longer unit length -- normalizing after the
// multiply recovers the pure rotated direction, discarding exactly the
// scale contamination and nothing else. A camera entity is never expected
// to actually carry a non-1 scale (nothing about "how big is a camera"
// means anything optically -- CameraComponent's own header comment already
// makes this point for fovYDeg/near/far), but resolveWorldMatrix() folds in
// an ENTIRE parent chain's scale too, not just this entity's own, so a
// scaled ancestor is a real, reachable case this function stays correct
// under rather than merely assuming away.
CameraWorldPose resolveCameraWorldPose(const glm::mat4& worldMatrix);

// Phase 18g: which (at most one) ECS entity is the scene's "active" Camera
// this frame -- the entity whose resolved world pose + CameraComponent
// optics actually drive Play mode's main render pass, INSTEAD OF
// Application::camera_ -- see application.cpp's own Phase 18g render()
// comment for exactly where/how this gets consumed, and Edit mode's own
// total non-involvement (Application::camera_ is used unconditionally
// there, regardless of this function's result).
//
// --- Why this is a DIFFERENT resolution rule from resolveActiveDirectionalLight()
// DirectionalLight has no upper bound on how many entities may exist --
// resolveActiveDirectionalLight() picks a winner (activeDirectionalLight_,
// "most recently created") purely because exactly one uLightDirection/
// uLightColor uniform pair exists to feed, while every OTHER Directional
// Light entity is simply inert, competing for nothing. A Camera entity is
// different: this engine has exactly one rendered VIEW per frame, full
// stop, so "which camera renders this frame" isn't a competition among
// several legitimate choices the way "which light casts THE shadow" is --
// there is supposed to be at most ONE Camera entity in the scene at all
// (the Create menu's own Phase 18g change disables the "Camera" item the
// instant one exists, and re-enables it only once that one is deleted --
// see editor_ui.cpp's own Phase 18g renderCreateEntityMenuItems() comment).
// This function's own "first found by registry iteration order, count the
// rest as ignored" rule therefore exists purely DEFENSIVELY, for the one
// path the UI-level restriction above can't reach: a hand-edited or
// hand-authored scene JSON that already contains more than one CameraComponent
// record before this engine's own scene loader ever runs (scene_serialization.hpp's
// own loader applies no such uniqueness check -- see this project's own
// established "the UI enforces it; loaded data is defensively tolerated,
// never trusted" split, e.g. transform_hierarchy.hpp's own dangling-Parent/
// cycle handling for an equally malformed-input case). "First found by
// registry iteration order" (not "most recently created," which would need
// this Application to remember creation order across a LOAD, something
// nothing else in this engine does) is the simplest deterministic tie-break
// that needs no extra bookkeeping at all -- EntityRegistry::each() already
// visits a component pool in one fixed, repeatable order (ecs.hpp's own
// each() comment), so this is stable across every frame this same registry
// is queried, and does not change from render() call to render() call
// unless the SCENE itself changes (an entity being created/destroyed).
//
// `ignoredCount` is how many EXTRA Camera entities beyond `active` this call
// found -- 0 in every normal (zero-or-one Camera entity) scene. A caller
// that finds this nonzero should LOG_WARN once (edge-triggered, the
// identical pointLightOverflowActive_-style "warn on entering the bad state,
// stay silent while it persists" discipline application.cpp's own
// collectPointLights() overflow handling already establishes) rather than
// crash or silently pick a DIFFERENT entity every frame -- see
// application.cpp's own Phase 18g render() comment for exactly how.
struct ActiveCameraResolution {
    EntityId active;
    int ignoredCount = 0;
};

ActiveCameraResolution resolveActiveCamera(EntityRegistry& registry);

}  // namespace engine

#endif  // ENGINE_CAMERA_COMPONENT_HPP
