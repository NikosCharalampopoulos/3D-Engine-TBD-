#include "engine/input.hpp"

#include "engine/window.hpp"

namespace engine {

InputState pollInputState(const Window& window, InputActionMap& actionMap) {
    // Re-samples every bound action's current key state (see
    // InputActionMap::update()) before reading any of them below --
    // still exactly once per frame, the same "poll once, read many" shape
    // pollInputState() already had before this phase; only the source of
    // each field moved from a direct window.isKeyPressed(GLFW_KEY_*) call
    // to a lookup through actionMap's binding table.
    actionMap.update([&window](int key) { return window.isKeyPressed(key); });

    InputState input;
    input.moveForward = actionMap.isDown(InputAction::MoveForward);
    input.moveBackward = actionMap.isDown(InputAction::MoveBackward);
    input.moveLeft = actionMap.isDown(InputAction::MoveLeft);
    input.moveRight = actionMap.isDown(InputAction::MoveRight);
    // Up/down each still accept two key choices by default -- now
    // expressed as two bindings on one action (see InputActionMap's
    // constructor) rather than a hardcoded `||` of two
    // window.isKeyPressed() calls.
    input.moveUp = actionMap.isDown(InputAction::MoveUp);
    input.moveDown = actionMap.isDown(InputAction::MoveDown);
    // Level-triggered, same as every movement flag above -- see
    // input.hpp's own comment on why that's fine for "close the window"
    // but would be wrong for a toggle.
    input.escapePressed = actionMap.isDown(InputAction::Quit);
    // Edge-triggered -- see InputActionMap::justPressed()'s own comment
    // and input.hpp's field comment.
    input.toggleDebugUIPressed = actionMap.justPressed(InputAction::ToggleDebugUI);

    const auto [cursorX, cursorY] = window.getCursorPos();
    input.cursorX = cursorX;
    input.cursorY = cursorY;

    return input;
}

}  // namespace engine
