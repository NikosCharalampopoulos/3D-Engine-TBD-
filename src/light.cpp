// Phase 15: see light.hpp's own header comment for the full design. This
// translation unit depends on nothing but ecs.hpp/transform.hpp -- no
// GL/Window dependency at all, and (as of this file's second review-pass
// fix) no log.hpp either, since collectPointLights() no longer logs
// anything itself -- see its own comment below for why that moved to
// Application (application.cpp/application.hpp). Same "pure logic, its own
// small function" shape physics.cpp/transform_hierarchy.cpp already
// establish -- so tests/light_test.cpp can exercise collectPointLights()
// with no live GL context or GPU either.

#include "engine/light.hpp"

#include "engine/ecs.hpp"
#include "engine/transform.hpp"

namespace engine {

bool collectPointLights(EntityRegistry& registry, std::size_t maxTotal, std::vector<PointLightSample>& out) {
    // Deliberately no logging and no static/persistent state in here -- see
    // light.hpp's own comment on why: this function stays exactly the "pure
    // logic, no side effects beyond `out`" shape this file's own header
    // comment (and tests/light_test.cpp, run against a fresh EntityRegistry
    // per test case) already commit it to. Whether an overflow is worth a
    // LOG_WARN -- and whether THIS call's overflow is new or a continuation
    // of an already-warned-about one -- depends on state that outlives any
    // single call (the previous call's own result), which only a caller
    // that owns a persistent per-scene lifetime can correctly track. See
    // Application::render()'s own call site (application.cpp) and
    // pointLightOverflowActive_ (application.hpp) for where that now lives.
    bool overflowed = false;
    registry.each<PointLight>([&](EntityId id, PointLight& light) {
        const Transform* transform = registry.getComponent<Transform>(id);
        if (transform == nullptr) {
            return;
        }
        if (out.size() >= maxTotal) {
            overflowed = true;
            return;
        }
        out.push_back(
            PointLightSample{transform->position(), light.color, light.constant, light.linear, light.quadratic});
    });
    return overflowed;
}

}  // namespace engine
