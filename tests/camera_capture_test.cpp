// Phase 16: tests engine::decideCameraCapture() (src/camera_capture.cpp) in
// isolation -- same "plain executable, links only the pure logic file it's
// testing" shape as asset_drop_test/material_override_test above.
// camera_capture.cpp depends on nothing beyond its own header, so this needs
// no live GL context/GPU, no real GLFW window, and no Dear ImGui frame --
// none of which a real double-click-to-capture gesture is reproducible
// without anyway (see README.md's own Phase 16 section for what headless
// verification could and couldn't cover for the rest of this feature as a
// result). This is the one part of the whole feature that's pure state
// transition logic, exhaustively covered here.
//
// Post-review addition: the first 8 cases below test decideCameraCapture()'s
// own pure logic assuming an already-EDGE-triggered `escapeJustPressed`
// input (every one of them was already correct in isolation, confirmed by
// reviewer) -- they do NOT, by themselves, prove the real app never quits
// after a real, physically-HELD Escape press exits capture, because that
// bug lived in what got PASSED to this function across consecutive frames,
// not in this function itself. The "held key across consecutive polls"
// block that follows them closes that gap: it drives this engine's own
// REAL edge-detection machinery (engine::InputActionMap, same class
// Application::run() actually uses every frame via pollInputState(),
// input.cpp) with a synthetic but otherwise ordinary multi-poll "key held
// down" sequence, and feeds ITS real justPressed() output into
// decideCameraCapture() -- an integration test, not just a second unit test
// of the same pure function, since the regression was specifically about how
// the two pieces are wired together across frames.
//
// Post-review addition (after Phase 18a): the final block at the bottom of
// main() tests a second, separate pure function in this same file --
// engine::shouldRequestCameraCaptureFromDoubleClick() -- covering a
// different bug entirely (the Viewport toolbar overlay's own background
// rectangle wasn't excluded from the double-click guard; see that
// function's own camera_capture.hpp comment). It shares this file rather
// than getting one of its own because it is thematically the other half of
// this exact same guard's decision, not because it's related to Escape/quit
// at all.

#include "engine/camera_capture.hpp"
#include "engine/input_action_map.hpp"

#include <GLFW/glfw3.h>

#include <cassert>
#include <iostream>

int main() {
    using engine::CameraCaptureDecision;
    using engine::decideCameraCapture;

    // --- Not captured, the ordinary steady states -----------------------

    // Nothing happened this frame: stays uncaptured, no quit. The
    // overwhelmingly common per-frame case.
    {
        const CameraCaptureDecision d = decideCameraCapture(/*currentlyCaptured=*/false, /*escapeJustPressed=*/false,
                                                              /*enterCaptureRequested=*/false);
        assert(!d.captured);
        assert(!d.quitRequested);
    }

    // Escape pressed while NOT captured: quits, matching Escape's exact
    // pre-Phase-16 behavior for a user who never captures the camera at all.
    {
        const CameraCaptureDecision d = decideCameraCapture(/*currentlyCaptured=*/false, /*escapeJustPressed=*/true,
                                                              /*enterCaptureRequested=*/false);
        assert(!d.captured);
        assert(d.quitRequested);
    }

    // A double-click-in-viewport signal while NOT captured: enters capture,
    // does not quit.
    {
        const CameraCaptureDecision d = decideCameraCapture(/*currentlyCaptured=*/false, /*escapeJustPressed=*/false,
                                                              /*enterCaptureRequested=*/true);
        assert(d.captured);
        assert(!d.quitRequested);
    }

    // Both Escape AND an enter-capture request in the same frame, while not
    // captured -- Escape wins (quits, does not also enter capture). See the
    // header's own comment on why this precedence, not either alternative.
    {
        const CameraCaptureDecision d = decideCameraCapture(/*currentlyCaptured=*/false, /*escapeJustPressed=*/true,
                                                              /*enterCaptureRequested=*/true);
        assert(!d.captured);
        assert(d.quitRequested);
    }

    // --- Currently captured -----------------------------------------------

    // Nothing happened this frame: stays captured, no quit.
    {
        const CameraCaptureDecision d = decideCameraCapture(/*currentlyCaptured=*/true, /*escapeJustPressed=*/false,
                                                              /*enterCaptureRequested=*/false);
        assert(d.captured);
        assert(!d.quitRequested);
    }

    // Escape pressed while captured: EXITS capture, absorbs the press --
    // this is the whole point of this phase's precedence rule. Must NOT
    // quit -- a captured user pressing Escape expects to get their cursor
    // back, not for the app to close out from under them.
    {
        const CameraCaptureDecision d = decideCameraCapture(/*currentlyCaptured=*/true, /*escapeJustPressed=*/true,
                                                              /*enterCaptureRequested=*/false);
        assert(!d.captured);
        assert(!d.quitRequested);
    }

    // A defensive case that should never occur in practice (the real
    // Viewport double-click trigger is gated to only ever fire while NOT
    // captured -- see editor_ui.cpp's own Phase 16 comment) but the state
    // machine still has to answer deterministically: enterCaptureRequested
    // while ALREADY captured is a no-op, stays captured, no quit -- not a
    // toggle back out.
    {
        const CameraCaptureDecision d = decideCameraCapture(/*currentlyCaptured=*/true, /*escapeJustPressed=*/false,
                                                              /*enterCaptureRequested=*/true);
        assert(d.captured);
        assert(!d.quitRequested);
    }

    // Both Escape AND an (unexpected) enter-capture request while captured:
    // Escape's exit-capture meaning still takes priority -- exits, no quit,
    // exactly like the plain "Escape while captured" case above. The
    // (already defensive-only) enterCaptureRequested is simply irrelevant
    // once currentlyCaptured is true, regardless of escapeJustPressed.
    {
        const CameraCaptureDecision d = decideCameraCapture(/*currentlyCaptured=*/true, /*escapeJustPressed=*/true,
                                                              /*enterCaptureRequested=*/true);
        assert(!d.captured);
        assert(!d.quitRequested);
    }

    // --- Post-review regression: a REAL held key across many consecutive
    // polls must never quit the app after exiting capture ------------------
    // Reproduces the reported bug's exact mechanics using this engine's own
    // REAL edge-detection (engine::InputActionMap, input_action_map.hpp) --
    // its default constructor already binds InputAction::Quit to
    // GLFW_KEY_ESCAPE (input_action_map.cpp), the identical binding a real
    // run uses, so this is genuinely the same machinery, not a stand-in for
    // it.
    {
        using engine::InputAction;
        using engine::InputActionMap;

        InputActionMap actionMap;
        bool physicalEscapeHeld = false;
        // Mirrors exactly what input.cpp's pollInputState() does each frame:
        // re-sample via update(), then read the edge-triggered result --
        // just with a synthetic isKeyDown query standing in for a real
        // window_.isKeyPressed(), the same substitution
        // tests/input_action_map_test.cpp's own fake KeyDownQuery already
        // establishes for testing this exact class without a live window.
        const auto pollEscapeJustPressed = [&]() {
            actionMap.update([&](int key) { return key == GLFW_KEY_ESCAPE && physicalEscapeHeld; });
            return actionMap.justPressed(InputAction::Quit);
        };

        // Start captured, mirroring ENGINE_DEBUG_FORCE_CAMERA_CAPTURE's own
        // headless proof (README.md's Phase 16 Verify section).
        bool captured = true;

        // Poll 1: the physical key goes down for the very first time --
        // a real edge, so justPressed() must fire exactly here.
        physicalEscapeHeld = true;
        {
            const bool escapeJustPressed = pollEscapeJustPressed();
            assert(escapeJustPressed);
            const CameraCaptureDecision d =
                decideCameraCapture(captured, escapeJustPressed, /*enterCaptureRequested=*/false);
            assert(!d.captured);
            assert(!d.quitRequested);  // exits capture, does NOT quit.
            captured = d.captured;
        }

        // Polls 2-5: the SAME physical key-press is STILL held down --
        // exactly what a real human tap-and-release spans (many more than
        // one ~16ms poll at this engine's frame throttle). This is the
        // reported bug's exact reproduction: a level-triggered signal here
        // would read true on every one of these polls too, and
        // decideCameraCapture() would quit on the very first of them, since
        // `captured` had already flipped to false above -- indistinguishable
        // from a brand-new press. The real edge-triggered justPressed()
        // must NOT re-fire while the key never actually went back up.
        for (int poll = 0; poll < 4; ++poll) {
            const bool escapeJustPressed = pollEscapeJustPressed();
            assert(!escapeJustPressed);
            const CameraCaptureDecision d =
                decideCameraCapture(captured, escapeJustPressed, /*enterCaptureRequested=*/false);
            assert(!d.captured);       // stays uncaptured -- nothing re-entered it.
            assert(!d.quitRequested);  // THE regression this test exists to catch.
            captured = d.captured;
        }

        // The key is released, then pressed again -- confirms this fix
        // doesn't silently break Escape's ORIGINAL "quit" meaning: a
        // genuinely NEW press, while uncaptured, still quits.
        physicalEscapeHeld = false;
        assert(!pollEscapeJustPressed());
        physicalEscapeHeld = true;
        {
            const bool escapeJustPressed = pollEscapeJustPressed();
            assert(escapeJustPressed);
            const CameraCaptureDecision d =
                decideCameraCapture(captured, escapeJustPressed, /*enterCaptureRequested=*/false);
            assert(!d.captured);
            assert(d.quitRequested);
        }
    }

    // --- Post-review fix: shouldRequestCameraCaptureFromDoubleClick() -----
    // (camera_capture.hpp) -- the pure decision extracted from
    // editor_ui.cpp's own double-click-to-capture guard after a review found
    // it incomplete for Phase 18a's floating toolbar overlay: the guard's
    // pre-existing `!ImGui::IsAnyItemHovered()` only excludes a double-click
    // landing on one of the toolbar's six BUTTON item rects, not
    // renderViewportToolbarOverlay()'s own translucent BACKGROUND rectangle
    // (the rounded-corner margin and the small ImGui::SameLine() gaps
    // between buttons) -- see camera_capture.hpp's own comment on this
    // function for the full bug. Every one of the five inputs is exercised
    // independently below, the same "one case per condition, plus the
    // combinations that matter" shape the first 8 cases above already use
    // for decideCameraCapture().
    {
        using engine::shouldRequestCameraCaptureFromDoubleClick;

        // THE bug this function exists to fix: not on a button
        // (anyItemHovered=false) but inside the toolbar's own background
        // rect (mouseInsideToolbarRect=true) -- must NOT request capture.
        // The old inline condition (`!IsAnyItemHovered() && IsWindowHovered()
        // && IsMouseDoubleClicked(...)`, with no rect check at all) would
        // have incorrectly returned true here.
        assert(!shouldRequestCameraCaptureFromDoubleClick(
            /*currentlyCaptured=*/false, /*anyItemHovered=*/false, /*mouseInsideToolbarRect=*/true,
            /*windowHovered=*/true, /*mouseDoubleClicked=*/true));

        // The working case this fix must NOT regress: a double-click
        // squarely on a button (anyItemHovered=true; also, by construction,
        // inside the background rect, since every button sits inside it) --
        // still correctly excluded, same as it always was.
        assert(!shouldRequestCameraCaptureFromDoubleClick(
            /*currentlyCaptured=*/false, /*anyItemHovered=*/true, /*mouseInsideToolbarRect=*/true,
            /*windowHovered=*/true, /*mouseDoubleClicked=*/true));

        // The other working case this fix must NOT regress: a double-click
        // well outside the toolbar's footprint entirely, on the plain empty
        // 3D viewport -- still correctly requests capture.
        assert(shouldRequestCameraCaptureFromDoubleClick(
            /*currentlyCaptured=*/false, /*anyItemHovered=*/false, /*mouseInsideToolbarRect=*/false,
            /*windowHovered=*/true, /*mouseDoubleClicked=*/true));

        // Not hovering this window at all (e.g. the double-click actually
        // landed on a different docked panel that merely overlaps on
        // screen): never requests capture, regardless of the toolbar rect.
        assert(!shouldRequestCameraCaptureFromDoubleClick(
            /*currentlyCaptured=*/false, /*anyItemHovered=*/false, /*mouseInsideToolbarRect=*/false,
            /*windowHovered=*/false, /*mouseDoubleClicked=*/true));

        // Hovering the empty viewport, but not an actual double-click this
        // frame (e.g. a single click, or no click at all): never requests
        // capture.
        assert(!shouldRequestCameraCaptureFromDoubleClick(
            /*currentlyCaptured=*/false, /*anyItemHovered=*/false, /*mouseInsideToolbarRect=*/false,
            /*windowHovered=*/true, /*mouseDoubleClicked=*/false));

        // Already captured short-circuits everything else -- even a
        // double-click on the plain empty viewport, which would otherwise
        // satisfy every other condition, must not re-request capture while
        // already captured (mirrors decideCameraCapture()'s own defensive
        // handling of a redundant enterCaptureRequested while captured,
        // exercised above).
        assert(!shouldRequestCameraCaptureFromDoubleClick(
            /*currentlyCaptured=*/true, /*anyItemHovered=*/false, /*mouseInsideToolbarRect=*/false,
            /*windowHovered=*/true, /*mouseDoubleClicked=*/true));
    }

    std::cout << "camera_capture_test: all checks passed" << std::endl;
    return 0;
}
