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

}  // namespace

int main() {
    try {
        engine::Application app(800, 600, "3D Engine", maxFramesFromEnv());
        app.run();
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("Fatal: ") + e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
