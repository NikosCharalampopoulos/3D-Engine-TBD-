#include "engine/input.hpp"

#include <GLFW/glfw3.h>

#include "engine/window.hpp"

namespace engine {

InputState pollInputState(const Window& window, InputActionMap& actionMap, bool forceEscapeDown) {
    // Re-samples every bound action's current key state (see
    // InputActionMap::update()) before reading any of them below --
    // still exactly once per frame, the same "poll once, read many" shape
    // pollInputState() already had before this phase; only the source of
    // each field moved from a direct window.isKeyPressed(GLFW_KEY_*) call
    // to a lookup through actionMap's binding table.
    //
    // Phase 16: `forceEscapeDown` ORs a synthetic "GLFW_KEY_ESCAPE is held"
    // answer into this same query -- see this function's own input.hpp
    // comment for why this is where ENGINE_DEBUG_SIMULATE_ESCAPE's headless
    // held-key simulation is injected, rather than after the fact on the
    // returned InputState. Hardcoding GLFW_KEY_ESCAPE here (rather than
    // looking up InputAction::Quit's own current binding) is safe for
    // exactly what this engine does today: nothing anywhere rebinds Quit
    // away from its InputActionMap-constructor default (there is no runtime
    // rebinding UI at all yet -- see input_action_map.hpp's own header
    // comment), so GLFW_KEY_ESCAPE is guaranteed to be the physical key
    // actually driving InputAction::Quit for the whole lifetime of any run
    // this debug var could target.
    actionMap.update([&window, forceEscapeDown](int key) {
        return (forceEscapeDown && key == GLFW_KEY_ESCAPE) || window.isKeyPressed(key);
    });

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
    // Phase 16: also edge-triggered, the SAME InputAction::Quit binding
    // escapePressed above reads, just via justPressed() instead of
    // isDown() -- see input.hpp's own field comment for why this is a
    // second, separate field rather than a change to escapePressed's own
    // existing (still level-triggered) contract.
    input.escapeJustPressed = actionMap.justPressed(InputAction::Quit);

    const auto [cursorX, cursorY] = window.getCursorPos();
    input.cursorX = cursorX;
    input.cursorY = cursorY;

    return input;
}

}  // namespace engine
