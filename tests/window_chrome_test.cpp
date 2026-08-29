// Phase 17d: tests engine::applyDragDelta()/decideMaximizeRestoreToggle()
// (src/window_chrome.cpp) in isolation -- same "plain executable, links only
// the pure logic file it's testing" shape as camera_capture_test/
// editor_icons_test above. window_chrome.cpp depends on nothing beyond
// <cmath>, so this needs no live GL context, no real GLFW window, and no
// Dear ImGui frame -- none of which "does the OS window actually move/
// maximize/restore" is verifiable without anyway (see README.md's own Phase
// 17d section for exactly what headless verification could and couldn't
// cover for the rest of this feature as a result). This is the one part of
// the whole feature that's pure input-in/output-out logic, exhaustively
// covered here.

#include "engine/window_chrome.hpp"

#include <cassert>
#include <iostream>

int main() {
    using engine::applyDragDelta;
    using engine::decideMaximizeRestoreToggle;
    using engine::WindowPosition;

    // --- applyDragDelta(): the ordinary cases ----------------------------

    // Zero delta: window stays exactly where it is -- the overwhelmingly
    // common per-frame case while a drag is "active" but the mouse hasn't
    // actually moved this particular frame (e.g. it's being held perfectly
    // still, or Dear ImGui reported a sub-frame with no new OS mouse event).
    {
        const WindowPosition p = applyDragDelta(100, 200, 0.0f, 0.0f);
        assert(p.x == 100);
        assert(p.y == 200);
    }

    // A whole-pixel positive delta on both axes: window moves right+down by
    // exactly that amount.
    {
        const WindowPosition p = applyDragDelta(100, 200, 15.0f, 7.0f);
        assert(p.x == 115);
        assert(p.y == 207);
    }

    // A whole-pixel negative delta on both axes: window moves left+up.
    {
        const WindowPosition p = applyDragDelta(500, 400, -30.0f, -12.0f);
        assert(p.x == 470);
        assert(p.y == 388);
    }

    // --- applyDragDelta(): sub-pixel rounding, both signs ----------------
    // See this function's own .cpp comment for exactly why round-to-nearest
    // (not truncation) matters here -- these three cases are the specific
    // regression that comment describes, pinned so a future edit that swaps
    // std::lround() for a plain static_cast<int> fails immediately.

    // A small POSITIVE sub-pixel delta rounds up to +1, not down to 0.
    {
        const WindowPosition p = applyDragDelta(0, 0, 0.6f, 0.0f);
        assert(p.x == 1);
    }

    // A small NEGATIVE sub-pixel delta rounds to -1, not truncated to 0 --
    // the exact asymmetry a plain static_cast<int> would introduce.
    {
        const WindowPosition p = applyDragDelta(0, 0, -0.6f, 0.0f);
        assert(p.x == -1);
    }

    // Exactly at the halfway point (0.5): round-to-nearest still produces a
    // whole-pixel move rather than getting stuck at 0 -- std::lround() rounds
    // halfway cases away from zero, so +0.5 -> 1.
    {
        const WindowPosition p = applyDragDelta(0, 0, 0.5f, -0.5f);
        assert(p.x == 1);
        assert(p.y == -1);
    }

    // --- applyDragDelta(): a realistic multi-frame drag sequence ---------
    // Confirms the function composes correctly across several consecutive
    // calls the way a real drag actually uses it -- each call's own output
    // fed back in as the next call's `currentWindowX/Y`, matching
    // renderTitleBar()'s own per-frame "re-query the window's current
    // position, add this frame's delta" loop (editor_ui.cpp).
    {
        WindowPosition p{300, 300};
        p = applyDragDelta(p.x, p.y, 10.0f, 0.0f);
        p = applyDragDelta(p.x, p.y, 10.0f, 0.0f);
        p = applyDragDelta(p.x, p.y, -5.0f, 20.0f);
        assert(p.x == 315);  // 300 + 10 + 10 - 5
        assert(p.y == 320);  // 300 + 0 + 0 + 20
    }

    // --- decideMaximizeRestoreToggle(): both directions -------------------

    // Not currently maximized -> the button/double-click should maximize.
    assert(decideMaximizeRestoreToggle(/*currentlyMaximized=*/false) == true);

    // Currently maximized -> the button/double-click should restore.
    assert(decideMaximizeRestoreToggle(/*currentlyMaximized=*/true) == false);

    std::cout << "window_chrome_test: all checks passed" << std::endl;
    return 0;
}
