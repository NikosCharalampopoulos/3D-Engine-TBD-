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
// What this component deliberately is NOT: a way to make an ECS entity
// actually control this engine's rendered view. That view still comes
// entirely from Application's own camera_ (a free-fly engine::Camera object
// -- see camera.hpp), completely independent of the ECS, exactly as it has
// since Phase 3 -- this phase does not touch camera_'s own update path, and
// there is no "active camera entity" concept anywhere in this file or its
// callers (contrast light.hpp's own DirectionalLight, which DOES need an
// "active" resolution because there is exactly one shadow-casting light
// uniform pair for it to compete over -- a CameraComponent entity competes
// with nothing: nothing in this engine's rendering pipeline reads one at
// all yet). A full "possess this entity, free-fly control transfers to it"
// feature is real, substantial, separate scope of its own -- it would need
// to decide what happens to keyboard/mouse input when a possessed camera
// entity exists, whether/how camera_ itself gets redirected or replaced,
// and (eventually) a camera-switching UI/keybind and possibly multiple
// viewports, none of which exist anywhere in this engine today. Building
// any part of that now, before anything could exercise it, is exactly the
// kind of speculative, nothing-consumes-it complexity this codebase's own
// established style avoids (see e.g. physics.hpp's own RigidBody mass-field
// comment, and light.hpp's own "no scene serialization for a component
// nothing writes yet" Phase 15a/15b precedent). A future phase that
// actually wants ECS-driven view control is the right place to build that,
// on top of this phase's real (if inert) ECS foundation -- not this one.
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
// receives input (see above); carrying "control feel" fields nothing reads
// would be exactly the same kind of speculative field this comment already
// argues against for the "active camera" concept itself.
//
// Default field values (60-degree vertical FOV, 0.1/100.0 near/far) are
// copied VERBATIM from engine::Camera's own fovYDeg_/nearPlane_/farPlane_
// defaults (camera.hpp) -- not because anything currently reads them
// together with the real camera_, but so that a freshly Create'd Camera
// entity's Inspector fields start out describing a plausible, ordinary
// camera (matching this engine's actual one) rather than an arbitrary
// placeholder a user would have to already know how to correct.

namespace engine {

struct CameraComponent {
    float fovYDeg = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

}  // namespace engine

#endif  // ENGINE_CAMERA_COMPONENT_HPP
