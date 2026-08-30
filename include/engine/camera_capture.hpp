#ifndef ENGINE_CAMERA_CAPTURE_HPP
#define ENGINE_CAMERA_CAPTURE_HPP

// Phase 16: fixes a real bug the project owner hit on the actual Windows
// build, not a theoretical one -- the free-fly camera (camera_.
// processMovement()/processMouseInput(), called unconditionally from
// Application::update() ever since Phase 3) read WASD/mouse input EVERY
// frame, regardless of whether the user was actually trying to fly the
// camera at all. Clicking anywhere else in the window, or just moving the
// mouse across the app without intending to look around, moved the camera --
// there was no concept of "the camera is currently captured" anywhere in
// this engine before this phase.
//
// --- The fix, in one sentence -------------------------------------------
// Camera input is now INACTIVE by default; double-clicking inside the
// Viewport panel (editor_ui.cpp's own new BeginDoubleClick-driven detection,
// gated to that one panel -- see this project's own "Deliberately NOT done"
// note below) "captures" it -- hides the OS cursor and starts feeding WASD/
// mouse-look to camera_ again -- and pressing Escape while captured releases
// it, showing the cursor and silencing camera input, without also quitting
// the app the way a bare Escape press already did before this phase (see
// application.cpp's own Phase 16 comment on run() for exactly how that
// precedence is threaded through the pre-existing quit-on-Escape check).
//
// --- Why this one small function exists, on its own, GL/Window/ImGui-free -
// "What should the NEXT capture state be, given the current one and this
// frame's two possible triggers (Escape, and a double-click reported from
// the Viewport panel)" is a pure decision with exactly one right answer for
// any combination of inputs -- and, crucially, it's the one piece of this
// whole feature that has NOTHING to do with a real mouse gesture, unlike
// almost everything else this phase touches (hiding the OS cursor, ImGui's
// own hover suppression, the double-click detection itself). Pulling it out
// here, the identical "small, standalone decision, unit-tested in isolation"
// shape this codebase's own light.hpp (resolveActiveDirectionalLight()),
// material_override.hpp (resolveDiffuseTextureOverride()), and asset_drop.hpp
// (classifyAssetDropPath()) already establish, is what lets tests/
// camera_capture_test.cpp exercise every combination of "currently captured
// or not / Escape pressed or not / a capture-entry requested or not" without
// a live GL context, a real GLFW window, or a Dear ImGui frame -- none of
// which a real double-click gesture is reproducible without anyway (see this
// phase's own README section for exactly what headless verification could
// and could not cover as a result).
//
// --- Why Escape needs two meanings, decided by ONE function, not two ------
// Before this phase, run()'s own `if (input.escapePressed) { quit; }` was
// the entire handling for Escape -- level-triggered and unconditional (see
// input.hpp's own comment on why InputState::escapePressed itself stays
// level-triggered: "close the window" is idempotent whether it fires once or
// every frame the key happens to be held). This phase's brief is explicit
// that Escape must now do ONE OF TWO different things depending on state,
// not stop meaning "quit" -- exit capture if currently captured, and only
// actually quit if pressed while NOT captured. Encoding that as a single
// small function (rather than two separate "if captured, do X; else if
// escape, do Y" branches scattered across run()) is what guarantees the two
// meanings can never both fire off the same physical key-press, or drift out
// of sync with each other as this file gets edited later.
//
// --- Post-review bug fix: this function's OWN input must be EDGE-triggered,
// not level-triggered -- a real held key broke this the first time --------
// The first-pass version of this function took a parameter literally named
// `escapePressed`, fed directly from `InputState::escapePressed`
// (level-triggered -- true for EVERY poll a physical key is held, which for
// a real human key-press spans many frames at this engine's ~16ms/frame
// throttle, not just one). That was a real, reproduced bug, not a
// theoretical one: `decideCameraCapture()` itself has no memory of what
// happened on a PRIOR call -- it only ever sees `currentlyCaptured` (which
// the caller updates between calls) -- so a real held Escape produced
// exactly this sequence: frame N sees `(captured=true, escapePressed=true)`,
// correctly exits capture; frame N+1, with the SAME physical key still
// down, sees `(captured=false, escapePressed=true)` -- indistinguishable
// from a brand-new Escape press with capture already off -- and quits the
// whole app. A user tapping Escape just to get their cursor back would
// close the application instead, making this feature actively worse than
// the always-on-camera bug it exists to fix. The one-frame synthetic press
// `ENGINE_DEBUG_SIMULATE_ESCAPE` used at the time (application.cpp) never
// caught this precisely because it only ever set the flag true for exactly
// one frame -- accidentally behaving LIKE an edge-triggered signal and
// masking the bug it should have caught.
//
// Fixed by renaming this parameter to `escapeJustPressed` and updating its
// CONTRACT, not its logic (every one of this function's own branches was
// already correct in isolation -- see tests/camera_capture_test.cpp, whose
// 8 cases all still pass unmodified): callers must now pass an
// EDGE-triggered signal, true for exactly the one poll a physical Escape
// press transitions from up to down, exactly like `InputState::
// toggleDebugUIPressed`/`InputActionMap::justPressed()` already provide for
// F1 (input_action_map.hpp) -- never `InputState::escapePressed` itself,
// which stays level-triggered and untouched (its own original "quit" use
// case is still idempotent under level-triggering; only THIS dual-meaning
// use needed edge-triggering, so a new, separate `InputState::
// escapeJustPressed` field was added rather than changing what
// `escapePressed` itself means -- see input.hpp's own Phase 16 comment for
// why a second field, not a redefinition of the first). With an
// edge-triggered signal, the exact scenario above self-corrects: frame N
// exits capture on the one true edge; frame N+1 (key still physically down,
// but no NEW transition) sees `escapeJustPressed=false`, so
// `currentlyCaptured=false` combined with no Escape and no capture-request
// correctly falls through to "nothing happened" -- no quit, regardless of
// how many more frames the physical key stays down.
namespace engine {

// This frame's outcome, given `currentlyCaptured` (the capture state as of
// the START of this frame) plus this frame's own two independent triggers.
// `captured` is the state that should hold from this point forward (may be
// unchanged from `currentlyCaptured`); `quitRequested` is true only on the
// one specific case this phase's own brief calls out -- Escape pressed while
// NOT captured -- matching Escape's pre-Phase-16 behavior exactly for a user
// who never captures the camera at all.
struct CameraCaptureDecision {
    bool captured = false;
    bool quitRequested = false;
};

// The whole state machine, in one function. `escapeJustPressed` MUST be
// edge-triggered -- true for exactly the one poll a physical Escape press
// transitions from up to down, never level-triggered/"currently held" --
// see this header's own "Post-review bug fix" comment above for exactly why
// that contract is load-bearing, not a cosmetic naming preference; passing a
// level-triggered signal here reintroduces the exact "held Escape quits the
// app one frame after exiting capture" bug that comment describes.
//   - currentlyCaptured, escapeJustPressed=false, enterCaptureRequested=false:
//     no change either way (the ordinary, overwhelmingly common per-frame
//     case -- nothing happened).
//   - !currentlyCaptured, enterCaptureRequested=true, escapeJustPressed=false:
//     enters capture.
//   - currentlyCaptured, escapeJustPressed=true: EXITS capture and absorbs
//     the Escape press -- quitRequested is false here, the core of this
//     whole phase's "Escape's existing quit behavior becomes secondary"
//     brief.
//   - !currentlyCaptured, escapeJustPressed=true: quits -- Escape's exact
//     pre-Phase-16 behavior, unchanged, for the state most players spend
//     most of their time in before ever double-clicking the Viewport.
//   - currentlyCaptured, enterCaptureRequested=true (defensive only -- the
//     real Viewport double-click check, editor_ui.cpp, is itself gated to
//     never report a request while already captured; Dear ImGui's own
//     ImGuiConfigFlags_NoMouse, set for exactly this state, additionally
//     makes IsWindowHovered()/IsMouseDoubleClicked() unable to fire at all
//     while captured -- see application.cpp's own Phase 16 render()
//     comment): a no-op, stays captured. Nothing about "the user tried to
//     enter capture again while already in it" should ever exit capture or
//     otherwise misbehave, so this is handled explicitly rather than left as
//     an assumed-unreachable case.
//   - Both escapeJustPressed AND enterCaptureRequested true in the same
//     frame, while NOT currently captured (the only state either could
//     plausibly race in, given the gating above): Escape wins -- quits,
//     does not also enter capture. Vanishingly unlikely with a real
//     mouse+keyboard (a double-click and an Escape press landing in the
//     exact same poll), but a pure function has to answer it
//     deterministically either way, and "the key whose job is to stop what's
//     happening always wins" is the least surprising rule to pick.
CameraCaptureDecision decideCameraCapture(bool currentlyCaptured, bool escapeJustPressed,
                                           bool enterCaptureRequested);

// Post-review fix (after Phase 18a): the double-click-to-capture guard in
// editor_ui.cpp's own renderDockspaceShell() decided `enterCaptureRequested`
// (the parameter immediately above) via a chain of Dear ImGui hover queries
// -- `!ImGui::IsAnyItemHovered() && ImGui::IsWindowHovered() &&
// ImGui::IsMouseDoubleClicked(...)`. A review of Phase 18a's floating
// overlay found that chain incomplete: `IsAnyItemHovered()` only excludes a
// double-click landing on one of the toolbar's six `ImGui::Button()` item
// rects -- it says nothing about `renderViewportToolbarOverlay()`'s own
// translucent BACKGROUND rectangle (`bgMin`/`bgMax` in that function), which
// covers the rounded-corner margin around the buttons and the small
// `ImGui::SameLine()` gaps between them too. A double-click landing in one
// of those gaps visually lands on toolbar chrome but was, before this fix,
// falling through and requesting camera capture as if it had landed on the
// empty 3D viewport -- always latent in Phase 17c's guard shape, but Phase
// 18a's overlay (the toolbar now sits directly on top of the rendered
// image, rather than in a row above it) is what makes it reachable.
//
// This function is the fix's own decision, pulled out pure and ImGui-free --
// same "small, standalone decision, unit-tested in isolation" shape this
// header's own comment above already cites for decideCameraCapture() itself
// -- so the exact combination that was broken can be exhaustively covered by
// tests/camera_capture_test.cpp without a live ImGui frame or a real
// double-click gesture, neither reproducible in this project's headless
// Xvfb environment.
//
// `anyItemHovered` mirrors `ImGui::IsAnyItemHovered()`; `mouseInsideToolbarRect`
// is the NEW condition this fix adds, mirroring
// `ImGui::IsMouseHoveringRect(bgMin, bgMax)` tested against the toolbar
// overlay's own background rectangle; `windowHovered` mirrors
// `ImGui::IsWindowHovered()`; `mouseDoubleClicked` mirrors
// `ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)`. All four must hold
// (item not hovered, background rect not hovered, window hovered, an actual
// double-click this frame) for a double-click to be treated as a request to
// enter camera capture; `currentlyCaptured` short-circuits the whole thing,
// matching the real guard's own leading `!cameraCaptured` check (requesting
// capture again while already captured is meaningless -- see
// decideCameraCapture()'s own handling of a redundant `enterCaptureRequested`
// above for why that case is a defensive no-op rather than assumed
// unreachable).
bool shouldRequestCameraCaptureFromDoubleClick(bool currentlyCaptured, bool anyItemHovered,
                                                bool mouseInsideToolbarRect, bool windowHovered,
                                                bool mouseDoubleClicked);

}  // namespace engine

#endif  // ENGINE_CAMERA_CAPTURE_HPP
