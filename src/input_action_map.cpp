#include "engine/input_action_map.hpp"

#include <GLFW/glfw3.h>

#include <utility>

namespace engine {

InputActionMap::InputActionMap() {
    // Matches input.cpp's pre-Phase-8d hardcoded behavior exactly (see
    // input.hpp's own comment): W/A/S/D movement, Space-or-E up,
    // LeftShift-or-Q down, Escape quits. ToggleDebugUI -> F1 is new in
    // this phase (see application.cpp's own Phase 8d comment on how it's
    // wired to DebugUI).
    bindings_[InputAction::MoveForward] = {GLFW_KEY_W};
    bindings_[InputAction::MoveBackward] = {GLFW_KEY_S};
    bindings_[InputAction::MoveLeft] = {GLFW_KEY_A};
    bindings_[InputAction::MoveRight] = {GLFW_KEY_D};
    bindings_[InputAction::MoveUp] = {GLFW_KEY_SPACE, GLFW_KEY_E};
    bindings_[InputAction::MoveDown] = {GLFW_KEY_LEFT_SHIFT, GLFW_KEY_Q};
    bindings_[InputAction::Quit] = {GLFW_KEY_ESCAPE};
    bindings_[InputAction::ToggleDebugUI] = {GLFW_KEY_F1};

    // Every action gets a state_ entry up front (both flags false) so
    // isDown()/justPressed() never need to special-case "never polled
    // yet" separately from "polled and currently up" -- both read as
    // "not down", which is the correct answer for either case.
    for (const auto& [action, keys] : bindings_) {
        (void)keys;
        state_[action] = ActionState{};
    }
}

void InputActionMap::setBinding(InputAction action, std::vector<int> glfwKeys) {
    bindings_[action] = std::move(glfwKeys);
    // setBinding() can target an action that predates this call (every
    // InputAction does, per the constructor above), but guard against a
    // future InputAction value being added to the enum without a matching
    // state_ entry -- state_.find() in isDown()/justPressed() already
    // treats "no entry" as "not down", but try_emplace here means a
    // caller can setBinding() before the first update() and still get a
    // sane state_ entry rather than relying on that fallback.
    state_.try_emplace(action);
}

void InputActionMap::addBinding(InputAction action, int glfwKey) {
    bindings_[action].push_back(glfwKey);
    state_.try_emplace(action);
}

const std::vector<int>& InputActionMap::bindingsFor(InputAction action) const {
    static const std::vector<int> kEmpty;
    const auto it = bindings_.find(action);
    return it != bindings_.end() ? it->second : kEmpty;
}

void InputActionMap::update(const KeyDownQuery& isKeyDown) {
    for (auto& [action, keys] : bindings_) {
        bool down = false;
        for (int key : keys) {
            if (isKeyDown(key)) {
                down = true;
                break;
            }
        }
        ActionState& s = state_[action];
        s.wasDown = s.down;
        s.down = down;
    }
}

bool InputActionMap::isDown(InputAction action) const {
    const auto it = state_.find(action);
    return it != state_.end() && it->second.down;
}

bool InputActionMap::justPressed(InputAction action) const {
    const auto it = state_.find(action);
    return it != state_.end() && it->second.down && !it->second.wasDown;
}

}  // namespace engine
