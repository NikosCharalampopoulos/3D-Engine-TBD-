// Phase 8d's own test: exercises InputActionMap in isolation -- default
// bindings match this engine's pre-Phase-8d hardcoded behavior, a
// multi-key action (MoveUp: Space or E) is down if EITHER key is down,
// level-triggered isDown() stays true every poll a key is held, and
// edge-triggered justPressed() fires exactly once across repeated polls
// with the key held down throughout. Links against input_action_map.cpp
// ALONE (see that file's own header comment on why it has no Window/GL
// dependency), so -- like Phase 8b's scene_serialization_test -- this
// needs no window, no GL context, and no GPU; a fake key source (a plain
// std::vector<int> of "these keys are down this poll") stands in for
// Window::isKeyPressed().

#include "engine/input_action_map.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
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

// A fake `isKeyDown` query: reports true for exactly the keys listed in
// `down`, standing in for Window::isKeyPressed() without any live GLFW
// window/context.
engine::KeyDownQuery fakeKeys(std::vector<int> down) {
    return [down = std::move(down)](int key) { return std::find(down.begin(), down.end(), key) != down.end(); };
}

}  // namespace

int main() {
    using engine::InputAction;
    using engine::InputActionMap;

    // --- Defaults match this engine's pre-Phase-8d hardcoded behavior ---
    {
        InputActionMap map;
        expectTrue(map.bindingsFor(InputAction::MoveForward) == std::vector<int>{GLFW_KEY_W},
                   "default MoveForward -> W");
        expectTrue(map.bindingsFor(InputAction::MoveBackward) == std::vector<int>{GLFW_KEY_S},
                   "default MoveBackward -> S");
        expectTrue(map.bindingsFor(InputAction::MoveLeft) == std::vector<int>{GLFW_KEY_A}, "default MoveLeft -> A");
        expectTrue(map.bindingsFor(InputAction::MoveRight) == std::vector<int>{GLFW_KEY_D}, "default MoveRight -> D");
        expectTrue((map.bindingsFor(InputAction::MoveUp) == std::vector<int>{GLFW_KEY_SPACE, GLFW_KEY_E}),
                   "default MoveUp -> Space, E");
        expectTrue((map.bindingsFor(InputAction::MoveDown) == std::vector<int>{GLFW_KEY_LEFT_SHIFT, GLFW_KEY_Q}),
                   "default MoveDown -> LeftShift, Q");
        expectTrue(map.bindingsFor(InputAction::Quit) == std::vector<int>{GLFW_KEY_ESCAPE}, "default Quit -> Escape");
        expectTrue(map.bindingsFor(InputAction::ToggleDebugUI) == std::vector<int>{GLFW_KEY_F1},
                   "default ToggleDebugUI -> F1");
    }

    // --- Pressing the bound key sets the action down (level-triggered) --
    {
        InputActionMap map;
        map.update(fakeKeys({GLFW_KEY_W}));
        expectTrue(map.isDown(InputAction::MoveForward), "MoveForward down when W is held");
        expectTrue(!map.isDown(InputAction::MoveBackward), "MoveBackward not down when only W is held");

        // Stays down every poll the key is held, unlike an edge-triggered
        // action (see justPressed() below).
        map.update(fakeKeys({GLFW_KEY_W}));
        expectTrue(map.isDown(InputAction::MoveForward), "MoveForward stays down across repeated polls while W held");

        map.update(fakeKeys({}));
        expectTrue(!map.isDown(InputAction::MoveForward), "MoveForward goes back up once W is released");
    }

    // --- A multi-key action is down if EITHER bound key is down ----------
    {
        InputActionMap map;
        map.update(fakeKeys({GLFW_KEY_E}));
        expectTrue(map.isDown(InputAction::MoveUp), "MoveUp down via E (its second default binding)");

        map.update(fakeKeys({GLFW_KEY_SPACE}));
        expectTrue(map.isDown(InputAction::MoveUp), "MoveUp down via Space (its first default binding)");
    }

    // --- Edge-triggered: fires exactly once across polls with the key ---
    // --- held down every time, not once per frame it's held -------------
    {
        InputActionMap map;

        map.update(fakeKeys({}));
        expectTrue(!map.justPressed(InputAction::ToggleDebugUI), "ToggleDebugUI not pressed before F1 is touched");

        map.update(fakeKeys({GLFW_KEY_F1}));
        expectTrue(map.justPressed(InputAction::ToggleDebugUI), "ToggleDebugUI just-pressed on the poll F1 goes down");

        // Same key still held on the NEXT poll: must not fire again, or a
        // held F1 would flicker the debug UI on/off every frame instead of
        // toggling it once per physical press.
        map.update(fakeKeys({GLFW_KEY_F1}));
        expectTrue(!map.justPressed(InputAction::ToggleDebugUI),
                   "ToggleDebugUI does not re-fire on a second poll with F1 still held");
        expectTrue(map.isDown(InputAction::ToggleDebugUI), "ToggleDebugUI still reads as down (level) while F1 held");

        // Release, then press again: should fire once more.
        map.update(fakeKeys({}));
        expectTrue(!map.justPressed(InputAction::ToggleDebugUI), "ToggleDebugUI not pressed on the poll F1 is released");
        map.update(fakeKeys({GLFW_KEY_F1}));
        expectTrue(map.justPressed(InputAction::ToggleDebugUI), "ToggleDebugUI fires again on a fresh press");
    }

    // --- setBinding()/addBinding() actually change what isDown() reads ---
    {
        InputActionMap map;
        map.setBinding(InputAction::MoveForward, {GLFW_KEY_UP});
        map.update(fakeKeys({GLFW_KEY_W}));
        expectTrue(!map.isDown(InputAction::MoveForward), "MoveForward no longer bound to W after setBinding()");
        map.update(fakeKeys({GLFW_KEY_UP}));
        expectTrue(map.isDown(InputAction::MoveForward), "MoveForward now bound to Up after setBinding()");

        map.addBinding(InputAction::MoveForward, GLFW_KEY_W);
        map.update(fakeKeys({GLFW_KEY_W}));
        expectTrue(map.isDown(InputAction::MoveForward), "MoveForward down via W again after addBinding() re-adds it");
    }

    if (failures == 0) {
        std::printf("input_action_map_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "input_action_map_test: %d check(s) failed\n", failures);
    return 1;
}
