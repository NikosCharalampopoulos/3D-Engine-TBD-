#ifndef ENGINE_WINDOW_HPP
#define ENGINE_WINDOW_HPP

// RAII wrapper around a GLFW window + OpenGL 4.3 core context on
// Linux/Windows (bumped from 3.3 in Phase 12 as a foundation change for
// Phase 13's compute-shader clustered lighting -- see README.md's Phase 12
// section). macOS requests 4.1 instead, the highest core profile Apple's
// (deprecated, frozen) OpenGL implementation has ever shipped -- requesting
// 4.3 there would fail context creation outright on every Mac.
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
//
// Phase 6 requests a multisampled default framebuffer: GLFW_SAMPLES is set
// via glfwWindowHint() before window/context creation (GLFW handles
// allocating the multisample-capable framebuffer itself once that hint is
// set -- there's no separate "create an FBO" step needed for the default
// framebuffer), and GL_MULTISAMPLE is enabled right after the context
// becomes current. GLFW_SAMPLES is only a *hint* -- some GL/driver
// combinations may ignore it -- so the constructor also queries the real
// GL_SAMPLES value back via glGetIntegerv() and logs what it actually got
// (a warning, not a thrown error, if it came back 0): this phase's bar is
// "prove MSAA actually took effect", not just "asked for it and assumed".

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
