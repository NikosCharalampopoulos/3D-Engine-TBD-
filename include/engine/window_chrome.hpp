#ifndef ENGINE_WINDOW_CHROME_HPP
#define ENGINE_WINDOW_CHROME_HPP

// Phase 17d: the fourth and last item in the "Phase 17: visual design" arc
// (17a base theme, 17b icon font, 17c toolbar -- see README.md's own
// sections for each). The project owner supplied a reference mockup of a
// borderless window with rounded outer corners and a custom-drawn top bar
// (app icon/name on the left, minimize/maximize/close on the right) standing
// in for the OS's own native title bar entirely -- the last remaining piece
// of the mockup Phase 17a/17b/17c deliberately left untouched (see e.g.
// Phase 17a's own README section: "any custom window chrome/borderless
// rounded OUTER window border (Phase 17d -- real platform-level
// borderless-window work, much bigger scope...)").
//
// --- Why this is its own header, GL/GLFW/ImGui-free, same as
// camera_capture.hpp/editor_icons.hpp before it -----------------------------
// Almost everything this phase touches is real, honest platform-glue code
// that cannot be meaningfully unit-tested: "does GLFW_DECORATED=false
// actually remove the OS title bar," "does glfwSetWindowPos() actually move
// the window," "does glfwMaximizeWindow() actually maximize it" are all
// questions only a real, interactive window manager can answer -- headless
// Xvfb in this project's own CI/dev environment runs no window manager at
// all (see README.md's own Phase 17d section for exactly what that does and
// doesn't verify), and even a real desktop run needs a human's own eyes on
// it, the same "cannot be exercised by a headless run" honesty this
// project's own Phase 16/17a/17c sections already established for a real
// mouse gesture (a Viewport double-click, a Create-menu popup click). But
// ONE piece of this feature genuinely is pure, input-in/output-out logic
// with nothing GLFW-specific about it at all: given the window's current
// on-screen position and this frame's raw mouse-movement delta, what
// position should glfwSetWindowPos() be called with to keep the window
// "stuck" under the cursor while a title-bar drag is in progress. Pulling
// that one computation out here -- the same "small, standalone decision,
// unit-tested in isolation, with no live GL context/GLFW window/Dear ImGui
// frame at all" shape light.hpp/asset_drop.hpp/camera_capture.hpp/
// editor_icons.hpp already establish -- is what lets tests/
// window_chrome_test.cpp exercise it exhaustively despite this phase's own
// real ceiling on what else can be verified here.
//
// --- The portable drag technique this function assumes, and why NOT a
// screen-space cursor position -----------------------------------------------
// A naive "record the cursor's absolute screen position when the drag
// starts, then on each later frame compute newWindowPos = windowPosAtStart +
// (currentScreenCursorPos - startScreenCursorPos)" design needs the cursor's
// GLOBAL desktop position -- which GLFW's own glfwGetCursorPos() does NOT
// give (its own documented contract is "relative to the content area of the
// window," see window.hpp's own getCursorPos() comment); Dear ImGui's
// io.MousePos, fed by the exact same GLFW callback, carries the identical
// window-relative contract. Rather than reconstruct a global position by
// adding a separately-queried window position to it (extra GLFW calls, and
// an extra opportunity for the two queries to observe the window having
// moved a partial frame apart from each other), this function instead takes
// this frame's raw incremental mouse-movement delta -- Dear ImGui's own
// io.MouseDelta, "this frame's mouse position minus last frame's," which
// stays correct and window-position-independent regardless of where the
// window itself currently sits, exactly because it's already a DELTA, not an
// absolute coordinate anchored to one particular frame's window position.
// Simply adding that delta onto the window's own CURRENT position (re-queried
// fresh via glfwGetWindowPos() every frame -- see window.hpp's own
// getWindowPos()) reproduces the identical end result as the anchor-based
// technique with less state to track (no drag-start position to remember at
// all) and, as a bonus, self-corrects for anything the OS/window manager did
// to the window's position on its own between frames (edge-snapping, a
// multi-monitor drag crossing a DPI boundary, etc.) that an anchor computed
// once at drag-start could otherwise silently drift out of sync with.
#include <cmath>

namespace engine {

// The window position glfwSetWindowPos() should be called with this frame,
// given the window's own current on-screen position (as of THIS frame's own
// glfwGetWindowPos() query -- see this header's own top comment for why a
// freshly re-queried current position, not a remembered drag-start anchor,
// is the correct input) and this frame's raw mouse-movement delta (Dear
// ImGui's own io.MouseDelta while a title-bar drag is in progress).
struct WindowPosition {
    int x = 0;
    int y = 0;
};

// mouseDeltaX/Y are `float` (not `int`) because that's the type Dear ImGui's
// own io.MouseDelta carries -- sub-pixel mouse movement is real on a
// high-report-rate mouse/trackpad, and truncating it to int BEFORE this
// function ran would silently drop a whole frame's worth of movement
// whenever a single frame's delta happened to be under one pixel (a real
// loss on a smooth, slow drag, not a hypothetical one -- several
// sub-pixel-per-frame deltas in a row could otherwise sum to a visible,
// truncation-caused lag between the cursor and the window it's supposedly
// dragging). Rounded to the nearest integer pixel exactly once, here, right
// before producing the final result GLFW's own integer-pixel
// glfwSetWindowPos() needs -- not truncated, so a small negative delta (e.g.
// -0.6) rounds to -1, not 0, keeping upward/leftward drags exactly as
// responsive as downward/rightward ones instead of silently biased toward
// "stuck" for one sign of motion.
WindowPosition applyDragDelta(int currentWindowX, int currentWindowY, float mouseDeltaX, float mouseDeltaY);

// Given the window's CURRENT maximized state (GLFW's own live
// glfwGetWindowAttrib(window, GLFW_MAXIMIZED) truth -- see window.hpp's own
// isMaximized() comment for why this is read fresh from GLFW every call
// rather than tracked as a second, separately-maintained bool that could
// drift out of sync with what the OS/window manager actually did), returns
// whether the window should be MAXIMIZED (true) or RESTORED (false) after a
// maximize/restore button click or a title-bar-empty-area double-click --
// simply the opposite of its current state, the ordinary "this button/
// gesture toggles between exactly two states" convention every real OS's own
// title bar already follows. Pulled out as its own named, tested function
// (rather than an inline `!currentlyMaximized` at the one call site) purely
// for the same documentation/contract-pinning reason
// decideCameraCapture()'s own simpler branches are still each individually
// asserted in tests/camera_capture_test.cpp rather than trusted by
// inspection: a future edit that accidentally inverted this one line would
// fail a test immediately instead of silently maximizing on a double-click
// that was supposed to restore, or vice versa.
bool decideMaximizeRestoreToggle(bool currentlyMaximized);

}  // namespace engine

#endif  // ENGINE_WINDOW_CHROME_HPP
