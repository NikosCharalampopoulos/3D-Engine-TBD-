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
//
// Phase 14a: no GLFW_RESIZABLE hint is set (never has been), so GLFW's own
// default (resizable = true) already applied before this phase too -- what
// was actually missing was anything reacting to a resize. The constructor
// now also registers a glfwSetFramebufferSizeCallback() that immediately
// re-applies glViewport() to the new framebuffer size the moment GLFW
// reports a resize, in addition to (not instead of) Application::render()
// already re-querying getSize() and calling glViewport() fresh at the top
// of every frame (see application.cpp) -- the callback is deliberately
// redundant with that per-frame call for the steady-state case (both end up
// setting the same viewport before the next real draw), but it matters for
// one specific case the per-frame call alone doesn't cover: on at least
// Windows, an interactive drag-resize runs GLFW's event pump
// (glfwPollEvents()) through a modal loop for the duration of the drag,
// during which this engine's own main loop (poll -> update -> render ->
// swap) never reaches its next render() call at all -- only registered
// callbacks fire during that blocked period. Without a callback, the GL
// viewport stays at its pre-drag size for that entire drag; nothing this
// engine draws during it would use the new size until the drag ends and
// control returns to run()'s own loop. Actually redrawing *new content* at
// each intermediate size during that drag (so the window doesn't appear to
// freeze while being resized) would additionally require rendering from
// inside the callback itself -- out of scope for this phase (see this
// class's own Phase 14a callback comment in window.cpp and README.md's
// Phase 14a section for the exact scope line drawn here): a resized-then-
// released window is guaranteed to render correctly at its new size and
// never crash/corrupt GL state either way, which is this phase's actual
// bar (see this project's Phase 14a brief); a live, flicker-free redraw
// *during* the drag itself is not.
//
// Every render-target FBO this engine allocates elsewhere (Application's
// hdrFramebuffer_, the SSAO g-buffer, bloom's ping-pong buffers, the shadow
// maps, etc.) is still sized once, at construction time, from the window's
// *initial* framebuffer size -- unaffected by either the callback above or
// render()'s per-frame glViewport() re-query, and NOT resized when the
// window is. A resize therefore does not crash or corrupt any of those
// buffers (they simply keep their original resolution and get sampled with
// GL_LINEAR/GL_CLAMP_TO_EDGE like normal, blitting/stretching onto whatever
// the new default-framebuffer viewport is), but it does mean the final
// on-screen image can look visually stretched/distorted relative to the
// window's new aspect ratio until those buffers are themselves made
// resize-aware -- explicitly deferred to Phase 14c's render-to-texture
// viewport work, not attempted here (see this phase's own "What NOT to do"
// scope notes).

#include <string>
#include <utility>

struct GLFWwindow;

namespace engine {

// Requested multisample sample count for the window's own default
// framebuffer (see Window's constructor for the GLFW_SAMPLES hint / real
// GL_SAMPLES verification this feeds). Declared here (not window.cpp-local)
// so Application can request the *same* count for its own multisample HDR
// framebuffer (see application.cpp's MSAA HDR framebuffer bug-fix comment
// and framebuffer.hpp) -- one shared source of truth for "how much MSAA
// this engine asks for" rather than two independently-maintained constants
// that could drift apart. Bumped from 4x to 16x at the project owner's
// request for smoother edges (8x, then 16x were tried first, neither
// cleared this environment's own cap -- see below). Still just a hint (see
// the class comment and the GL_SAMPLES verification below) -- this
// environment's own EGL config probe (see the EGL-context-creation note
// below) previously found configs with up to 4 samples on this specific
// Mesa/llvmpipe stack, so a 32x request here is expected to still only be
// granted at 4x on this particular software renderer; a real GPU (e.g. the
// project owner's own machine) is expected to grant a much higher count
// (32x is above what most consumer GPU drivers actually expose for
// GL_MAX_SAMPLES, commonly capped at 8x or 16x -- so this request may still
// get clamped down on real hardware too, just to a higher value than this
// sandbox's 4x). The actual granted value is always logged from the real
// GL_SAMPLES query (for the window's own default framebuffer) or
// Framebuffer's own GL_MAX_SAMPLES/GL_MAX_COLOR_TEXTURE_SAMPLES-clamped
// query (for Application's multisample HDR framebuffer), never assumed
// from this request.
inline constexpr int kRequestedMsaaSamples = 32;

class Window {
public:
    // Throws std::runtime_error if GLFW fails to initialize, the window/GL
    // context can't be created, or GL function pointers can't be loaded.
    //
    // Phase 14a: `maximized` (default false) sets the GLFW_MAXIMIZED window
    // hint before creation -- an ordinary resizable window at (width, height)
    // that the user can maximize themselves either way, just starting
    // already-maximized when true. Deliberately a constructor parameter, not
    // a hardcoded literal in here: this class stays as agnostic about "what
    // size/state should a window start at" as it already is about width/
    // height -- that's a call-site decision (see main.cpp, which is also
    // where Phase 14a's default width/height bump from 800x600 lives; this
    // class's own default width/height are unchanged from Phase 1 -- it has
    // never hardcoded a size, only main.cpp's call site did). GLFW ignores
    // GLFW_MAXIMIZED on platforms/window managers that don't support it
    // (Xvfb included, verified empirically -- see this phase's own README
    // section) rather than failing, so passing true is always safe to try,
    // never a hard requirement.
    Window(int width, int height, const std::string& title, bool maximized = false);
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

    // Phase 16: hides + locks the OS cursor (glfwSetInputMode(...,
    // GLFW_CURSOR_DISABLED), GLFW's own "capture the mouse" mode -- it also
    // switches GLFW to reporting unbounded relative motion rather than a
    // position clamped to the window, which is exactly what free-fly
    // mouse-look wants) when `captured` is true, or restores the ordinary
    // visible, OS-positioned cursor (GLFW_CURSOR_NORMAL) when false. A thin,
    // policy-free wrapper matching this class's existing "just GLFW, no
    // decision-making" role (see this file's own class comment) --
    // Application::setCameraCaptured() (application.cpp) is what decides
    // WHEN to call this (a Viewport double-click, or Escape while captured)
    // and pairs it with resetting Camera's own tracked mouse position (see
    // camera.hpp's resetMouseTracking()) so the cursor's own position jump
    // on either transition doesn't also snap the camera's view.
    void setCursorCaptured(bool captured);

    GLFWwindow* handle() const { return window_; }

private:
    GLFWwindow* window_ = nullptr;
    int width_;
    int height_;
};

}  // namespace engine

#endif  // ENGINE_WINDOW_HPP
