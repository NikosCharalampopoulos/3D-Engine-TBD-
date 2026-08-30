#ifndef ENGINE_GIZMO_HPP
#define ENGINE_GIZMO_HPP

// Phase 18e: the project owner's own explicit request -- "if I could move it
// from inside the scene (not when I move using the camera with WASD and
// mouse)" -- a standard Unity/Blender/Unreal translate gizmo: three colored
// axis handles rendered at the selected entity's position, click-dragged to
// slide it along one axis. Before this phase the ONLY way to move a selected
// entity was typing numbers into the Inspector's Transform DragFloat3 fields
// (editor_ui.cpp's renderInspectorPanel()) -- there was, and outside this
// file still is, no mouse-picking/raycasting of any kind anywhere in this
// engine.
//
// This header is the pure, GL/ImGui-free half of the feature -- the same
// "pure logic, its own small file, fully unit-testable with no live
// window/GL context" shape physics.hpp/camera_capture.hpp/window_chrome.hpp
// already establish (see e.g. camera_capture.hpp's own header comment on
// why decideCameraCapture() lives apart from the ImGui-facing code that
// calls it). Two GL/ImGui-facing halves consume this file's functions from
// elsewhere:
//   - editor_ui.cpp's new EditorUI::updateGizmo() -- runs INSIDE the
//     Viewport panel's own Begin()/End() block (the only place Dear ImGui's
//     own IsWindowHovered()/mouse queries can be scoped to that one docked
//     panel, the same reason the Phase 16 double-click-to-capture check
//     lives there too) -- turns this frame's ImGui mouse state into calls
//     into this file (screenPointToWorldRay(), hitTestGizmoAxes(),
//     updateGizmoDrag()) and, while dragging, writes the resulting position
//     straight back into the selected entity's Transform component, the
//     exact same "EditorUI mutates registry_'s Transform directly" pattern
//     renderInspectorPanel()'s own Position DragFloat3 already establishes
//     (see that call site's own comment).
//   - application.cpp's new Application::renderGizmo() -- draws the three
//     colored arrow meshes (mesh.hpp's makeGizmoArrow()) at the selected
//     entity's world position, using this file's own gizmoAxisLength() so
//     the RENDERED arrows and EditorUI's own hit-test always agree on
//     exactly where the pickable geometry is -- both sides call the same
//     pure function with the same inputs (entity position, camera
//     position/view/projection), so they can never silently disagree.
//
// --- Why local Transform, not world-space-through-the-parent-chain --------
// Exactly transform_hierarchy.hpp's own Phase 14b precedent for
// stepPhysics() (see physics.cpp's own Phase 14b comment, cross-referenced
// from transform_hierarchy.hpp's header comment: "Physics stays
// local-space-only... a RigidBody entity's simulation... continues to
// read/write only its own entity's local Transform"): a gizmo drag computes
// a WORLD-space delta (the ray and the three axis lines this whole file
// works with are all necessarily in world space -- that's the space a
// camera ray lives in) and EditorUI adds that delta directly onto the
// selected entity's own LOCAL Transform::position(), unconverted through any
// parent's own transform. For a root entity (no Parent component -- by far
// the common case, and exactly what the project owner asked to be able to
// move) local position IS world position, so this is exactly correct. For a
// entity that DOES have a Parent (transform_hierarchy.hpp) with its own
// rotation/scale, this is a documented, deliberate simplification, not a bug
// silently overlooked: making the gizmo (or physics) fully parent-hierarchy-
// aware -- inverse-transforming a world delta back through the parent's own
// world matrix before applying it -- is real, separate scope this phase
// does not take on, the identical line transform_hierarchy.hpp's own header
// comment already draws for physics.
//
// This file itself knows nothing about ECS/Transform/parenting at all --
// every function below works in plain glm::vec3/mat4 "world space" (by
// caller convention, not by anything this file enforces), the same
// separation-of-concerns physics.hpp keeps from ecs.hpp's own Transform
// (physics.hpp only reaches into a Transform's position field via
// EntityRegistry, never redefines what "position" means).

#include <glm/glm.hpp>

#include <optional>

namespace engine {

// A world-space ray: `origin` plus a normalized `direction`. The trivial
// default (looking down -Z from the origin) mirrors Camera's own front_
// default (camera.hpp) purely so a default-constructed Ray is a well-defined,
// unit-length direction rather than a zero vector -- never meant to be used
// as-is, just a safe default for aggregate init.
struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

// Standard "unproject two points through the camera's combined view-
// projection matrix and subtract" screen-to-world-ray technique: `screenX`/
// `screenY` are pixel coordinates with the origin at the viewport's own
// top-left corner, x right, y down -- the same convention Dear ImGui's own
// io.MousePos uses relative to a window's GetCursorScreenPos() (see
// editor_ui.cpp's own EditorUI::updateGizmo()), so a caller can hand this
// function `io.MousePos - panelScreenPos` directly with no further
// conversion. `viewportWidth`/`viewportHeight` are that same viewport's
// pixel dimensions (EditorUI's own contentRegion, Phase 14c). `view`/
// `projection` are the exact matrices Camera::getViewMatrix()/
// getProjectionMatrix() return this frame -- this function is otherwise
// independent of the Camera class itself (no #include "engine/camera.hpp"
// here), matching this file's own "works in plain glm types, caller supplies
// the camera state" design.
//
// Unlike closestPointsBetweenLines()/worldPointToScreenPoint() below, this
// function does not guard against a degenerate/singular view-projection
// matrix -- Camera (camera.hpp) never produces one (a valid FOV/aspect/near/
// far always yields an invertible glm::perspective()), so there is no
// realistic caller input that would make glm::inverse() below return
// garbage; adding a defensive check for a case this engine's own Camera
// class cannot produce would just be dead code.
Ray screenPointToWorldRay(float screenX, float screenY, float viewportWidth, float viewportHeight,
                           const glm::mat4& view, const glm::mat4& projection);

// The inverse of screenPointToWorldRay() above: projects a world-space point
// to the same screen-pixel convention (origin top-left, x right, y down).
// Returns std::nullopt when `worldPoint` is behind the camera (its clip-space
// w comes back at or below a small epsilon) -- the identical "no meaningful
// result this frame, not garbage" convention this project already applies at
// the old (Phase 14d, since removed) computeSelectionOutlineNDC()'s own
// w<=epsilon guard: a point behind the camera has no sane screen position,
// and dividing by a near-zero/negative w would silently produce a wildly
// wrong (or NaN/Inf) on-screen coordinate rather than "this isn't
// projectable right now." Not needed by EditorUI::updateGizmo() (which only
// ever unprojects screen->world, never the reverse), but IS needed by
// application.cpp's ENGINE_DEBUG_GIZMO_DRAG headless verification hook,
// which has to compute where on screen the gizmo's own axis handles
// currently render in order to synthesize a realistic mouse-drag sequence
// against them (see that env var's own application.cpp comment).
std::optional<glm::vec2> worldPointToScreenPoint(const glm::vec3& worldPoint, float viewportWidth,
                                                  float viewportHeight, const glm::mat4& view,
                                                  const glm::mat4& projection);

// Which of the gizmo's three translation handles (if any) is being
// interacted with. kNone doubles as both "hit-tested nothing" (
// hitTestGizmoAxes()'s own return) and "not currently dragging" (
// GizmoDragState::axis's default) -- the same "one value plays both roles"
// shape CreateEntityKind::kNone already establishes (editor_ui.hpp).
enum class GizmoAxis {
    kNone,
    kX,
    kY,
    kZ,
};

// The world-space unit direction for `axis` -- (1,0,0)/(0,1,0)/(0,0,1) for
// kX/kY/kZ, matching this whole engine's existing world-axis convention
// (Camera's own worldUp_ is (0,1,0), transform.hpp's translate() has no axis
// concept of its own to disagree with). kNone returns a zero vector -- never
// meant to be used as a real direction, just a well-defined value so this
// function has no unreachable/undefined case for the enum's own default
// member.
glm::vec3 gizmoAxisDirection(GizmoAxis axis);

// Standard "keep the gizmo a roughly constant apparent screen size regardless
// of camera distance" scale: the world-space length of each drawn/pickable
// axis handle, as a function of how far the camera currently is from the
// gizmo's own origin (the selected entity's position). A plain linear scale
// by distance (kGizmoAxisLengthScale) is the standard technique real DCC
// tools use for this exact purpose -- the on-screen SIZE of an object at
// distance d, viewed through a fixed FOV, is proportional to (object's real
// size / d), so scaling the real size proportionally to d exactly cancels
// that and keeps the apparent screen size constant. Floored at
// kGizmoMinAxisLength so a camera sitting extremely close to (or exactly at)
// the gizmo's own origin doesn't collapse it to a zero/negative-length,
// unusable sliver.
constexpr float kGizmoAxisLengthScale = 0.15f;
constexpr float kGizmoMinAxisLength = 0.15f;
float gizmoAxisLength(float distanceToCamera);

// The world-space pick tolerance (how close the mouse ray has to pass to an
// axis handle to count as a hit) for a gizmo of the given `axisLength` --
// scaled together with the handle itself (kGizmoPickToleranceScale) so a
// gizmo rendered small (camera far away) isn't proportionally *harder* to
// click than one rendered large (camera close), matching gizmoAxisLength()'s
// own "constant apparent size" goal applied to its pick tolerance too.
constexpr float kGizmoPickToleranceScale = 0.18f;
float gizmoPickTolerance(float axisLength);

// The closest-approach parameters between `ray` and the infinite 3D line
// through `linePoint` in direction `lineDirection` (`lineDirection` need not
// be pre-normalized -- this function normalizes it internally; `ray.
// direction` IS assumed already normalized, matching every Ray this file's
// own screenPointToWorldRay() ever produces). Returns std::nullopt when the
// two directions are nearly parallel (the standard closest-point-between-
// two-lines formula's determinant, 1 - dot(d1,d2)^2 for unit d1/d2, going
// near zero -- physically, this is "the camera is looking straight down (or
// up) the axis being tested," where a ray-vs-line intersection is genuinely
// undefined: infinitely many points on the line are all equally "closest").
// Never divides by a value it hasn't first checked isn't near-zero -- the
// "no meaningful result this frame, not garbage" convention this project
// already applies elsewhere (see this file's own worldPointToScreenPoint()
// comment above, and Phase 18d's selection_mask.frag uDepthBias comment, for
// the same instinct applied to a different near-degenerate case).
//
// `rayT`/`lineT`: the parametric distance (world units, since both
// directions are unit length) along `ray.direction` from `ray.origin`, and
// along the normalized `lineDirection` from `linePoint`, respectively, at
// which the two lines come closest to each other.
struct LineClosestPoint {
    float rayT = 0.0f;
    float lineT = 0.0f;
};
std::optional<LineClosestPoint> closestPointsBetweenLines(const Ray& ray, const glm::vec3& linePoint,
                                                           const glm::vec3& lineDirection);

// The result of testing `ray` against all three of the gizmo's own axis
// handles (each a finite segment [gizmoOrigin, gizmoOrigin +
// axisLength*axisDirection]) -- kNone (axisT left at its default 0) when the
// ray passes no closer than `pickToleranceWorld` to any of the three, or
// when every one of the three axis directions happens to be nearly parallel
// to the ray (see closestPointsBetweenLines()'s own comment; geometrically
// implausible for all three at once, but handled the same defensive way
// regardless). `axisT` is where along the HIT axis (world units from
// gizmoOrigin, already clamped to [0, axisLength] -- a hit behind the
// gizmo's own origin or past its visible tip doesn't count) the closest
// approach landed.
struct AxisHitTestResult {
    GizmoAxis axis = GizmoAxis::kNone;
    float axisT = 0.0f;
};
AxisHitTestResult hitTestGizmoAxes(const Ray& ray, const glm::vec3& gizmoOrigin, float axisLength,
                                    float pickToleranceWorld);

// The gizmo's own persistent cross-frame drag state -- the "not dragging /
// dragging-axis-X / dragging-axis-Y / dragging-axis-Z" state machine the
// project owner's own brief calls for, in the identical shape
// decideCameraCapture()'s own CameraCaptureDecision (camera_capture.hpp)
// already establishes for a different two-state machine: a plain struct the
// caller (EditorUI) owns as a member and threads back in as `current` on the
// next call, not a class with hidden internal state.
//
// `axis == kNone` means "not currently dragging" -- `startAxisT`/
// `startEntityPosition` are meaningless in that state (left at their
// defaults) and are only ever read back by updateGizmoDrag() itself while
// `axis != kNone`. `startEntityPosition` is the gizmo's own world-space
// origin (the selected entity's position) AS OF THE FRAME THE DRAG STARTED
// -- deliberately NOT re-read every frame while dragging, since the axis
// LINE being dragged along has to stay fixed in space for the whole
// gesture (re-anchoring it to the entity's own constantly-changing position
// while the entity is, itself, what's being moved by this same drag would
// make the two feed back into each other nonsensically).
struct GizmoDragState {
    GizmoAxis axis = GizmoAxis::kNone;
    float startAxisT = 0.0f;
    glm::vec3 startEntityPosition{0.0f};
};

// GizmoDragResult::newPosition is set (to `startEntityPosition + delta`,
// where delta is this frame's fresh closest-point-on-the-fixed-drag-axis
// minus `startAxisT`) on every frame an in-progress drag actually moves
// anything, std::nullopt on every other frame (not dragging at all; the
// frame a drag starts, which only grabs the axis without moving anything
// yet; the frame it ends; or a frame where the ray happened to be too
// parallel to the drag axis to compute a meaningful update -- see
// closestPointsBetweenLines()'s own comment). The caller (EditorUI) is what
// decides what "position" means for its own purposes (this file has no idea
// what a Transform is -- see this file's own header comment on why the
// local-vs-world-space application of this result is the CALLER's decision,
// not this function's).
struct GizmoDragResult {
    GizmoDragState state;
    std::optional<glm::vec3> newPosition;
};

// The whole drag state machine, in one pure function -- the same
// "current state + this frame's inputs -> next state (+ any derived output)"
// shape decideCameraCapture() already establishes (camera_capture.hpp), just
// with a real glm::vec3 payload alongside the state transition instead of a
// pair of bools.
//
//   - current.axis == kNone (not dragging):
//       - mousePressedThisFrame && hoverAxis != kNone: GRABS that axis --
//         computes this frame's closest point on the (infinite) line through
//         `entityPosition` in hoverAxis's own direction, and if that
//         succeeds (see closestPointsBetweenLines()), starts dragging: next
//         state has axis=hoverAxis, startAxisT = that closest point's lineT,
//         startEntityPosition = entityPosition. newPosition is std::nullopt
//         this same frame -- a grab only anchors the drag, it doesn't yet
//         move anything (there's no "delta" to speak of on the very frame
//         the mouse first went down). If closestPointsBetweenLines() itself
//         fails (the ray is nearly parallel to the axis being grabbed --
//         geometrically, the camera is looking almost straight down/up that
//         axis right where the user clicked), the grab is silently declined
//         -- stays kNone -- rather than starting a drag with a meaningless
//         anchor.
//       - anything else (no press, or a press somewhere hoverAxis says isn't
//         a handle): stays kNone, newPosition = std::nullopt. `mouseDown`
//         alone (no fresh press) intentionally does nothing here -- a
//         click-and-hold that happened to originate somewhere else (a
//         toolbar button, empty viewport space) and later drifts, while
//         still held, over a gizmo handle must NOT retroactively start a
//         drag; only requiring `mousePressedThisFrame` (an EDGE, exactly the
//         same load-bearing contract camera_capture.hpp's own
//         escapeJustPressed already documents) guarantees a drag only ever
//         starts from a press that began directly on a handle.
//   - current.axis != kNone (dragging):
//       - !mouseDown: the button was released -- RETURNS to kNone (every
//         field reset to its default), newPosition = std::nullopt (the
//         entity's position already reflects the last frame that DID move
//         it; releasing the button moves nothing further).
//       - mouseDown (still held): re-projects `ray` onto the SAME fixed axis
//         line this drag grabbed (anchored at current.startEntityPosition,
//         never re-read from `entityPosition` -- see GizmoDragState's own
//         comment on why). If that succeeds, newPosition =
//         current.startEntityPosition + (thisFrame'sLineT -
//         current.startAxisT) * axisDirection(current.axis); state is
//         otherwise UNCHANGED (still the same axis/startAxisT/
//         startEntityPosition the drag began with -- only the derived
//         newPosition varies frame to frame, not the anchor itself). If it
//         fails (ray briefly parallel to the axis mid-drag), state still
//         stays unchanged but newPosition is std::nullopt for just this one
//         frame -- the entity simply holds its last position rather than
//         jumping to a garbage one, and the very next frame the ray is no
//         longer degenerate resumes tracking normally from the SAME anchor,
//         with no accumulated drift from the skipped frame(s).
//
// `entityPosition` is only ever consulted in the "grab" branch above (to
// seed startEntityPosition) -- a caller mid-drag is free to pass whatever it
// has on hand (even a stale value), since it's ignored while current.axis !=
// kNone.
GizmoDragResult updateGizmoDrag(const GizmoDragState& current, bool mouseDown, bool mousePressedThisFrame,
                                 GizmoAxis hoverAxis, const Ray& ray, const glm::vec3& entityPosition);

// Phase 18j introduced this as which of the two gizmo tools built so far is
// currently active -- NOT yet a real, user-facing move/rotate/scale switcher
// (that was explicitly deferred to Phase 18k, once scale existed too and all
// three tools could be switched between with one W/E/R-style mechanism built
// once rather than revised twice). Phase 18k is that phase: kScale is new
// below, and `gizmoMode_` (application.hpp) is now a REAL runtime-mutable
// value, flipped live by W/E/R (Application::run(), gated the same
// `!ImGui::GetIO().WantCaptureKeyboard`-and-edge-triggered way this project's
// existing Ctrl+S/Ctrl+Z chords already are, plus a new `!cameraCaptured_`
// gate -- W and E collide with this engine's own free-fly camera bindings,
// InputActionMap::MoveForward/MoveUp, input_action_map.cpp -- so the tool
// switcher only ever fires while the camera is NOT captured, exactly when a
// gizmo can be interacted with in the first place). ENGINE_DEBUG_GIZMO_MODE
// (application.cpp) still exists, now just as the STARTING mode for a
// headless run rather than the only way to reach a non-translate tool.
// Lives in this pure header (not editor_ui.hpp/application.hpp) because both
// EditorUI's interaction code and Application's rendering code need to agree
// on it, the same "shared pure vocabulary both GL/ImGui-facing halves
// consume" role GizmoAxis above already plays.
enum class GizmoMode {
    kTranslate,
    kRotate,
    kScale,
};

// ===========================================================================
// Phase 18j: the rotate gizmo's own additions below -- three colored rings
// (X=red/Y=green/Z=blue, the identical axis-color convention the translate
// gizmo's own three arrows already established), each lying in the plane
// PERPENDICULAR to its own axis (the X ring lies in the Y-Z plane, etc.),
// click-dragged to spin the selected entity around that one axis. Follows
// the same "pure decision function here, ImGui wiring in EditorUI,
// Application applies the result" split the translate gizmo above
// establishes -- see this header's own top-of-file comment for the general
// shape, unchanged by this phase.
// ===========================================================================

// The result of intersecting `ray` with the infinite plane through
// `planePoint` with unit normal `planeNormal` (`planeNormal` need not be
// pre-normalized -- this function normalizes it internally, the same
// convention closestPointsBetweenLines() already applies to its own
// `lineDirection`). Returns std::nullopt in exactly two cases, both "no
// meaningful result this frame, not garbage" (the same convention every
// other near-degenerate case in this file already follows):
//   - `ray.direction` nearly perpendicular to `planeNormal` (dot() near
//     zero) -- physically, the ray is grazing almost exactly ALONG the
//     plane itself, where a well-defined single intersection point doesn't
//     exist (or is arbitrarily far away for an infinitesimally smaller
//     angle) -- the plane-equivalent of closestPointsBetweenLines()'s own
//     near-parallel-lines case, and reused here as the SAME threshold
//     (kParallelEpsilon, gizmo.cpp) for the identical reason: both are "the
//     angle between two unit directions is near zero" degeneracies.
//   - the computed intersection lies BEHIND the ray's own origin (t < 0) --
//     never a usable result for a mouse ray cast forward from the camera,
//     the same "reject, don't hand back a point no on-screen click could
//     have produced" instinct worldPointToScreenPoint()'s own w<=epsilon
//     guard already applies to a different kind of behind-the-camera case.
struct RayPlaneHit {
    float t = 0.0f;
    glm::vec3 point{0.0f};
};
std::optional<RayPlaneHit> intersectRayWithPlane(const Ray& ray, const glm::vec3& planePoint,
                                                  const glm::vec3& planeNormal);

// The two orthonormal basis vectors spanning the plane perpendicular to
// `axis` -- `u` is this ring's own angle-zero direction, `v` is the
// direction a POSITIVE angle sweeps toward. `v` is deliberately chosen as
// cross(gizmoAxisDirection(axis), u) (hand-verified per axis below), which
// is exactly what makes an angle DELTA computed in this basis, when handed
// to Transform::rotate(deltaDeg, gizmoAxisDirection(axis)) (transform.hpp),
// visually spin the grabbed ring point in the SAME direction the mouse
// dragged it -- glm::angleAxis(theta, a) rotates a vector starting
// perpendicular to `a` towards cross(a, that vector) for small positive
// theta (the standard right-hand-rule Rodrigues-formula fact), so choosing
// v = cross(axis, u) up front means this header's own angle convention and
// Transform::rotate()'s own rotation convention agree by construction,
// rather than by ad-hoc sign-matching at every call site.
//   kX: u=(0,1,0), v=cross((1,0,0),(0,1,0))=(0,0,1)
//   kY: u=(0,0,1), v=cross((0,1,0),(0,0,1))=(1,0,0)
//   kZ: u=(1,0,0), v=cross((0,0,1),(1,0,0))=(0,1,0)
// kNone returns both vectors zeroed -- never meant to be used as a real
// basis, just a well-defined value for the enum's own default case, the
// same convention gizmoAxisDirection(kNone) already establishes.
struct RingPlaneBasis {
    glm::vec3 u{0.0f};
    glm::vec3 v{0.0f};
};
RingPlaneBasis gizmoRingPlaneBasis(GizmoAxis axis);

// The angle (radians, atan2()'s own (-pi, pi] range) of `point` around
// `gizmoOrigin`, measured in the plane perpendicular to `axis` using
// gizmoRingPlaneBasis(axis)'s own (u, v) convention above: angle 0 sits at
// `gizmoOrigin + u`, and angle increases towards `v`. `point` need not lie
// exactly at the ring's own radius, or exactly in-plane -- only its
// projection onto (u, v) is used (dot(point - gizmoOrigin, u) and
// dot(point - gizmoOrigin, v)), so any point intersectRayWithPlane() above
// hands back (already exactly in-plane by construction) yields a
// well-defined angle regardless of exactly how far from the ring's own
// drawn radius it landed.
float gizmoRingAngle(const glm::vec3& point, const glm::vec3& gizmoOrigin, GizmoAxis axis);

// The result of testing `ray` against all three of the gizmo's own rings
// (each the circle of radius `ringRadius`, centered at `gizmoOrigin`, lying
// in the plane perpendicular to that axis) -- kNone when the ray's
// plane-intersection point (intersectRayWithPlane() above) lands no closer
// than `pickToleranceWorld` to any of the three circles' own radius, or
// when the ray happens to be near-parallel to every one of the three
// planes at once (geometrically implausible for all three simultaneously,
// but handled the same defensive way regardless, exactly like
// hitTestGizmoAxes()'s own identical all-three-parallel corner case).
//
// The "distance from the ray to a ring" this function actually measures is
// |length(planeIntersectionPoint - gizmoOrigin) - ringRadius| -- since the
// plane-intersection point is, by construction, exactly IN the same plane
// the ring's own circle lies in, this is the EXACT (not approximate)
// distance from that point to the closest point on the circle: the
// closest point on a circle to any other point in its own plane always
// lies along the radial direction through that point, at a distance equal
// to the difference between the two radii. This is the standard technique
// real DCC tools use for ring-gizmo picking (project the ray onto the
// ring's own plane, then compare radial distance) -- genuine closest-point-
// on-a-CIRCLE math, not an approximation that treats the ring as a straight
// line segment the way hitTestGizmoAxes() correctly does for the
// translate gizmo's own straight arrow handles.
struct RingHitTestResult {
    GizmoAxis axis = GizmoAxis::kNone;
};
RingHitTestResult hitTestGizmoRings(const Ray& ray, const glm::vec3& gizmoOrigin, float ringRadius,
                                     float pickToleranceWorld);

// The rotate gizmo's own persistent cross-frame drag state -- the
// "not dragging / dragging-ring-X/Y/Z" state machine, the same shape
// GizmoDragState above establishes for the translate gizmo, just carrying
// an angle anchor instead of a position one.
//
// `axis == kNone` means "not currently dragging" (`lastAngle` is
// meaningless in that state, left at its default). Unlike GizmoDragState's
// own `startAxisT`/`startEntityPosition` -- which anchor a translate drag
// to a FIXED point captured once at grab time, since updateGizmoDrag()
// recomputes the drag's TOTAL delta from that fixed anchor every frame and
// hands the caller an absolute new position to assign -- `lastAngle` is
// deliberately updated EVERY frame (see updateGizmoRotateDrag()'s own
// comment below for why): this state machine hands the caller an
// INCREMENTAL angle delta each frame, meant to be applied via
// Transform::rotate() (which itself composes onto whatever rotation is
// already live, transform.hpp's own documented "new_rotation = incoming *
// old" contract) rather than a second, hand-rolled quaternion composition
// scheme recomputing an absolute result from a fixed start every frame the
// way the translate gizmo's own setPosition()-based application does.
struct GizmoRotateDragState {
    GizmoAxis axis = GizmoAxis::kNone;
    float lastAngle = 0.0f;
};

// GizmoRotateDragResult::deltaAngleDeg is set (to the shortest signed
// angle, in DEGREES, from `current.lastAngle` to this frame's freshly
// measured ring angle -- see below for the wraparound handling) on every
// frame an in-progress drag actually has a fresh angle to report;
// std::nullopt on every other frame (not dragging at all; the frame a drag
// starts, which only anchors `lastAngle` without reporting any delta yet;
// the frame it ends; or a frame where the ray happened to be too
// near-parallel to the ring's own plane to compute a meaningful angle --
// see intersectRayWithPlane()'s own comment). The caller
// (EditorUI::updateGizmoRotate()) is what actually applies this delta, via
// `transform->rotate(*deltaAngleDeg, gizmoAxisDirection(state.axis))` --
// this file has no idea what a Transform is, the same separation
// GizmoDragResult's own header comment above already documents for the
// translate gizmo.
struct GizmoRotateDragResult {
    GizmoRotateDragState state;
    std::optional<float> deltaAngleDeg;
};

// The rotate gizmo's whole drag state machine, in one pure function -- the
// same "current state + this frame's inputs -> next state (+ any derived
// output)" shape updateGizmoDrag() above already establishes for the
// translate gizmo, adapted for an angle-based (not position-based) drag:
//
//   - current.axis == kNone (not dragging):
//       - mousePressedThisFrame && hoverAxis != kNone: GRABS that ring --
//         intersects `ray` with the plane through `gizmoOrigin`
//         perpendicular to hoverAxis (intersectRayWithPlane()) and, if that
//         succeeds, starts dragging: next state has axis=hoverAxis,
//         lastAngle = gizmoRingAngle() of the intersection point.
//         deltaAngleDeg is std::nullopt this same frame -- a grab only
//         anchors the drag, exactly mirroring updateGizmoDrag()'s own "the
//         grab frame produces no position update" contract, applied here to
//         angle instead. If the plane intersection fails (the ray is
//         grazing the ring's own plane right where the user clicked, or
//         lands behind the ray's own origin), the grab is silently
//         declined -- stays kNone -- rather than starting a drag anchored
//         to a meaningless angle.
//       - anything else (no press, or a press somewhere hoverAxis says
//         isn't a ring): stays kNone, deltaAngleDeg = std::nullopt. Exactly
//         updateGizmoDrag()'s own edge-triggering contract -- a held-but-
//         not-freshly-pressed button hovering a ring must not retroactively
//         start a drag.
//   - current.axis != kNone (dragging):
//       - !mouseDown: released -- RETURNS to kNone (every field reset to
//         its default), deltaAngleDeg = std::nullopt (the entity's rotation
//         already reflects the last frame that DID report a delta;
//         releasing the button rotates nothing further).
//       - mouseDown (still held): re-intersects `ray` with the SAME ring's
//         plane (through `gizmoOrigin`, perpendicular to current.axis --
//         note this is `gizmoOrigin`, not a cached grab-time anchor point,
//         since the plane a ring lies in is defined purely by its axis
//         direction through the gizmo's own current origin, unlike the
//         translate gizmo's axis LINE, which has to stay fixed in space for
//         the whole gesture -- see GizmoDragState's own comment for why
//         THAT anchor is frozen; a rotation's own plane has no equivalent
//         "already moving out from under the drag" problem, since spinning
//         an entity in place doesn't relocate `gizmoOrigin`). If that
//         succeeds, deltaAngleDeg = the shortest signed angle (radians,
//         wrapped via atan2(sin(delta), cos(delta)) so a grab spanning the
//         ±180-degree discontinuity reports a small delta in the right
//         direction rather than an enormous wrong-signed jump) from
//         current.lastAngle to this frame's freshly measured angle,
//         converted to degrees; state.lastAngle is updated to this frame's
//         freshly measured angle (NOT incremented by the delta -- reading
//         the ray fresh every frame this way means one skipped/degenerate
//         frame never leaves the next good frame's own delta polluted by
//         it). If it fails (ray briefly grazing the ring's own plane
//         mid-drag), state.lastAngle is left UNCHANGED (still whatever the
//         last successful frame measured) and deltaAngleDeg is
//         std::nullopt for just this one frame -- the entity simply holds
//         its last rotation rather than jumping to a garbage one, and the
//         very next non-degenerate frame's delta is computed against that
//         same still-valid last-known angle, with no accumulated drift from
//         the skipped frame(s) -- the identical resume-cleanly guarantee
//         updateGizmoDrag()'s own header comment already documents for its
//         own briefly-parallel mid-drag case.
GizmoRotateDragResult updateGizmoRotateDrag(const GizmoRotateDragState& current, bool mouseDown,
                                             bool mousePressedThisFrame, GizmoAxis hoverAxis, const Ray& ray,
                                             const glm::vec3& gizmoOrigin);

// ===========================================================================
// Phase 18k: the scale gizmo's own additions below -- three colored handles
// (X=red/Y=green/Z=blue, the identical axis-color convention the translate
// gizmo's arrows and the rotate gizmo's rings both already establish),
// visually a thin shaft with a CUBE tip instead of the translate gizmo's
// cone tip (mesh.hpp's makeGizmoScaleHandle()) -- the standard DCC-tool
// visual distinction between move and scale handles -- click-dragged to
// scale the selected entity along that one axis (non-uniform scale: only the
// dragged axis's own Transform::scale() component changes).
//
// Reuses hitTestGizmoAxes() UNMODIFIED for hit-testing -- a scale handle is
// geometrically the exact same thing a translate handle already is for
// picking purposes (a straight finite segment from `gizmoOrigin` outward
// along one world axis; the cube-vs-cone tip shape only affects what's
// DRAWN, not the pickable line-segment geometry hitTestGizmoAxes() already
// tests against), so a second, near-duplicate hit-test function would be
// pure, unjustified duplication.
//
// closestPointsBetweenLines() (already used by updateGizmoDrag() above) is
// also reused unmodified for the drag math itself -- a scale drag is
// mathematically almost identical to a translate drag (both measure where
// along a fixed world-space axis line the mouse ray currently projects), the
// two differ only in what they DO with that measurement: updateGizmoDrag()
// hands back an absolute new position; updateGizmoScaleDrag() below instead
// converts "how far along the axis, now, versus at grab time" into a scale
// RATIO, applied by multiplying it onto whatever scale the entity already
// had along that one axis at grab time.
// ===========================================================================

// The scale drag's own "world distance along the axis, at grab time, is too
// close to zero to divide by" floor -- see updateGizmoScaleDrag()'s own
// comment for exactly where this is used. A grab landing closer to
// `gizmoOrigin` than this (world units) is declined outright (the same
// "decline rather than produce garbage" instinct closestPointsBetweenLines()'s
// own near-parallel case and intersectRayWithPlane()'s own grazing case both
// already apply, just for a different degeneracy: here, a near-zero
// DENOMINATOR in a ratio, rather than a near-zero determinant/near-zero
// dot product) -- a click landing within a hundredth of a world unit of the
// gizmo's own origin, where all three axis handles nearly converge, is not a
// deliberate aim at any one axis's own handle anyway (see hitTestGizmoAxes()'s
// own AxisHitTestResult -- a real handle is drawn/picked out at
// gizmoAxisLength()'s own distance away, never at distance ~0).
constexpr float kGizmoScaleMinGrabDistance = 0.01f;

// The scale drag's own floor on the RESULTING scale component -- never lets
// a drag push any one axis's Transform::scale() component to exactly zero or
// negative. See transform.hpp's Transform::getModelMatrix(): a zero scale
// component collapses the model matrix along that axis to a singular
// (non-invertible) matrix -- degenerate for anything downstream that needs
// to invert it (e.g. a normal matrix built as
// transpose(inverse(mat3(model))), the standard technique this engine's own
// lit shaders use) -- and a NEGATIVE scale component mirrors the mesh along
// that axis without correspondingly flipping its triangle winding, which
// real GL rendering does not correct for on its own: the mesh would render
// visually inside-out/inverted (and, combined with the same uninverted
// normal-matrix concern above, wrong-signed lighting normals too) rather
// than the "flip in place" a user dragging a scale handle would actually
// expect. 0.01 (not exactly 0.0) leaves the entity still visibly present
// (a barely-visible sliver, not literally invisible) and keeps every
// downstream matrix operation well-conditioned, the same "floored, not
// zeroed" instinct gizmoAxisLength()'s own kGizmoMinAxisLength already
// applies to a different quantity for a related "never let this collapse to
// an unusable value" reason.
constexpr float kGizmoMinScaleComponent = 0.01f;

// The scale gizmo's own persistent cross-frame drag state -- the same
// "not dragging / dragging-axis-X/Y/Z" shape GizmoDragState establishes for
// translate, adapted for a RATIO-based drag instead of a position-delta one.
//
// `axis == kNone` means "not currently dragging" (the other three fields are
// meaningless in that state, left at their defaults). `startAxisT` is the
// (unclamped) world-space distance along the grabbed axis, from
// `startGizmoOrigin`, that the ray's closest approach landed at on the grab
// frame -- this is the RATIO's own denominator every later frame's fresh
// measurement is divided by (see updateGizmoScaleDrag()'s own comment for
// exactly how), so it is captured once at grab time and never recomputed,
// the identical "anchor frozen at grab time" discipline
// GizmoDragState::startAxisT already establishes for translate (and for the
// identical reason: the axis LINE this ratio is measured against has to stay
// fixed in space for the whole gesture, not slide out from under the drag as
// the entity's own scale -- and therefore the gizmo handle's own on-screen
// length -- visibly grows or shrinks while dragging). `startEntityScale` is
// the entity's full Transform::scale() as of the grab frame -- every later
// frame's reported scale multiplies ONLY the dragged axis's own component of
// THIS frozen value by that frame's fresh ratio, leaving the other two
// components exactly as they were at grab time (not re-multiplied frame over
// frame, which would compound rounding error and also fight a live Inspector
// edit to another axis mid-drag). `startGizmoOrigin` is the world-space
// gizmo origin (the selected entity's resolved world position) as of the
// grab frame, frozen for the identical "the line has to stay fixed" reason
// `startAxisT` is.
struct GizmoScaleDragState {
    GizmoAxis axis = GizmoAxis::kNone;
    float startAxisT = 0.0f;
    glm::vec3 startEntityScale{1.0f, 1.0f, 1.0f};
    glm::vec3 startGizmoOrigin{0.0f};
};

// GizmoScaleDragResult::newScale is set (to `startEntityScale` with only the
// dragged axis's own component replaced by `startEntityScale[axis] * ratio`,
// clamped to never go below kGizmoMinScaleComponent -- see below for exactly
// how `ratio` is computed) on every frame an in-progress drag actually has a
// fresh measurement to report; std::nullopt on every other frame (not
// dragging at all; the frame a drag starts, which only anchors the drag
// without scaling anything yet; the frame it ends; or a frame where the ray
// happened to be too parallel to the drag axis to compute a meaningful
// update -- see closestPointsBetweenLines()'s own comment). The caller
// (EditorUI::updateGizmoScale()) is what actually applies this via
// `transform->setScale(*newScale)` -- this file has no idea what a Transform
// is, the identical separation GizmoDragResult's/GizmoRotateDragResult's own
// header comments already document for the other two tools.
struct GizmoScaleDragResult {
    GizmoScaleDragState state;
    std::optional<glm::vec3> newScale;
};

// The scale drag's whole state machine, in one pure function -- the same
// "current state + this frame's inputs -> next state (+ derived output)"
// shape updateGizmoDrag()/updateGizmoRotateDrag() above both already
// establish:
//
//   - current.axis == kNone (not dragging):
//       - mousePressedThisFrame && hoverAxis != kNone: GRABS that axis --
//         computes this frame's closest point on the (infinite) line through
//         `gizmoOrigin` in hoverAxis's own direction
//         (closestPointsBetweenLines(), reused unmodified from the translate
//         gizmo). If that succeeds AND the resulting distance
//         (|closest->lineT|) is at least kGizmoScaleMinGrabDistance away
//         from the origin (see that constant's own comment for why a grab
//         any closer is declined rather than anchored to a near-zero
//         denominator), starts dragging: next state has axis=hoverAxis,
//         startAxisT=closest->lineT, startEntityScale=`entityScale`,
//         startGizmoOrigin=`gizmoOrigin`. newScale is std::nullopt this same
//         frame -- a grab only anchors the drag, the identical "the grab
//         frame produces no update" contract updateGizmoDrag()'s own header
//         comment documents. Declined (stays kNone) if the plane/line
//         intersection fails (ray nearly parallel to the axis) OR the
//         distance-floor check above fails.
//       - anything else: stays kNone, newScale=std::nullopt -- the identical
//         edge-triggering contract (a held-but-not-freshly-pressed button
//         must not retroactively start a drag) updateGizmoDrag()'s own
//         header comment already documents in full.
//   - current.axis != kNone (dragging):
//       - !mouseDown: released -- RETURNS to kNone (every field reset to its
//         default), newScale=std::nullopt.
//       - mouseDown (still held): re-projects `ray` onto the SAME fixed axis
//         line this drag grabbed (anchored at current.startGizmoOrigin,
//         never re-read from `gizmoOrigin` -- identical reasoning
//         GizmoDragState's own header comment gives for why translate's own
//         anchor is frozen, since the entity's own scale -- and therefore
//         this handle's own drawn length -- is exactly what this drag is
//         changing). If that succeeds, `ratio = closest->lineT /
//         current.startAxisT` (both measured along the SAME frozen line, so
//         this is exactly "how far along the axis now, versus at grab time"
//         -- dragging further from the origin than the grab point gives
//         ratio > 1 (scale up), dragging back toward (or past) the origin
//         gives ratio < 1 or even negative (scale down, then flip) --
//         negative/near-zero results are exactly what
//         kGizmoMinScaleComponent's own floor below catches);
//         `newScale = current.startEntityScale` with the dragged axis's own
//         component replaced by
//         `max(current.startEntityScale[axis] * ratio,
//         kGizmoMinScaleComponent)`. state is otherwise UNCHANGED (the same
//         "only the derived output varies frame to frame, not the anchor
//         itself" contract updateGizmoDrag() already documents). If it fails
//         (ray briefly parallel to the axis mid-drag), state stays unchanged
//         and newScale is std::nullopt for just this one frame -- the entity
//         simply holds its last scale rather than jumping to a garbage one,
//         resuming cleanly from the same anchor the next non-degenerate
//         frame, the identical guarantee updateGizmoDrag()'s own header
//         comment documents for its own briefly-parallel case.
//
// `gizmoOrigin`/`entityScale` are only ever consulted in the "grab" branch
// above (to seed startGizmoOrigin/startEntityScale) -- a caller mid-drag is
// free to pass whatever it has on hand (even stale values), since both are
// ignored while current.axis != kNone, the identical contract
// updateGizmoDrag()'s own header comment documents for its own
// `entityPosition` parameter.
GizmoScaleDragResult updateGizmoScaleDrag(const GizmoScaleDragState& current, bool mouseDown,
                                           bool mousePressedThisFrame, GizmoAxis hoverAxis, const Ray& ray,
                                           const glm::vec3& gizmoOrigin, const glm::vec3& entityScale);

}  // namespace engine

#endif  // ENGINE_GIZMO_HPP
