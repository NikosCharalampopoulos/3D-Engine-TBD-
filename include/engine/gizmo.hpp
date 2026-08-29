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

}  // namespace engine

#endif  // ENGINE_GIZMO_HPP
