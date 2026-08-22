#ifndef ENGINE_INPUT_ACTION_MAP_HPP
#define ENGINE_INPUT_ACTION_MAP_HPP

// Phase 8d: the data-driven layer underneath pollInputState() (input.hpp) --
// maps logical, named InputActions to the physical GLFW_KEY_* constant(s)
// that trigger them, and tracks enough per-action state (this poll's down/up
// vs. the previous poll's) to distinguish level-triggered actions
// (movement: true every frame the key is held) from edge-triggered ones
// (Quit, ToggleDebugUI: true only on the single poll where a bound key
// transitions from up to down). See input.hpp's own comment for how
// pollInputState() consumes this.
//
// This replaces input.cpp's old style -- one hardcoded
// `window.isKeyPressed(GLFW_KEY_W)` `if` per action, `||`'d together for
// the two actions that accepted two keys -- with a real binding TABLE
// (bindings_ below): reassigning which key(s) drive an action is now a
// setBinding()/addBinding() call instead of editing an `if` in
// pollInputState(), even though nothing yet exposes a runtime rebinding UI
// for it (seeing README.md's own Phase 8d section for why a rebinding menu
// is out of scope for this phase).
//
// Deliberately narrow, matching this project's "extensible shape, not
// speculative handlers" convention (see Phase 8b's scene schema comment):
// a binding is a plain `int` GLFW_KEY_* constant, not some invented
// device-agnostic key/button abstraction -- there is exactly one input
// device this engine reads from (keyboard, via Window::isKeyPressed()), so
// abstracting over a second device that doesn't exist yet (gamepad) would
// be speculative. A later phase that actually adds a second device can
// widen `int` into a small tagged key/button type at that point, when
// there's a second real case to design the abstraction against.
//
// No GLFW/Window/GL dependency in this header, and update() below takes a
// generic `isKeyDown` callable rather than a `const Window&` directly --
// so this class can be exercised by a unit test with a fake key source and
// no live GL context, the same way Phase 8b's scene_serialization_test
// avoided needing one (see tests/input_action_map_test.cpp).

#include <functional>
#include <unordered_map>
#include <vector>

namespace engine {

enum class InputAction {
    MoveForward,
    MoveBackward,
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    Quit,
    ToggleDebugUI,
};

// Reports whether a given physical key (a GLFW_KEY_* constant) is
// currently held down -- the exact contract Window::isKeyPressed(int)
// already has (current state, not an edge-triggered event). Production
// code passes a lambda wrapping Window::isKeyPressed(); tests pass a fake
// with the same shape.
using KeyDownQuery = std::function<bool(int)>;

class InputActionMap {
public:
    // Populates the default bindings, matching this engine's
    // pre-Phase-8d hardcoded behavior exactly, plus one new binding
    // (ToggleDebugUI -> F1) that didn't exist before this phase -- see
    // input_action_map.cpp's constructor definition for the actual
    // GLFW_KEY_* table.
    InputActionMap();

    // Replaces every key bound to `action` with exactly `glfwKeys` (an
    // empty vector unbinds the action entirely -- isDown()/justPressed()
    // then always report false for it from the next update() onward).
    void setBinding(InputAction action, std::vector<int> glfwKeys);

    // Adds one more key alongside whatever is already bound to `action`
    // (e.g. the default MoveUp binding is Space *or* E -- either key
    // presses the action down; that's expressed as two entries in
    // MoveUp's binding list, built via two addBinding() calls, rather
    // than a hardcoded `||` of two isKeyPressed() checks).
    void addBinding(InputAction action, int glfwKey);

    const std::vector<int>& bindingsFor(InputAction action) const;

    // Re-samples every bound action's current down/up state via
    // `isKeyDown` and rolls this poll's "current" into "previous" for the
    // next call -- call exactly once per frame/poll, before isDown()/
    // justPressed() below read this poll's results. Must be the SAME
    // InputActionMap object across polls for justPressed() to see real
    // transitions (Application owns one as a member and passes it into
    // pollInputState() every frame -- see input.hpp).
    void update(const KeyDownQuery& isKeyDown);

    // Level-triggered: true if `action` was down as of the most recent
    // update() (i.e. any of its bound keys was held then).
    bool isDown(InputAction action) const;

    // Edge-triggered: true only if `action` is down as of the most recent
    // update() but was NOT down as of the update() before that -- fires
    // exactly once per physical key-press, even across many polls with
    // the key held down throughout.
    bool justPressed(InputAction action) const;

private:
    struct ActionState {
        bool down = false;
        bool wasDown = false;
    };

    std::unordered_map<InputAction, std::vector<int>> bindings_;
    std::unordered_map<InputAction, ActionState> state_;
};

}  // namespace engine

#endif  // ENGINE_INPUT_ACTION_MAP_HPP
