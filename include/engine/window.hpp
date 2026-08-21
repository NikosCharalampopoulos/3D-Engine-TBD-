#ifndef ENGINE_WINDOW_HPP
#define ENGINE_WINDOW_HPP

// RAII wrapper around a GLFW window + OpenGL 3.3 core context.
//
// The constructor does all setup (glfwInit, window/context creation, GLAD
// function loading) and throws std::runtime_error on the first failure,
// cleaning up anything it already created before throwing -- so a failed
// construction never leaves a half-initialized GLFW/GL state behind. The
// destructor tears everything down (destroys the window, terminates GLFW)
// unconditionally, so normal scope exit, early return, and exceptions
// thrown after construction all clean up the same way.
//
// Only one Window is expected to exist at a time in this phase (it owns
// GLFW's global init/terminate pair itself rather than sharing a refcount
// with sibling windows); a later phase can add multi-window support if it's
// ever needed.

#include <string>
#include <utility>

struct GLFWwindow;

namespace engine {

class Window {
public:
    // Throws std::runtime_error if GLFW fails to initialize, the window/GL
    // context can't be created, or GL function pointers can't be loaded.
    Window(int width, int height, const std::string& title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    bool shouldClose() const;
    void swapBuffers();
    void pollEvents();

    // Framebuffer size in pixels (may differ from the constructor's
    // width/height on HiDPI displays; matches what glViewport should use).
    std::pair<int, int> getSize() const;

    // True if `key` (a GLFW_KEY_* constant) is currently held down.
    bool isKeyPressed(int key) const;

    // Cursor position in screen coordinates, relative to the window's
    // upper-left corner (whatever GLFW itself reports -- e.g. it never
    // moves under Xvfb, since there's no real pointer device driving it).
    // Camera's mouse-look diffs successive calls to this itself; Window
    // just reports the raw absolute position each time, matching how
    // isKeyPressed() reports raw current state rather than edge-triggered
    // events.
    std::pair<double, double> getCursorPos() const;

    GLFWwindow* handle() const { return window_; }

private:
    GLFWwindow* window_ = nullptr;
    int width_;
    int height_;
};

}  // namespace engine

#endif  // ENGINE_WINDOW_HPP
