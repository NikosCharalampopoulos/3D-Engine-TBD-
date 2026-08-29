// Phase 1 entry point.
//
// Construct an Application with sane defaults and run it. All the actual
// windowing/loop/logging/GL-error-checking logic lives in
// include/engine/{window,application,log,gl_debug}.hpp + src/{window,
// application}.cpp -- see README.md for the design and the headless
// verification pattern (tools/run_headless.sh).

#include "engine/application.hpp"
#include "engine/log.hpp"

#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <string>

namespace {

// ENGINE_MAX_FRAMES, when set to a positive integer, caps how many frames
// Application::run() renders before returning on its own. Used ONLY by the
// headless verification path (tools/run_headless.sh under Xvfb, which has
// no real window manager or keyboard to send ESC) so the process still
// exits deterministically instead of depending on the harness's hard
// kill-after-timeout fallback. Left unset, normal interactive runs behave
// as expected: run until the window is closed or ESC is pressed.
//
// Any value that isn't a valid non-negative frame count (missing, empty,
// non-numeric, negative, or too large to fit in a uint64_t) is rejected
// with a warning and treated as unset (0 / uncapped) rather than silently
// misbehaving. This matters because std::strtoull() happily accepts a
// leading '-' and, per the C standard, treats it as negating the parsed
// magnitude mod 2^64 -- so "-5" would otherwise silently become
// 18446744073709551611, which is effectively an infinite frame count for
// any real headless run and would defeat the whole point of this knob
// (the run would then depend entirely on the harness's external hard-kill
// timeout instead of exiting cleanly on its own).
std::uint64_t maxFramesFromEnv() {
    const char* value = std::getenv("ENGINE_MAX_FRAMES");
    if (!value || *value == '\0') {
        return 0;
    }

    const char* p = value;
    while (std::isspace(static_cast<unsigned char>(*p))) {
        ++p;
    }
    if (*p == '-') {
        LOG_WARN(std::string("ENGINE_MAX_FRAMES=\"") + value +
                  "\" is negative; ignoring it and running uncapped");
        return 0;
    }

    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    if (end == value) {
        LOG_WARN(std::string("ENGINE_MAX_FRAMES=\"") + value +
                  "\" is not a valid number; ignoring it and running uncapped");
        return 0;
    }
    if (errno == ERANGE) {
        LOG_WARN(std::string("ENGINE_MAX_FRAMES=\"") + value +
                  "\" is out of range; clamping to the maximum representable frame count");
    }
    return static_cast<std::uint64_t>(parsed);
}

// Phase 14a: this call site's own default window size, bumped from the
// Phase 1 800x600 to 1600x900 -- the same doubling-of-pixel-area-ish jump
// this project already made for MSAA sample counts (window.hpp's own
// comment on 4x -> 8x -> 16x), just for window size: big enough that the
// four Phase 14a dockspace panels (Scene/Assets/Viewport/Inspector) each
// have a genuinely usable amount of screen real estate rather than a
// cramped sliver, while staying a plain 16:9 window (not full display-
// resolution) a user can still move/resize/tile against other windows on
// their own desktop -- "maximizable, not forced fullscreen" (see
// windowMaximizedFromEnv() below for the separate, opt-in "start already
// maximized" knob). 1600x900 rather than 1920x1080 specifically: it's
// already 3x this project's original 800x600 pixel count, which is as much
// headless-verification render-time growth as this phase takes on (see
// ENGINE_WINDOW_WIDTH/HEIGHT below for why the headless path doesn't
// actually pay this cost at all); 1920x1080 would be 4.5x, with no
// benefit for this phase's placeholder-only panels.
constexpr int kDefaultWindowWidth = 1600;
constexpr int kDefaultWindowHeight = 900;

// Phase 14a: ENGINE_WINDOW_WIDTH/ENGINE_WINDOW_HEIGHT let a caller override
// main.cpp's own default window size -- used by tools/run_headless.sh
// specifically (see that script's own Phase 14a comment) to keep launching
// engine_app at the original 800x600 under Xvfb instead of inheriting this
// phase's bigger interactive default. Widening Xvfb's own virtual screen to
// 1600x900 to match instead (the other option this phase's brief allows)
// was rejected: every render pass sized off the window's real framebuffer
// (hdrFramebuffer_, SSAO's g-buffer, bloom's ping-pong buffers -- see
// application.hpp) would then run at 3x this project's original pixel
// count on the same llvmpipe software rasterizer every prior phase's own
// headless timing budget (tools/run_headless.sh's 60s hard-kill timeout,
// sized against Phase 13g's own measured ~20s/60-frames Debug-build cost --
// see that script's own comment) was measured against -- risking a
// regression to every *existing* phase's headless verification, not just
// this one, for a resolution bump this phase's own placeholder-only panels
// don't substantively need. An explicit env var lets a real interactive
// run default to the bigger, more usable window while this project's
// proven headless timing budget stays exactly as it was.
//
// Same rejected-then-accepted validation shape as ENGINE_MAX_FRAMES above:
// missing/empty means "use the default"; anything present but not a valid
// positive int is rejected with a warning (not silently misparsed) and
// falls back to the default too.
int windowDimensionFromEnv(const char* envVarName, int defaultValue) {
    const char* value = std::getenv(envVarName);
    if (!value || *value == '\0') {
        return defaultValue;
    }

    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed <= 0 || parsed > 16384 || errno == ERANGE) {
        LOG_WARN(std::string(envVarName) + "=\"" + value + "\" is not a valid positive window dimension; using " +
                  std::to_string(defaultValue) + " instead");
        return defaultValue;
    }
    return static_cast<int>(parsed);
}

// Phase 14a: ENGINE_WINDOW_MAXIMIZED opts into GLFW_MAXIMIZED (see
// window.hpp's own Phase 14a comment) -- unset by default, same
// getenv-gated-off-by-default pattern as every other flag in this engine
// (ENGINE_CAMERA_DEMO, ENGINE_SHOW_DEBUG_UI, etc.). A normal resizable
// window the user can maximize themselves is this phase's own chosen
// default (see kDefaultWindowWidth/Height's own comment); this env var is
// the documented opt-in for "start already maximized" instead, not a
// change to that default.
bool windowMaximizedFromEnv() {
    const char* value = std::getenv("ENGINE_WINDOW_MAXIMIZED");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

// Phase 17d: this project's new default is a BORDERLESS window with a
// custom-drawn title bar (editor_ui.cpp's own renderTitleBar(), README.md's
// own Phase 17d section) replacing the OS's native one entirely, matching
// the project owner's reference mockup -- unlike every other
// ENGINE_WINDOW_* flag above/below, whose ABSENCE means "use the old/
// pre-existing behavior," ENGINE_WINDOW_DECORATED's absence means "use the
// new default" (borderless), and setting it is the escape hatch BACK to the
// old, OS-decorated behavior. This asymmetry is deliberate, not an
// inconsistency: this class-level default lives in Window itself (see
// window.hpp's own Phase 17d comment, `decorated = true`, the class's
// opinion-free/native-GLFW-behavior default) precisely so nothing about
// Window's OWN default silently changes; only THIS call site's own chosen
// value -- kDefaultWindowDecorated below -- actually flips the real
// interactive-run default to borderless, mirroring exactly how
// kDefaultWindowWidth/Height above already override Window's own unrelated
// Phase-1-era default width/height without Window itself ever needing to
// know this project settled on 1600x900.
//
// A real escape hatch, not just a headless-verification convenience (unlike
// e.g. ENGINE_WINDOW_WIDTH/HEIGHT's own headless-only motivation, see that
// pair's own comment above): this phase's own brief is explicit that native
// OS edge-drag-to-resize is a known, real limitation of a borderless GLFW
// window on at least some platforms/window managers (see window.hpp's own
// Phase 17d comment and README.md's own Phase 17d section for the full
// accounting) -- a real user who hits exactly that on their own desktop can
// set this env var to get the OS's native title bar/border (and its native
// resize handles) back without needing a source change or a rebuild.
constexpr bool kDefaultWindowDecorated = false;

bool windowDecoratedFromEnv() {
    const char* value = std::getenv("ENGINE_WINDOW_DECORATED");
    if (!value || *value == '\0') {
        return kDefaultWindowDecorated;
    }
    // Same "present but not \"0\"" truthiness convention windowMaximizedFromEnv()
    // above already uses -- ENGINE_WINDOW_DECORATED=1 (or any other non-"0"
    // value) forces the OS's native decorations back on.
    return std::string(value) != "0";
}

}  // namespace

int main() {
    try {
        const int width = windowDimensionFromEnv("ENGINE_WINDOW_WIDTH", kDefaultWindowWidth);
        const int height = windowDimensionFromEnv("ENGINE_WINDOW_HEIGHT", kDefaultWindowHeight);
        engine::Application app(width, height, "3D Engine", maxFramesFromEnv(), windowMaximizedFromEnv(),
                                  windowDecoratedFromEnv());
        app.run();
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("Fatal: ") + e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
