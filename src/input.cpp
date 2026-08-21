#include "engine/input.hpp"

#include <GLFW/glfw3.h>

#include "engine/window.hpp"

namespace engine {

InputState pollInputState(const Window& window) {
    InputState input;
    input.moveForward = window.isKeyPressed(GLFW_KEY_W);
    input.moveBackward = window.isKeyPressed(GLFW_KEY_S);
    input.moveLeft = window.isKeyPressed(GLFW_KEY_A);
    input.moveRight = window.isKeyPressed(GLFW_KEY_D);
    // Up/down accept two key choices each -- same pair Camera's Phase 3-5
    // processKeyboard() accepted directly, kept identical here so behavior
    // doesn't change.
    input.moveUp = window.isKeyPressed(GLFW_KEY_SPACE) || window.isKeyPressed(GLFW_KEY_E);
    input.moveDown = window.isKeyPressed(GLFW_KEY_LEFT_SHIFT) || window.isKeyPressed(GLFW_KEY_Q);
    input.escapePressed = window.isKeyPressed(GLFW_KEY_ESCAPE);

    const auto [cursorX, cursorY] = window.getCursorPos();
    input.cursorX = cursorX;
    input.cursorY = cursorY;

    return input;
}

}  // namespace engine
