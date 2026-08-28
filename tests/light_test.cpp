// Phase 15's own test: exercises engine::collectPointLights() (src/light.cpp)
// in isolation -- same "plain executable, links only the pure logic file
// it's testing" shape as physics_test.cpp (see that file's own header
// comment). light.cpp depends only on ecs.hpp/transform.hpp/log.hpp, none of
// which touch GL/Window at all, so this needs no live window/GL context/GPU
// either.

#include "engine/light.hpp"

#include "engine/ecs.hpp"
#include "engine/transform.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expectTrue(bool condition, const std::string& what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

void expectNear(float actual, float expected, const std::string& what, float epsilon = 1e-4f) {
    expectTrue(glm::epsilonEqual(actual, expected, epsilon), what + " (expected " + std::to_string(expected) +
                                                                   ", got " + std::to_string(actual) + ")");
}

}  // namespace

int main() {
    // --- An entity with both Transform and PointLight is collected, at its
    // Transform's own position ------------------------------------------------
    {
        engine::EntityRegistry registry;
        const engine::EntityId id = registry.create();
        registry.addComponent<engine::Transform>(id).setPosition(glm::vec3(1.0f, 2.0f, 3.0f));
        registry.addComponent<engine::PointLight>(
            id, engine::PointLight{glm::vec3(0.2f, 0.4f, 0.6f), 1.0f, 0.35f, 0.44f});

        std::vector<engine::PointLightSample> samples;
        engine::collectPointLights(registry, /*maxTotal=*/8, samples);

        expectTrue(samples.size() == 1, "one PointLight entity collects exactly one sample");
        if (samples.size() == 1) {
            expectNear(samples[0].position.x, 1.0f, "sample position.x matches the entity's Transform");
            expectNear(samples[0].position.y, 2.0f, "sample position.y matches the entity's Transform");
            expectNear(samples[0].position.z, 3.0f, "sample position.z matches the entity's Transform");
            expectNear(samples[0].color.r, 0.2f, "sample color.r matches the PointLight component");
            expectNear(samples[0].constant, 1.0f, "sample constant matches the PointLight component");
            expectNear(samples[0].linear, 0.35f, "sample linear matches the PointLight component");
            expectNear(samples[0].quadratic, 0.44f, "sample quadratic matches the PointLight component");
        }
    }

    // --- Appends to a non-empty `out`, doesn't overwrite it -----------------
    // (this is how Application::render() seeds `out` with its own fixed
    // kPointLights table before calling collectPointLights() -- see
    // light.hpp's own comment on why this is an append, not an overwrite.)
    {
        engine::EntityRegistry registry;
        const engine::EntityId id = registry.create();
        registry.addComponent<engine::Transform>(id).setPosition(glm::vec3(0.0f));
        registry.addComponent<engine::PointLight>(id, engine::PointLight{});

        std::vector<engine::PointLightSample> samples;
        samples.push_back(engine::PointLightSample{glm::vec3(9.0f), glm::vec3(1.0f), 1.0f, 0.7f, 1.8f});
        engine::collectPointLights(registry, /*maxTotal=*/8, samples);

        expectTrue(samples.size() == 2, "collectPointLights appends, leaving a pre-seeded entry in place");
        if (samples.size() == 2) {
            expectNear(samples[0].position.x, 9.0f, "the pre-seeded entry is untouched");
        }
    }

    // --- A PointLight with no Transform contributes nothing (the same
    // "opt-in, no implicit pairing" tolerance ecs.hpp documents elsewhere) --
    {
        engine::EntityRegistry registry;
        const engine::EntityId id = registry.create();
        registry.addComponent<engine::PointLight>(id, engine::PointLight{});

        std::vector<engine::PointLightSample> samples;
        engine::collectPointLights(registry, /*maxTotal=*/8, samples);

        expectTrue(samples.empty(), "a PointLight with no Transform is skipped, not a crash");
    }

    // --- An entity with a Transform but no PointLight contributes nothing --
    {
        engine::EntityRegistry registry;
        const engine::EntityId id = registry.create();
        registry.addComponent<engine::Transform>(id).setPosition(glm::vec3(5.0f));

        std::vector<engine::PointLightSample> samples;
        engine::collectPointLights(registry, /*maxTotal=*/8, samples);

        expectTrue(samples.empty(), "an entity with only a Transform contributes no PointLightSample");
    }

    // --- maxTotal caps the total, extras silently skipped (not a crash, not
    // an out-of-bounds append past what the caller asked for) ---------------
    {
        engine::EntityRegistry registry;
        for (int i = 0; i < 5; ++i) {
            const engine::EntityId id = registry.create();
            registry.addComponent<engine::Transform>(id).setPosition(glm::vec3(static_cast<float>(i), 0.0f, 0.0f));
            registry.addComponent<engine::PointLight>(id, engine::PointLight{});
        }

        std::vector<engine::PointLightSample> samples;
        engine::collectPointLights(registry, /*maxTotal=*/3, samples);

        expectTrue(samples.size() == 3, "collectPointLights stops appending once maxTotal is reached");
    }

    // --- maxTotal already reached by the pre-seeded entries: nothing new is
    // appended at all ---------------------------------------------------------
    {
        engine::EntityRegistry registry;
        const engine::EntityId id = registry.create();
        registry.addComponent<engine::Transform>(id).setPosition(glm::vec3(0.0f));
        registry.addComponent<engine::PointLight>(id, engine::PointLight{});

        std::vector<engine::PointLightSample> samples(2, engine::PointLightSample{});
        engine::collectPointLights(registry, /*maxTotal=*/2, samples);

        expectTrue(samples.size() == 2, "maxTotal already met by pre-seeded entries: nothing appended");
    }

    if (failures == 0) {
        std::printf("light_test: all checks passed\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "light_test: %d check(s) failed\n", failures);
    return EXIT_FAILURE;
}
