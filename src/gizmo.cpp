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
