// Phase 18e: see gizmo.hpp's own header comment for the full design. This
// translation unit depends on nothing beyond GLM -- no ecs.hpp, no GLFW/GL/
// ImGui at all -- the same minimal-dependency shape camera_capture.cpp/
// window_chrome.cpp already establish for the identical reason:
// tests/gizmo_test.cpp links this file alone.

#include "engine/gizmo.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine {

namespace {

// Below this, two directions are considered "too parallel to trust" -- see
// closestPointsBetweenLines()'s own header comment for exactly what
// degenerates when this threshold is crossed. 1e-6 leaves a wide practical
// margin: it only actually triggers within a small fraction of a degree of
// true parallel, far tighter than any gizmo-picking gesture a real mouse
// could produce by accident, while still being comfortably larger than
// ordinary floating-point rounding noise in dot()'s own result for two
// genuinely non-parallel unit vectors.
constexpr float kParallelEpsilon = 1e-6f;

// worldPointToScreenPoint()'s own "behind the camera" guard threshold -- see
// that function's header comment for why a near-zero/negative clip-space w
// has to be rejected rather than divided through.
constexpr float kClipWEpsilon = 1e-5f;

}  // namespace

Ray screenPointToWorldRay(float screenX, float screenY, float viewportWidth, float viewportHeight,
                           const glm::mat4& view, const glm::mat4& projection) {
    // Pixel coords (origin top-left, y down) -> NDC (origin center, y up,
    // both axes in [-1, 1]) -- the standard screen-to-NDC conversion, y
    // flipped because screen space counts down while NDC counts up.
    const float ndcX = (2.0f * screenX / viewportWidth) - 1.0f;
    const float ndcY = 1.0f - (2.0f * screenY / viewportHeight);

    // Unproject two points on the same screen pixel at opposite ends of the
    // view frustum's own depth range (OpenGL's default NDC z in [-1, 1] --
    // this project defines no GLM_FORCE_DEPTH_ZERO_TO_ONE anywhere, and
    // Camera::getProjectionMatrix() builds its matrix via plain
    // glm::perspective(), so this is the correct convention to unproject
    // against) and subtract to get a direction -- the standard "unproject
    // near and far, subtract" screen-ray technique.
    const glm::mat4 invViewProjection = glm::inverse(projection * view);

    glm::vec4 nearWorld = invViewProjection * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farWorld = invViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    nearWorld /= nearWorld.w;
    farWorld /= farWorld.w;

    Ray ray;
    ray.origin = glm::vec3(nearWorld);
    ray.direction = glm::normalize(glm::vec3(farWorld) - glm::vec3(nearWorld));
    return ray;
}

std::optional<glm::vec2> worldPointToScreenPoint(const glm::vec3& worldPoint, float viewportWidth,
                                                  float viewportHeight, const glm::mat4& view,
                                                  const glm::mat4& projection) {
    const glm::vec4 clip = projection * view * glm::vec4(worldPoint, 1.0f);
    if (clip.w <= kClipWEpsilon) {
        // Behind (or exactly at) the camera -- see this function's own
        // header comment for why this is rejected rather than divided
        // through.
        return std::nullopt;
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    const float screenX = (ndc.x * 0.5f + 0.5f) * viewportWidth;
    const float screenY = (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportHeight;
    return glm::vec2(screenX, screenY);
}

glm::vec3 gizmoAxisDirection(GizmoAxis axis) {
    switch (axis) {
        case GizmoAxis::kX:
            return glm::vec3(1.0f, 0.0f, 0.0f);
        case GizmoAxis::kY:
            return glm::vec3(0.0f, 1.0f, 0.0f);
        case GizmoAxis::kZ:
            return glm::vec3(0.0f, 0.0f, 1.0f);
        case GizmoAxis::kNone:
        default:
            return glm::vec3(0.0f);
    }
}

float gizmoAxisLength(float distanceToCamera) {
    return std::max(distanceToCamera * kGizmoAxisLengthScale, kGizmoMinAxisLength);
}

float gizmoPickTolerance(float axisLength) {
    return axisLength * kGizmoPickToleranceScale;
}

std::optional<LineClosestPoint> closestPointsBetweenLines(const Ray& ray, const glm::vec3& linePoint,
                                                           const glm::vec3& lineDirection) {
    const glm::vec3 d1 = ray.direction;  // assumed already unit length
    const glm::vec3 d2 = glm::normalize(lineDirection);
    const glm::vec3 r = ray.origin - linePoint;

    // Standard closest-point-between-two-lines derivation (minimizing
    // |(ray.origin + t1*d1) - (linePoint + t2*d2)|^2 over t1/t2): with a =
    // dot(d1,d1) = 1 and c = dot(d2,d2) = 1 (both unit vectors), the 2x2
    // linear system's determinant reduces to (1 - b*b), b = dot(d1,d2).
    const float b = glm::dot(d1, d2);
    const float d = glm::dot(d1, r);
    const float e = glm::dot(d2, r);
    const float denom = 1.0f - b * b;

    if (std::fabs(denom) < kParallelEpsilon) {
        // Nearly parallel -- see this function's own header comment. Reject
        // rather than divide by (near) zero, which would otherwise produce
        // an enormous or NaN/Inf t1/t2.
        return std::nullopt;
    }

    LineClosestPoint result;
    result.rayT = (b * e - d) / denom;
    result.lineT = (e - b * d) / denom;
    return result;
}

AxisHitTestResult hitTestGizmoAxes(const Ray& ray, const glm::vec3& gizmoOrigin, float axisLength,
                                    float pickToleranceWorld) {
    AxisHitTestResult best;
    float bestDistance = std::numeric_limits<float>::max();

    const GizmoAxis axes[3] = {GizmoAxis::kX, GizmoAxis::kY, GizmoAxis::kZ};
    for (GizmoAxis axis : axes) {
        const glm::vec3 direction = gizmoAxisDirection(axis);
        const std::optional<LineClosestPoint> closest = closestPointsBetweenLines(ray, gizmoOrigin, direction);
        if (!closest.has_value()) {
            continue;
        }

        // Clamp to the axis handle's own finite, visible extent -- a hit
        // "behind" the gizmo's own origin or past its drawn tip shouldn't
        // register, even if the infinite line's own closest approach lands
        // there.
        const float clampedAxisT = std::clamp(closest->lineT, 0.0f, axisLength);
        const glm::vec3 axisPoint = gizmoOrigin + direction * clampedAxisT;

        // Distance from that (now-clamped) point on the axis segment to the
        // closest point ON THE RAY (not the infinite line -- the ray only
        // extends forward from the camera, so its own parametric t is
        // clamped to >= 0 here too) -- a simple, robust point-to-ray
        // distance, recomputed fresh against the clamped axis point rather
        // than reusing `closest->rayT` (which was only valid for the
        // UNCLAMPED line and may no longer correspond to the true closest
        // ray point once lineT was clamped).
        float rayParam = glm::dot(axisPoint - ray.origin, ray.direction);
        rayParam = std::max(rayParam, 0.0f);
        const glm::vec3 rayPoint = ray.origin + ray.direction * rayParam;

        const float distance = glm::length(rayPoint - axisPoint);
        if (distance < bestDistance) {
            bestDistance = distance;
            best.axis = axis;
            best.axisT = clampedAxisT;
        }
    }

    if (bestDistance > pickToleranceWorld) {
        return AxisHitTestResult{};
    }
    return best;
}

// Phase 18j: intersectRayWithPlane() below reuses this file's own
// kParallelEpsilon (defined once, at file scope, above) rather than
// defining a second copy -- both closestPointsBetweenLines()'s own
// near-parallel-lines case and this function's own near-parallel-to-plane
// case are the identical underlying degeneracy ("the angle between two unit
// directions is near zero"), so the same threshold applies to both.
std::optional<RayPlaneHit> intersectRayWithPlane(const Ray& ray, const glm::vec3& planePoint,
                                                  const glm::vec3& planeNormal) {
    const glm::vec3 normal = glm::normalize(planeNormal);
    const float denom = glm::dot(normal, ray.direction);
    if (std::fabs(denom) < kParallelEpsilon) {
        // Ray nearly parallel to the plane itself (grazing angle) -- see
        // this function's own header comment.
        return std::nullopt;
    }
    const float t = glm::dot(planePoint - ray.origin, normal) / denom;
    if (t < 0.0f) {
        // Behind the ray's own origin -- not a usable result for a mouse
        // ray cast forward from the camera. See this function's own header
        // comment.
        return std::nullopt;
    }
    RayPlaneHit hit;
    hit.t = t;
    hit.point = ray.origin + ray.direction * t;
    return hit;
}

RingPlaneBasis gizmoRingPlaneBasis(GizmoAxis axis) {
    switch (axis) {
        case GizmoAxis::kX:
            return RingPlaneBasis{glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
        case GizmoAxis::kY:
            return RingPlaneBasis{glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f)};
        case GizmoAxis::kZ:
            return RingPlaneBasis{glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)};
        case GizmoAxis::kNone:
        default:
            return RingPlaneBasis{};
    }
}

float gizmoRingAngle(const glm::vec3& point, const glm::vec3& gizmoOrigin, GizmoAxis axis) {
    const RingPlaneBasis basis = gizmoRingPlaneBasis(axis);
    const glm::vec3 offset = point - gizmoOrigin;
    return std::atan2(glm::dot(offset, basis.v), glm::dot(offset, basis.u));
}

RingHitTestResult hitTestGizmoRings(const Ray& ray, const glm::vec3& gizmoOrigin, float ringRadius,
                                     float pickToleranceWorld) {
    RingHitTestResult best;
    float bestDistance = std::numeric_limits<float>::max();

    const GizmoAxis axes[3] = {GizmoAxis::kX, GizmoAxis::kY, GizmoAxis::kZ};
    for (GizmoAxis axis : axes) {
        const glm::vec3 normal = gizmoAxisDirection(axis);
        const std::optional<RayPlaneHit> hit = intersectRayWithPlane(ray, gizmoOrigin, normal);
        if (!hit.has_value()) {
            continue;
        }
        // See this function's own header comment: since `hit->point` lies
        // exactly in the ring's own plane, this radial difference IS the
        // exact closest distance from that point to the circle -- not an
        // approximation.
        const float radiusAtHit = glm::length(hit->point - gizmoOrigin);
        const float distance = std::fabs(radiusAtHit - ringRadius);
        if (distance < bestDistance) {
            bestDistance = distance;
            best.axis = axis;
        }
    }

    if (bestDistance > pickToleranceWorld) {
        return RingHitTestResult{};
    }
    return best;
}

GizmoRotateDragResult updateGizmoRotateDrag(const GizmoRotateDragState& current, bool mouseDown,
                                             bool mousePressedThisFrame, GizmoAxis hoverAxis, const Ray& ray,
                                             const glm::vec3& gizmoOrigin) {
    GizmoRotateDragResult result;

    if (current.axis == GizmoAxis::kNone) {
        // Not currently dragging -- only a fresh press directly on a ring
        // starts one (the identical edge-triggering contract
        // updateGizmoDrag() already documents).
        if (mousePressedThisFrame && hoverAxis != GizmoAxis::kNone) {
            const std::optional<RayPlaneHit> hit =
                intersectRayWithPlane(ray, gizmoOrigin, gizmoAxisDirection(hoverAxis));
            if (hit.has_value()) {
                result.state.axis = hoverAxis;
                result.state.lastAngle = gizmoRingAngle(hit->point, gizmoOrigin, hoverAxis);
            }
            // else: ray grazing the ring's own plane -- decline the grab,
            // result.state stays default-constructed (kNone).
        }
        // else: result.state stays default-constructed (kNone).
        return result;
    }

    // Currently dragging.
    if (!mouseDown) {
        // Released -- back to kNone (default-constructed), no further
        // rotation this frame.
        return result;
    }

    // Still held -- re-intersect the SAME ring's plane (through
    // `gizmoOrigin`, perpendicular to current.axis -- see this function's
    // own header comment for why this is `gizmoOrigin` fresh every frame,
    // not a frozen grab-time anchor the way the translate gizmo's own axis
    // line has to be).
    result.state = current;
    const std::optional<RayPlaneHit> hit =
        intersectRayWithPlane(ray, gizmoOrigin, gizmoAxisDirection(current.axis));
    if (hit.has_value()) {
        const float angle = gizmoRingAngle(hit->point, gizmoOrigin, current.axis);
        // Shortest signed angle from current.lastAngle to `angle`, wrapped
        // into (-pi, pi] -- a plain subtraction would report a huge
        // wrong-signed jump for a drag that crosses the atan2 ±pi seam.
        const float rawDelta = angle - current.lastAngle;
        const float wrappedDelta = std::atan2(std::sin(rawDelta), std::cos(rawDelta));
        result.deltaAngleDeg = glm::degrees(wrappedDelta);
        result.state.lastAngle = angle;
    }
    // else: briefly grazing this one frame -- result.deltaAngleDeg stays
    // std::nullopt, state.lastAngle is unchanged, so tracking resumes
    // cleanly from the same last-known angle the next non-degenerate frame.
    return result;
}

namespace {

// Phase 18k: helper used only by updateGizmoScaleDrag() below -- replaces
// just the `axis` component of `scale` with `value`, leaving the other two
// components untouched. A plain switch (not e.g. `scale[axisIndex] = value`
// via some GizmoAxis->int mapping) to match this file's own established
// per-axis-enum idiom (gizmoAxisDirection()/gizmoRingPlaneBasis() above both
// switch on GizmoAxis directly rather than indexing through an integer
// conversion). kNone leaves `scale` completely unchanged -- never meant to
// be reached (updateGizmoScaleDrag() only ever calls this with a real grabbed
// axis), but a well-defined no-op rather than undefined behavior, the same
// "no unreachable/undefined case for the enum's own default member" instinct
// gizmoAxisDirection() itself already documents.
glm::vec3 withAxisComponent(const glm::vec3& scale, GizmoAxis axis, float value) {
    glm::vec3 result = scale;
    switch (axis) {
        case GizmoAxis::kX:
            result.x = value;
            break;
        case GizmoAxis::kY:
            result.y = value;
            break;
        case GizmoAxis::kZ:
            result.z = value;
            break;
        case GizmoAxis::kNone:
        default:
            break;
    }
    return result;
}

float axisComponent(const glm::vec3& v, GizmoAxis axis) {
    switch (axis) {
        case GizmoAxis::kX:
            return v.x;
        case GizmoAxis::kY:
            return v.y;
        case GizmoAxis::kZ:
            return v.z;
        case GizmoAxis::kNone:
        default:
            return 0.0f;
    }
}

}  // namespace

GizmoScaleDragResult updateGizmoScaleDrag(const GizmoScaleDragState& current, bool mouseDown,
                                           bool mousePressedThisFrame, GizmoAxis hoverAxis, const Ray& ray,
                                           const glm::vec3& gizmoOrigin, const glm::vec3& entityScale) {
    GizmoScaleDragResult result;

    if (current.axis == GizmoAxis::kNone) {
        // Not currently dragging -- only a fresh press directly on a handle
        // starts one (the identical edge-triggering contract
        // updateGizmoDrag() already documents).
        if (mousePressedThisFrame && hoverAxis != GizmoAxis::kNone) {
            const glm::vec3 direction = gizmoAxisDirection(hoverAxis);
            const std::optional<LineClosestPoint> closest = closestPointsBetweenLines(ray, gizmoOrigin, direction);
            if (closest.has_value() && std::fabs(closest->lineT) >= kGizmoScaleMinGrabDistance) {
                result.state.axis = hoverAxis;
                result.state.startAxisT = closest->lineT;
                result.state.startEntityScale = entityScale;
                result.state.startGizmoOrigin = gizmoOrigin;
            }
            // else: ray nearly parallel to the axis being grabbed, or the
            // grab landed too close to the gizmo's own origin to divide by
            // (kGizmoScaleMinGrabDistance's own comment) -- decline the
            // grab, result.state stays default-constructed (kNone).
        }
        // else: result.state stays default-constructed (kNone).
        return result;
    }

    // Currently dragging.
    if (!mouseDown) {
        // Released -- back to kNone (default-constructed), no further
        // scaling this frame.
        return result;
    }

    // Still held -- re-project onto the SAME fixed axis line this drag
    // grabbed (anchored at current.startGizmoOrigin, never re-read from
    // `gizmoOrigin` -- see GizmoScaleDragState's own header comment).
    result.state = current;
    const glm::vec3 direction = gizmoAxisDirection(current.axis);
    const std::optional<LineClosestPoint> closest =
        closestPointsBetweenLines(ray, current.startGizmoOrigin, direction);
    if (closest.has_value()) {
        // startAxisT is guaranteed non-near-zero by the grab-time floor
        // above (kGizmoScaleMinGrabDistance), so this division is always
        // well-conditioned for the lifetime of a single drag.
        const float ratio = closest->lineT / current.startAxisT;
        const float rawComponent = axisComponent(current.startEntityScale, current.axis) * ratio;
        const float clampedComponent = std::max(rawComponent, kGizmoMinScaleComponent);
        result.newScale = withAxisComponent(current.startEntityScale, current.axis, clampedComponent);
    }
    // else: briefly parallel this one frame -- result.newScale stays
    // std::nullopt, state (the anchor) is unchanged, so tracking resumes
    // cleanly from the same anchor the next non-degenerate frame.
    return result;
}

GizmoDragResult updateGizmoDrag(const GizmoDragState& current, bool mouseDown, bool mousePressedThisFrame,
                                 GizmoAxis hoverAxis, const Ray& ray, const glm::vec3& entityPosition) {
    GizmoDragResult result;

    if (current.axis == GizmoAxis::kNone) {
        // Not currently dragging -- only a fresh press (edge-triggered, see
        // this function's own header comment on why) directly on a handle
        // starts one.
        if (mousePressedThisFrame && hoverAxis != GizmoAxis::kNone) {
            const glm::vec3 direction = gizmoAxisDirection(hoverAxis);
            const std::optional<LineClosestPoint> closest = closestPointsBetweenLines(ray, entityPosition, direction);
            if (closest.has_value()) {
                result.state.axis = hoverAxis;
                result.state.startAxisT = closest->lineT;
                result.state.startEntityPosition = entityPosition;
            }
            // else: ray nearly parallel to the axis being grabbed -- decline
            // the grab, result.state stays default-constructed (kNone).
        }
        // else: result.state stays default-constructed (kNone).
        return result;
    }

    // Currently dragging.
    if (!mouseDown) {
        // Released -- back to kNone (default-constructed), no further
        // movement this frame.
        return result;
    }

    // Still held -- re-project onto the SAME fixed axis line this drag
    // grabbed (anchored at current.startEntityPosition, never re-read from
    // `entityPosition` -- see GizmoDragState's own header comment).
    result.state = current;
    const glm::vec3 direction = gizmoAxisDirection(current.axis);
    const std::optional<LineClosestPoint> closest =
        closestPointsBetweenLines(ray, current.startEntityPosition, direction);
    if (closest.has_value()) {
        const float delta = closest->lineT - current.startAxisT;
        result.newPosition = current.startEntityPosition + direction * delta;
    }
    // else: briefly parallel this one frame -- result.newPosition stays
    // std::nullopt, state (the anchor) is unchanged, so tracking resumes
    // cleanly from the same anchor the next non-degenerate frame.
    return result;
}

}  // namespace engine
