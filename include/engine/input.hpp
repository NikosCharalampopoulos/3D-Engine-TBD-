#ifndef ENGINE_INPUT_HPP
#define ENGINE_INPUT_HPP

// A per-frame snapshot of the handful of inputs this engine's camera/controls
// actually consume: WASD-style movement flags, Escape, a debug-UI toggle
// flag, and the raw cursor position. Application polls this once per frame
// from Window (see pollInputState() below) and passes the resulting
// InputState down to whatever needs it -- Camera reads the movement/cursor
// fields, Application itself reads escapePressed/toggleDebugUIPressed --
// instead of Camera reaching into Window/GLFW key constants directly the
// way it did through Phase 5's processKeyboard(const Window&, float).
//
// Through Phase 8c this struct's own comment said it was deliberately NOT
// a general action-mapping/input-binding system. Phase 8d is that later
// phase: pollInputState() now fills these same fields by consulting
// InputActionMap (input_action_map.hpp) -- a real, data-driven table
// mapping named InputActions to physical GLFW_KEY_* bindings -- instead of
// checking hardcoded GLFW_KEY_* constants itself. This struct's own fields
// and meanings are UNCHANGED by that refactor (aside from the one new field
// below): they're still named for what they mean rather than which
// physical key produces them, so Camera's interface (and everything else
// that reads InputState) needed no changes at all.

#include "engine/input_action_map.hpp"

namespace engine {

class Window;

struct InputState {
    bool moveForward = false;   // InputAction::MoveForward, default W
    bool moveBackward = false;  // InputAction::MoveBackward, default S
    bool moveLeft = false;      // InputAction::MoveLeft, default A
    bool moveRight = false;     // InputAction::MoveRight, default D
    bool moveUp = false;        // InputAction::MoveUp, default Space or E
    bool moveDown = false;      // InputAction::MoveDown, default Left Shift or Q
    bool escapePressed = false; // InputAction::Quit, default Esc

    // Phase 8d: edge-triggered (see InputActionMap::justPressed()) --
    // unlike every field above, this is true for exactly one poll per
    // physical key-press of InputAction::ToggleDebugUI's bound key
    // (default F1), not for every poll while the key happens to be held.
    // A toggle needs edge-triggering (holding the key must not flicker
    // the overlay on/off every frame); escapePressed above stays
    // level-triggered because "close the window" is idempotent whether
    // it's acted on once or once-per-frame-while-held -- see
    // application.cpp's own Phase 8d comment for both actions' handling.
    bool toggleDebugUIPressed = false;

    // Raw absolute cursor position, in the same screen coordinates
    // Window::getCursorPos() reports -- Camera::processMouseInput() diffs
    // successive values itself, exactly as it did when Application read
    // Window::getCursorPos() directly.
    double cursorX = 0.0;
    double cursorY = 0.0;
};

// Polls `window` through `actionMap`'s current bindings once and returns a
// snapshot. `actionMap` must be the SAME object every frame (Application
// owns one as a member) rather than a fresh temporary -- edge-triggered
// fields (toggleDebugUIPressed) only work if InputActionMap::update() sees
// each frame's real state compared against the previous frame's, which
// requires persisting across calls. window.isKeyPressed()/getCursorPos()
// report raw current state (not edge-triggered events), so this whole call
// is still safe to make once per frame from Application's main loop, same
// as before this phase.
InputState pollInputState(const Window& window, InputActionMap& actionMap);

}  // namespace engine

#endif  // ENGINE_INPUT_HPP
