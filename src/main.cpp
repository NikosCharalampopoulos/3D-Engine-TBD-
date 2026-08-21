// Phase 1 entry point.
//
// Construct an Application with sane defaults and run it. All the actual
// windowing/loop/logging/GL-error-checking logic lives in
// include/engine/{window,application,log,gl_debug}.hpp + src/{window,
// application}.cpp -- see README.md for the design and the headless
// verification pattern (tools/run_headless.sh).

#include "engine/application.hpp"
#include "engine/log.hpp"

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
std::uint64_t maxFramesFromEnv() {
    const char* value = std::getenv("ENGINE_MAX_FRAMES");
    if (!value) {
        return 0;
    }
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    if (end == value) {
        return 0;
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
