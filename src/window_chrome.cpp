// Phase 17d: see window_chrome.hpp's own header comment for the full design.
// This translation unit depends on nothing beyond <cmath> (for std::lround)
// -- no ecs.hpp, no GLM, no GLFW/GL/ImGui at all -- the same
// minimal-dependency shape camera_capture.cpp/editor_icons.cpp already
// establish for the identical reason: tests/window_chrome_test.cpp links
// this file alone.

#include "engine/window_chrome.hpp"

namespace engine {

WindowPosition applyDragDelta(int currentWindowX, int currentWindowY, float mouseDeltaX, float mouseDeltaY) {
    // std::lround(), not a plain static_cast<int>: truncation toward zero
    // (what static_cast<int> does) would round -0.6 to 0, silently
    // "swallowing" a small leftward/upward movement while an equal-magnitude
    // rightward/downward movement (0.6 -> 0 under truncation too, but 0.6
    // itself is already the SAME distance from zero either way) -- the real
    // asymmetry only shows up right at these small negative deltas, which a
    // slow real drag produces constantly. Round-to-nearest treats both
    // directions identically, matching this header's own comment on why
    // sub-pixel deltas are accepted as `float` in the first place.
    return WindowPosition{
        currentWindowX + static_cast<int>(std::lround(mouseDeltaX)),
        currentWindowY + static_cast<int>(std::lround(mouseDeltaY)),
    };
}

bool decideMaximizeRestoreToggle(bool currentlyMaximized) {
    return !currentlyMaximized;
}

}  // namespace engine
