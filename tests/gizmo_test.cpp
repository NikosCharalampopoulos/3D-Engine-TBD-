// Phase 18e's own test: exercises every pure function in gizmo.hpp
// (src/gizmo.cpp) against hand-computed expected values -- same "plain
// executable, links only the pure logic file it's testing" shape
// physics_test/camera_capture_test already establish (see those files' own
// header comments). gizmo.cpp depends on nothing beyond GLM, so this needs
// no live window/GL/ImGui context either -- which matters more here than
// for most of this project's other pure-logic tests, since the interactive
// gesture this whole feature exists for (actually dragging a mouse across a
// gizmo handle) is NOT reproducible at all in this project's headless Xvfb
// environment (no physical pointer device) -- this test file is therefore
// this phase's single most important verification surface, not merely a
// supplement to a real interactive check the way some other tests are.
//
// Every camera/ray setup below is chosen specifically so the expected result
// can be derived by hand from the plain trigonometry/linear-algebra this
// file's own functions document, not just eyeballed against whatever the
// code under test happens to produce.

#include "engine/gizmo.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int failures = 0;

void expectTrue(bool condition, const std::string& what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

void expectNear(float actual, float expected, const std::string& what, float epsilon = 1e-3f) {
    expectTrue(glm::epsilonEqual(actual, expected, epsilon),
               what + " (expected " + std::to_string(expected) + ", got " + std::to_string(actual) + ")");
}

void expectVec3Near(const glm::vec3& actual, const glm::vec3& expected, const std::string& what,
                     float epsilon = 1e-3f) {
    expectNear(actual.x, expected.x, what + ".x", epsilon);
    expectNear(actual.y, expected.y, what + ".y", epsilon);
    expectNear(actual.z, expected.z, what + ".z", epsilon);
}

}  // namespace

int main() {
    using namespace engine;

    // ==== screenPointToWorldRay() ==========================================
    // Camera at the world origin, looking straight down -Z with +Y up --
    // glm::lookAt(eye=(0,0,0), center=(0,0,-1), up=(0,1,0)) is exactly GL's
    // own default view orientation, so `view` comes out as the identity
    // matrix (hand-verifiable: every basis vector of this camera's own local
    // frame already IS the corresponding world axis). fovY=90 degrees,
    // aspect=1, near=1, far=10 are all chosen so the frustum's half-extent
    // at the near/far planes is an exact, round tan(45 deg) = 1 world unit,
    // making every unprojected corner/center point a clean value to derive
    // by hand rather than an arbitrary trig result.
    {
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 10.0f);
        constexpr float kViewportSize = 400.0f;

        // Screen center -> NDC (0,0) -> straight down -Z. At NDC z=-1 (near)
        // the unprojected view-space point is (0,0,-near) = (0,0,-1); at
        // NDC z=+1 (far) it's (0,0,-far) = (0,0,-10) -- both exactly on the
        // view axis since x=y=0 at screen center. Since view==identity here,
        // view space IS world space.
        {
            const Ray ray = screenPointToWorldRay(kViewportSize / 2.0f, kViewportSize / 2.0f, kViewportSize,
                                                   kViewportSize, view, projection);
            expectVec3Near(ray.origin, glm::vec3(0.0f, 0.0f, -1.0f), "center ray origin");
            expectVec3Near(ray.direction, glm::vec3(0.0f, 0.0f, -1.0f), "center ray direction");
        }

        // Screen's right edge, vertical center -> NDC (1, 0). With a 90-deg
        // vertical FOV and aspect 1 (so horizontal FOV is also 90 deg here),
        // the near-plane half-width equals the near-plane half-height:
        // tan(45 deg) * near = 1 * 1 = 1, so NDC x=1 unprojects to view-space
        // x=+1 at the near plane (z=-1) and x=+10 (scaled by distance) at
        // the far plane (z=-10). direction = normalize((10-1, 0, -10-(-1)))
        // = normalize((9, 0, -9)) = (1/sqrt(2), 0, -1/sqrt(2)).
        {
            const Ray ray =
                screenPointToWorldRay(kViewportSize, kViewportSize / 2.0f, kViewportSize, kViewportSize, view, projection);
            expectVec3Near(ray.origin, glm::vec3(1.0f, 0.0f, -1.0f), "right-edge ray origin");
            const float kInvSqrt2 = 0.70710678f;
            expectVec3Near(ray.direction, glm::vec3(kInvSqrt2, 0.0f, -kInvSqrt2), "right-edge ray direction");
        }

        // Screen's TOP edge (pixel row 0), horizontal center -> NDC (0, +1).
        // This is the case the right-edge test above CANNOT stand in for:
        // every case that instead uses screenY = kViewportSize/2 lands at
        // ndcY=0 under either the correct Y-flip formula (ndcY = 1 -
        // 2*screenY/height) or an accidentally inverted one (ndcY =
        // 2*screenY/height - 1) -- both give 0 at the vertical center, so
        // neither can catch a flip-direction regression. Pixel row 0 can:
        // correctly it must map to ndcY=+1 (top of NDC, y-up), not -1.
        // Same derivation shape as the right-edge case with x and y swapped:
        // near-plane view-space y=+1 at z=-1 (since ndc.y=1 and the near
        // half-height is 1, same tan(45deg)*near=1 fact used above), and at
        // the far plane y = ndc.y * far = 1*10 = 10 at z=-10 (again, same
        // "x = ndc.x * (-z)" scaling the right-edge case's own comment
        // derives, just applied to y). direction = normalize((0,10,-10) -
        // (0,1,-1)) = normalize((0,9,-9)) = (0, 1/sqrt2, -1/sqrt2).
        // Under an inverted flip, screenY=0 would instead produce ndcY=-1,
        // giving origin (0,-1,-1) and direction (0,-1/sqrt2,-1/sqrt2) -- the
        // opposite sign on y throughout -- so this case visibly tells the
        // two conventions apart where the center-screen cases above cannot.
        {
            const Ray ray =
                screenPointToWorldRay(kViewportSize / 2.0f, 0.0f, kViewportSize, kViewportSize, view, projection);
            const float kInvSqrt2 = 0.70710678f;
            expectVec3Near(ray.origin, glm::vec3(0.0f, 1.0f, -1.0f), "top-edge ray origin");
            expectVec3Near(ray.direction, glm::vec3(0.0f, kInvSqrt2, -kInvSqrt2), "top-edge ray direction");
        }

        // ==== worldPointToScreenPoint(): the inverse of the case above ====
        {
            const std::optional<glm::vec2> screen =
                worldPointToScreenPoint(glm::vec3(1.0f, 0.0f, -1.0f), kViewportSize, kViewportSize, view, projection);
            expectTrue(screen.has_value(), "world point in front of camera projects");
            if (screen.has_value()) {
                expectNear(screen->x, kViewportSize, "projected screen x");
                expectNear(screen->y, kViewportSize / 2.0f, "projected screen y");
            }
        }
        // ==== worldPointToScreenPoint(): the inverse of the top-edge case
        // above -- exercises the same Y-flip convention in the opposite
        // direction (world -> screen rather than screen -> world). An
        // inverted flip here would project this point to screenY =
        // kViewportSize (the bottom), not 0 (the top).
        {
            const std::optional<glm::vec2> screen =
                worldPointToScreenPoint(glm::vec3(0.0f, 1.0f, -1.0f), kViewportSize, kViewportSize, view, projection);
            expectTrue(screen.has_value(), "top-edge world point in front of camera projects");
            if (screen.has_value()) {
                expectNear(screen->x, kViewportSize / 2.0f, "top-edge projected screen x");
                expectNear(screen->y, 0.0f, "top-edge projected screen y");
            }
        }
        // A point behind the camera (positive Z, camera looks down -Z) must
        // not project to a garbage on-screen coordinate -- std::nullopt, the
        // same "no meaningful result, not garbage" contract this function's
        // own header comment documents.
        {
            const std::optional<glm::vec2> screen =
                worldPointToScreenPoint(glm::vec3(0.0f, 0.0f, 5.0f), kViewportSize, kViewportSize, view, projection);
            expectTrue(!screen.has_value(), "world point behind camera does not project");
        }
    }

    // ==== gizmoAxisDirection() ==============================================
    expectVec3Near(gizmoAxisDirection(GizmoAxis::kX), glm::vec3(1, 0, 0), "gizmoAxisDirection(kX)");
    expectVec3Near(gizmoAxisDirection(GizmoAxis::kY), glm::vec3(0, 1, 0), "gizmoAxisDirection(kY)");
    expectVec3Near(gizmoAxisDirection(GizmoAxis::kZ), glm::vec3(0, 0, 1), "gizmoAxisDirection(kZ)");
    expectVec3Near(gizmoAxisDirection(GizmoAxis::kNone), glm::vec3(0, 0, 0), "gizmoAxisDirection(kNone)");

    // ==== gizmoAxisLength() / gizmoPickTolerance() ==========================
    expectNear(gizmoAxisLength(10.0f), 10.0f * kGizmoAxisLengthScale, "gizmoAxisLength(10)");
    // Floored at kGizmoMinAxisLength for a camera at (or very near) the
    // gizmo's own origin -- 0 * scale = 0 would otherwise collapse it.
    expectNear(gizmoAxisLength(0.0f), kGizmoMinAxisLength, "gizmoAxisLength(0) floors");
    expectNear(gizmoPickTolerance(2.0f), 2.0f * kGizmoPickToleranceScale, "gizmoPickTolerance(2)");

    // ==== closestPointsBetweenLines() =======================================
    {
        // Ray along the world X axis; line is the vertical line x=5 (point
        // (5,3,0), direction +Y). By inspection the two lines' true closest
        // approach is at world point (5,0,0): rayT=5 along (1,0,0) from the
        // origin, lineT=-3 along +Y from (5,3,0) (since (5,3,0) + (-3)*(0,1,0)
        // = (5,0,0)).
        const Ray ray{glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)};
        const std::optional<LineClosestPoint> result =
            closestPointsBetweenLines(ray, glm::vec3(5.0f, 3.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        expectTrue(result.has_value(), "perpendicular lines produce a result");
        if (result.has_value()) {
            expectNear(result->rayT, 5.0f, "perpendicular-lines rayT");
            expectNear(result->lineT, -3.0f, "perpendicular-lines lineT");
        }
    }
    {
        // A genuinely NON-perpendicular pair (b = dot(d1,d2) != 0), so the
        // `b*d` cross term inside lineT = (e - b*d) / denom actually
        // contributes -- the perpendicular case above has b=0, which makes
        // that whole term vanish regardless of its sign, so it can't catch
        // a `(e - b*d)` -> `(e + b*d)` sign regression the way this case
        // can. Hand-derived independently of the shipped code (worked out
        // the same way the ray/line formula above is derived, not by
        // reading off whatever gizmo.cpp currently outputs):
        //
        // ray: origin (2,0,0), direction d1=(1,0,0) (unit already).
        // line: point (0,0,0), direction (1,1,0) -> normalized d2 =
        // (1/sqrt2, 1/sqrt2, 0). b = dot(d1,d2) = 1/sqrt2 (a real 45-degree
        // angle between the two lines, not perpendicular).
        // r = ray.origin - linePoint = (2,0,0).
        // d = dot(d1,r) = 2. e = dot(d2,r) = (1/sqrt2)*2 + (1/sqrt2)*0 =
        // sqrt2. denom = 1 - b*b = 1 - 1/2 = 1/2.
        // lineT = (e - b*d) / denom = (sqrt2 - (1/sqrt2)*2) / (1/2)
        //       = (sqrt2 - sqrt2) / (1/2) = 0 / (1/2) = 0.
        // rayT  = (b*e - d) / denom = ((1/sqrt2)*sqrt2 - 2) / (1/2)
        //       = (1 - 2) / (1/2) = -1 / (1/2) = -2.
        // (Geometrically: both lines pass through the world origin -- the
        // ray line is the X axis, the other is the line y=x, z=0 -- so their
        // true closest approach is that shared point, at rayT=-2 along d1
        // from (2,0,0) and lineT=0 along d2 from (0,0,0); both land on
        // (0,0,0), a zero-distance intersection, consistent with the
        // algebra above.)
        //
        // A sign-flipped `(e + b*d)` would instead give lineT =
        // (sqrt2 + sqrt2) / (1/2) = 2*sqrt2 / (1/2) = 4*sqrt2 (~5.657) --
        // wildly different from the correct 0, so this case actually catches
        // that regression where the perpendicular case above cannot.
        const Ray ray{glm::vec3(2.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)};
        const std::optional<LineClosestPoint> result =
            closestPointsBetweenLines(ray, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f));
        expectTrue(result.has_value(), "non-perpendicular lines produce a result");
        if (result.has_value()) {
            expectNear(result->rayT, -2.0f, "non-perpendicular-lines rayT");
            expectNear(result->lineT, 0.0f, "non-perpendicular-lines lineT");
        }
    }
    {
        // Parallel (here, identical-direction, different-magnitude vector)
        // lines -- no unique closest point, must reject rather than divide
        // by the near-zero determinant.
        const Ray ray{glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)};
        const std::optional<LineClosestPoint> result =
            closestPointsBetweenLines(ray, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(2.0f, 0.0f, 0.0f));
        expectTrue(!result.has_value(), "parallel lines reject rather than produce garbage");
    }

    // ==== hitTestGizmoAxes() ================================================
    // gizmoOrigin at the world origin; axisLength 2. A ray shot in +Z aimed
    // squarely at world point (1,0,0) -- on the X axis handle, at axisT=1 --
    // from behind it: origin=(1,0,-5), direction=(0,0,1). By construction
    // this ray passes EXACTLY through (1,0,0), so its true distance to the X
    // axis is 0; its direction (0,0,1) is also exactly parallel to the Z
    // axis's own direction, so the Z axis must be silently skipped (not a
    // hit, not a crash) rather than treated as a coincidental zero-distance
    // hit.
    {
        const Ray ray{glm::vec3(1.0f, 0.0f, -5.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
        const AxisHitTestResult hit = hitTestGizmoAxes(ray, glm::vec3(0.0f), 2.0f, 0.1f);
        expectTrue(hit.axis == GizmoAxis::kX, "ray aimed at X handle hits kX");
        expectNear(hit.axisT, 1.0f, "hit axisT lands at the aimed-at point");
    }
    // The same ray, but with a pick tolerance loose enough to also catch the
    // Y axis (whose true distance from this same ray is exactly 1.0 world
    // unit, hand-derivable the same way): X (distance 0) must still win over
    // Y (distance 1) as the closer of the two.
    {
        const Ray ray{glm::vec3(1.0f, 0.0f, -5.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
        const AxisHitTestResult hit = hitTestGizmoAxes(ray, glm::vec3(0.0f), 2.0f, 1.5f);
        expectTrue(hit.axis == GizmoAxis::kX, "closer axis (X) wins over a farther one within tolerance (Y)");
    }
    // A ray that passes nowhere near any of the three axis handles must
    // report kNone, not an incorrect "closest anyway" hit.
    {
        const Ray ray{glm::vec3(10.0f, 10.0f, -5.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
        const AxisHitTestResult hit = hitTestGizmoAxes(ray, glm::vec3(0.0f), 2.0f, 0.5f);
        expectTrue(hit.axis == GizmoAxis::kNone, "a ray missing every axis handle reports kNone");
    }

    // ==== updateGizmoDrag(): idle -> dragging -> idle, with hand-computed
    // positions at every step ===============================================
    // Every ray below has the form origin=(t, 0, -5), direction=(0,0,1) for
    // varying t -- never parallel to the X or Y axis directions, so
    // closestPointsBetweenLines() against the X axis (entity position
    // (2,0,0), direction (1,0,0)) always succeeds. Hand-derivation: with
    // d1=(0,0,1), d2=(1,0,0), r = ray.origin - linePoint = (t - linePoint.x,
    // 0, -5), b=dot(d1,d2)=0, d=dot(d1,r)=-5, e=dot(d2,r)=t-linePoint.x,
    // denom=1 -- so lineT = e - b*d = t - linePoint.x exactly: the ray's own
    // world-space X coordinate, offset by the line's own anchor X. This is
    // simple enough to verify by hand at every step below.
    {
        const glm::vec3 entityStart(2.0f, 0.0f, 0.0f);
        auto rayAtX = [](float t) { return Ray{glm::vec3(t, 0.0f, -5.0f), glm::vec3(0.0f, 0.0f, 1.0f)}; };

        GizmoDragState state;  // starts at kNone

        // Not dragging, mouse merely hovering (no press) over the X handle:
        // nothing happens.
        {
            const GizmoDragResult r = updateGizmoDrag(state, /*mouseDown=*/false, /*mousePressedThisFrame=*/false,
                                                       GizmoAxis::kX, rayAtX(2.0f), entityStart);
            expectTrue(r.state.axis == GizmoAxis::kNone, "hover alone does not start a drag");
            expectTrue(!r.newPosition.has_value(), "hover alone produces no position update");
            state = r.state;
        }

        // A held button that was NOT a fresh press this frame, even while
        // hovering a handle, must not start a drag either -- the
        // edge-triggering contract this function's own header comment
        // documents.
        {
            const GizmoDragResult r = updateGizmoDrag(state, /*mouseDown=*/true, /*mousePressedThisFrame=*/false,
                                                       GizmoAxis::kX, rayAtX(2.0f), entityStart);
            expectTrue(r.state.axis == GizmoAxis::kNone, "a held-but-not-just-pressed button does not start a drag");
            state = r.state;
        }

        // Frame 1: a fresh press, aimed exactly at the entity's own current
        // X (t=2 -> lineT = 2 - 2 = 0) -- grabs the X handle. This frame
        // only anchors the drag; it must not move the entity yet.
        {
            const GizmoDragResult r = updateGizmoDrag(state, /*mouseDown=*/true, /*mousePressedThisFrame=*/true,
                                                       GizmoAxis::kX, rayAtX(2.0f), entityStart);
            expectTrue(r.state.axis == GizmoAxis::kX, "a fresh press on the X handle starts dragging kX");
            expectNear(r.state.startAxisT, 0.0f, "grab frame's startAxisT");
            expectVec3Near(r.state.startEntityPosition, entityStart, "grab frame's startEntityPosition");
            expectTrue(!r.newPosition.has_value(), "the grab frame itself produces no position update");
            state = r.state;
        }

        // Frame 2: still held, mouse moved to t=5 -> lineT = 5-2 = 3 ->
        // delta = 3 - startAxisT(0) = 3 -> newPosition = (2,0,0) +
        // (1,0,0)*3 = (5,0,0).
        {
            const GizmoDragResult r = updateGizmoDrag(state, /*mouseDown=*/true, /*mousePressedThisFrame=*/false,
                                                       GizmoAxis::kX, rayAtX(5.0f), entityStart);
            expectTrue(r.state.axis == GizmoAxis::kX, "still dragging kX on frame 2");
            expectTrue(r.newPosition.has_value(), "frame 2 produces a position update");
            if (r.newPosition.has_value()) {
                expectVec3Near(*r.newPosition, glm::vec3(5.0f, 0.0f, 0.0f), "frame 2 position");
            }
            state = r.state;  // anchor is unchanged frame to frame while dragging
        }

        // Frame 3: mouse moved further, to t=7 -> lineT=5 -> delta=5 ->
        // newPosition = (2,0,0) + (1,0,0)*5 = (7,0,0). The anchor
        // (startEntityPosition/startAxisT) must still read exactly as it did
        // at the grab frame -- confirms the anchor is never silently
        // re-seeded from the entity's own (already-moved) current position.
        {
            expectVec3Near(state.startEntityPosition, entityStart, "anchor unchanged by frame 2's own movement");
            expectNear(state.startAxisT, 0.0f, "startAxisT unchanged by frame 2's own movement");
            const GizmoDragResult r = updateGizmoDrag(state, /*mouseDown=*/true, /*mousePressedThisFrame=*/false,
                                                       GizmoAxis::kX, rayAtX(7.0f), entityStart);
            expectTrue(r.newPosition.has_value(), "frame 3 produces a position update");
            if (r.newPosition.has_value()) {
                expectVec3Near(*r.newPosition, glm::vec3(7.0f, 0.0f, 0.0f), "frame 3 position");
            }
            state = r.state;
        }

        // Frame 4: button released -- back to kNone, no further movement.
        {
            const GizmoDragResult r = updateGizmoDrag(state, /*mouseDown=*/false, /*mousePressedThisFrame=*/false,
                                                       GizmoAxis::kX, rayAtX(7.0f), entityStart);
            expectTrue(r.state.axis == GizmoAxis::kNone, "releasing the button returns to kNone");
            expectTrue(!r.newPosition.has_value(), "the release frame produces no position update");
            state = r.state;
        }

        // Frame 5: button pressed again, but NOT hovering any handle this
        // time -- must not start a new drag.
        {
            const GizmoDragResult r = updateGizmoDrag(state, /*mouseDown=*/true, /*mousePressedThisFrame=*/true,
                                                       GizmoAxis::kNone, rayAtX(7.0f), entityStart);
            expectTrue(r.state.axis == GizmoAxis::kNone, "a fresh press with no handle hovered starts nothing");
        }
    }

    // A grab attempted with a ray exactly parallel to the axis being grabbed
    // (looking straight down the axis) must be declined, not started with a
    // meaningless anchor.
    {
        const Ray ray{glm::vec3(0.0f, 0.0f, -5.0f), glm::vec3(1.0f, 0.0f, 0.0f)};  // parallel to X
        const GizmoDragState idle;
        const GizmoDragResult r = updateGizmoDrag(idle, /*mouseDown=*/true, /*mousePressedThisFrame=*/true,
                                                   GizmoAxis::kX, ray, glm::vec3(0.0f));
        expectTrue(r.state.axis == GizmoAxis::kNone, "a grab with a ray parallel to the axis is declined");
    }

    if (failures == 0) {
        std::printf("gizmo_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "gizmo_test: %d check(s) failed\n", failures);
    return 1;
}
