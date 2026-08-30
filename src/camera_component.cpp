#include "engine/camera_component.hpp"

// Phase 18g: camera_component.hpp's own two new pure functions -- see that
// header's own comments for the full design of each. Both are deliberately
// tiny (a handful of lines each); this .cpp exists at all only because
// resolveActiveCamera() needs EntityRegistry::each<CameraComponent>(), a
// template member function whose full definition only ecs.hpp (already
// #included by camera_component.hpp, see that header's own updated comment)
// provides -- there is no other reason this pair couldn't have stayed
// header-only `inline` functions the way CameraComponent itself remained
// data-only with no .cpp at all through Phase 15c-18e.

namespace engine {

CameraWorldPose resolveCameraWorldPose(const glm::mat4& worldMatrix) {
    const glm::vec3 position(worldMatrix[3]);
    // The upper-left 3x3 of a TRS world matrix is rotation * diag(scale) --
    // see this function's own header comment for why normalizing the
    // rotated forward vector AFTER this multiply (rather than using
    // glm::mat3(worldMatrix) directly) is what keeps this correct under a
    // non-uniform ancestor scale, not merely a stylistic preference.
    const glm::mat3 rotationScale(worldMatrix);
    const glm::vec3 forward = glm::normalize(rotationScale * glm::vec3(0.0f, 0.0f, -1.0f));
    return CameraWorldPose{position, position + forward};
}

ActiveCameraResolution resolveActiveCamera(EntityRegistry& registry) {
    ActiveCameraResolution result;
    // EntityRegistry::each() visits CameraComponent's own pool in a fixed,
    // repeatable dense-storage order (ecs.hpp's own each() comment) -- the
    // first entity this loop ever sees becomes `active`; every one after it
    // is counted into `ignoredCount` rather than silently overwriting
    // `active` -- see this function's own header comment for why "first
    // found," not "last found," and why this whole defensive path exists at
    // all given the Create menu's own UI-level one-Camera-entity limit.
    registry.each<CameraComponent>([&](EntityId id, CameraComponent&) {
        if (!result.active.valid()) {
            result.active = id;
        } else {
            ++result.ignoredCount;
        }
    });
    return result;
}

}  // namespace engine
