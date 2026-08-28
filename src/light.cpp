// Phase 15: see light.hpp's own header comment for the full design. This
// translation unit depends on nothing but ecs.hpp/transform.hpp/log.hpp --
// no GL/Window dependency at all, the same "pure logic, its own small
// function" shape physics.cpp/transform_hierarchy.cpp already establish --
// so tests/light_test.cpp can exercise collectPointLights() with no live GL
// context or GPU either.

#include "engine/light.hpp"

#include "engine/ecs.hpp"
#include "engine/log.hpp"
#include "engine/transform.hpp"

namespace engine {

void collectPointLights(EntityRegistry& registry, std::size_t maxTotal, std::vector<PointLightSample>& out) {
    bool loggedOverflow = false;
    registry.each<PointLight>([&](EntityId id, PointLight& light) {
        const Transform* transform = registry.getComponent<Transform>(id);
        if (transform == nullptr) {
            return;
        }
        if (out.size() >= maxTotal) {
            if (!loggedOverflow) {
                LOG_WARN("collectPointLights: more point lights exist than maxTotal (" + std::to_string(maxTotal) +
                          ") supports; extras are not rendered");
                loggedOverflow = true;
            }
            return;
        }
        out.push_back(
            PointLightSample{transform->position(), light.color, light.constant, light.linear, light.quadratic});
    });
}

}  // namespace engine
