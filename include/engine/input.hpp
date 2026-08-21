#ifndef ENGINE_INPUT_HPP
#define ENGINE_INPUT_HPP

// A per-frame snapshot of the handful of inputs this engine's camera/controls
// actually consume: WASD-style movement flags, Escape, and the raw cursor
// position. Application polls this once per frame from Window (see
// pollInputState() below) and passes the resulting InputState down to
// whatever needs it -- currently just Camera -- instead of Camera reaching
// into Window/GLFW key constants directly the way it did through Phase 5's
// processKeyboard(const Window&, float).
//
// Deliberately NOT a general action-mapping/input-binding system: no
// rebindable keys, no abstract "action" enum, no per-device configuration --
// just the concrete fields this phase's Camera reads, named for what they
// mean rather than which physical key produces them (so a later phase could
// swap in gamepad input, rebinding, etc. underneath pollInputState() without
// Camera's interface needing to change).

namespace engine {

class Window;

struct InputState {
    bool moveForward = false;   // W
    bool moveBackward = false;  // S
    bool moveLeft = false;      // A
    bool moveRight = false;     // D
    bool moveUp = false;        // Space or E
    bool moveDown = false;      // Left Shift or Q
    bool escapePressed = false; // Esc

    // Raw absolute cursor position, in the same screen coordinates
    // Window::getCursorPos() reports -- Camera::processMouseInput() diffs
    // successive values itself, exactly as it did when Application read
    // Window::getCursorPos() directly.
    double cursorX = 0.0;
    double cursorY = 0.0;
};

// Polls `window`'s current key/cursor state once and returns a snapshot.
// Window::isKeyPressed()/getCursorPos() report raw current state (not
// edge-triggered events), so this is safe to call once per frame from
// Application's main loop, same as each of those calls was previously safe
// to make directly.
InputState pollInputState(const Window& window);

}  // namespace engine

#endif  // ENGINE_INPUT_HPP
