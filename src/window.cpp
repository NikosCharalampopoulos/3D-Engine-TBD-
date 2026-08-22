#include "engine/window.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <stdexcept>
#include <string>

#include "engine/gl_debug.hpp"
#include "engine/log.hpp"

namespace {

void glfwErrorCallback(int error, const char* description) {
    LOG_ERROR(std::string("GLFW error ") + std::to_string(error) + ": " + description);
}

// Requested multisample sample count. Bumped from 4x to 8x at the project
// owner's request for smoother edges. Still just a hint (see the class
// comment and the GL_SAMPLES verification below) -- this environment's own
// EGL config probe (see the EGL-context-creation note below) previously
// found configs with up to 4 samples on this specific Mesa/llvmpipe stack,
// so an 8x request here may still only be granted at 4x (or fall back to 0)
// on this particular software renderer; a real GPU (e.g. the project
// owner's own machine) is expected to grant 8x without issue. The actual
// granted value is always logged from the real GL_SAMPLES query, not
// assumed from this request.
constexpr int kRequestedMsaaSamples = 8;

// Escape hatch for the Linux EGL-context-creation hint below. It's on by
// default because it's verified necessary on this project's headless
// Xvfb+llvmpipe target (see the constructor's comment) and GLFW's EGL path
// is dlopen'd at runtime, guarded to Linux only, and fails gracefully (see
// the constructor's window_ null-check) -- so forcing it costs nothing on a
// system where it works. But "costs nothing" isn't the same as "can't ever
// regress something else": a real desktop Linux box this was never
// validated against (indirect/network GLX such as `ssh -X` or a VNC/X
// server whose GLX works but whose EGL story is thinner, an old proprietary
// driver, a minimal system with no libEGL at all) could genuinely work
// today via plain GLX and stop working once EGL is forced -- unlike the
// headless CI target, nobody has verified those. Rather than assume "can't
// hurt" for environments nobody tested, this one env var lets a real user
// who hits exactly that regression get back to GLFW's default GLX path
// without needing a source change -- matching this codebase's existing
// convention of getenv-gated escape hatches for behavior that only some
// environments need (ENGINE_MAX_FRAMES, ENGINE_CAMERA_DEMO).
bool eglContextDisabledFromEnv() {
    const char* value = std::getenv("ENGINE_DISABLE_EGL_CONTEXT");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

}  // namespace

namespace engine {

Window::Window(int width, int height, const std::string& title) : width_(width), height_(height) {
    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    // Phase 12: bumped from 3.3 to 4.3 core -- a foundation change for
    // Phase 13's compute-shader clustered lighting (compute shaders are a GL
    // 4.3 feature; nothing in *this* phase uses them yet). GLFW's contract
    // for GLFW_CONTEXT_VERSION_MAJOR/MINOR + GLFW_OPENGL_CORE_PROFILE is to
    // fail context creation outright (glfwCreateWindow returns nullptr, an
    // error already handled below) rather than silently handing back a
    // lower version if the driver can't satisfy the requested one -- so a
    // 4.3 core context genuinely was granted if this constructor doesn't
    // throw, not silently downgraded. Verified on this project's headless
    // Xvfb + Mesa llvmpipe target (see README.md's Phase 12 section): this
    // environment's Mesa (25.2.8) reports GL 4.5 core available, comfortably
    // above 4.3, via both the EGL and GLX (ENGINE_DISABLE_EGL_CONTEXT=1)
    // context-creation paths below.
    //
    // Apple is a real exception to that "just request 4.3" plan, not merely
    // an untested one: macOS's OpenGL implementation (deprecated by Apple in
    // favor of Metal since 10.14, frozen since) has never shipped a core
    // profile above 4.1, full stop -- there is no driver update or hardware
    // that changes that ceiling. Requesting 4.3 there wouldn't risk a lower
    // context the way an untested Linux/Windows driver might; it would fail
    // glfwCreateWindow on literally every Mac, unconditionally, breaking a
    // platform this codebase already carries explicit support for (see the
    // GLFW_OPENGL_FORWARD_COMPAT hint right below, plus paths.cpp's and
    // log.hpp's own _WIN32 branches for the other non-Linux target) -- for
    // a phase that doesn't call anything version-4.3-specific yet. Request
    // the real ceiling macOS can grant instead of Phase 13's future floor;
    // whatever in Phase 13 actually needs compute shaders will need its own
    // macOS story then (feature-gated off, or reworked), not a context that
    // silently stops existing today.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
#ifdef __APPLE__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#endif
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    // Visible (not hidden) so an X server (e.g. Xvfb) actually maps a window
    // that can be screenshotted from the outside for headless verification.
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    // Requests a multisampled default framebuffer -- must be set before
    // glfwCreateWindow(); GLFW/GLX/EGL pick a matching framebuffer config at
    // creation time, it can't be changed on an existing window/context.
    // This is a hint, not a guarantee (see this file's class comment) --
    // verified against the real GL_SAMPLES value further down, after the
    // context exists.
    glfwWindowHint(GLFW_SAMPLES, kRequestedMsaaSamples);
#if defined(__linux__)
    // On this project's Linux/X11 target, request EGL for context creation
    // instead of GLFW's GLX default. This was verified necessary, not
    // cosmetic: probing this repo's headless Xvfb + Mesa llvmpipe
    // combination directly (glXGetFBConfigs) turns up zero GLX FBConfigs
    // with GLX_SAMPLE_BUFFERS > 0 at all -- Xvfb's own GLX implementation
    // simply never advertises a multisample-capable visual, no matter what
    // GLFW_SAMPLES asks for, so GL_SAMPLES silently comes back 0 through
    // GLX on this stack. The same probe against EGL
    // (eglGetConfigs/eglGetConfigAttrib) on the same display/driver finds
    // 25 window-capable EGL configs with up to 4 samples -- EGL's config
    // negotiation is independent of the X server's own (limited) GLX
    // extension, so asking GLFW to create the context via EGL instead
    // reaches those configs and gets real multisampling. A real desktop
    // Linux GLX driver (not Xvfb's) typically advertises multisample
    // FBConfigs fine and wouldn't need this, but there's no reason not to
    // prefer the API that's verified to actually deliver what was
    // requested. Guarded to Linux only: EGL context creation isn't
    // available on macOS (no EGL there), so other platforms are left on
    // GLFW's own native default (GLFW_NATIVE_CONTEXT_API -- WGL on Windows,
    // NSGL on macOS), unaffected by this hint.
    //
    // ENGINE_DISABLE_EGL_CONTEXT is the escape hatch for this (see
    // eglContextDisabledFromEnv() above) -- unset by default, so this hint
    // still applies unconditionally on Linux out of the box.
    if (!eglContextDisabledFromEnv()) {
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    }
#endif

    window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window (no GL context available?)");
    }

    glfwMakeContextCurrent(window_);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwTerminate();
        throw std::runtime_error("Failed to load OpenGL functions via glad");
    }

    LOG_INFO(std::string("OpenGL vendor:   ") + reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
    LOG_INFO(std::string("OpenGL renderer: ") + reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    LOG_INFO(std::string("OpenGL version:  ") + reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    // GL_MULTISAMPLE only has any effect if the framebuffer GLFW actually
    // created has sample buffers to begin with (see the GLFW_SAMPLES hint
    // above) -- enabling it unconditionally here is harmless either way (it
    // just has nothing to do if GL_SAMPLES comes back 0), so the real proof
    // is the glGetIntegerv(GL_SAMPLES, ...) query right below, not this call
    // succeeding.
    GL_CHECK(glEnable(GL_MULTISAMPLE));
    GLint actualSamples = 0;
    GL_CHECK(glGetIntegerv(GL_SAMPLES, &actualSamples));
    if (actualSamples > 0) {
        LOG_INFO("MSAA active: GL_SAMPLES = " + std::to_string(actualSamples) + " (requested " +
                  std::to_string(kRequestedMsaaSamples) + ")");
    } else {
        LOG_WARN("Requested " + std::to_string(kRequestedMsaaSamples) +
                  "x MSAA via GLFW_SAMPLES, but GL_SAMPLES reports 0 -- this GL/driver combo did not honor the "
                  "hint, so rendering below proceeds without multisampling");
    }

    GL_CHECK(glViewport(0, 0, width_, height_));

    LOG_INFO("Window created (" + std::to_string(width_) + "x" + std::to_string(height_) + ")");
}

Window::~Window() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    // Safe even if glfwInit() never succeeded / window_ was never set: this
    // destructor only runs after a successful construction (a throwing
    // constructor's partially-built object has no destructor invoked), and
    // construction always leaves glfwInit() successfully called by the time
    // it returns normally.
    glfwTerminate();
    LOG_INFO("Window destroyed, GLFW terminated");
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(window_) != 0;
}

void Window::swapBuffers() {
    glfwSwapBuffers(window_);
}

void Window::pollEvents() {
    glfwPollEvents();
}

std::pair<int, int> Window::getSize() const {
    int w = 0;
    int h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    return {w, h};
}

bool Window::isKeyPressed(int key) const {
    return glfwGetKey(window_, key) == GLFW_PRESS;
}

std::pair<double, double> Window::getCursorPos() const {
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window_, &x, &y);
    return {x, y};
}

}  // namespace engine
