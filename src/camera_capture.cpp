// Phase 16: see camera_capture.hpp's own header comment for the full design.
// This translation unit depends on nothing beyond the header itself -- no
// ecs.hpp, no GLM, no GLFW/GL/ImGui at all -- the same minimal-dependency
// shape asset_drop.cpp/material_override.cpp already establish for the
// identical reason: tests/camera_capture_test.cpp links this file alone.

#include "engine/camera_capture.hpp"

namespace engine {

CameraCaptureDecision decideCameraCapture(bool currentlyCaptured, bool escapeJustPressed, bool enterCaptureRequested) {
    if (currentlyCaptured) {
        // Escape's Phase-16 primary meaning: exit capture, absorb the press
        // (never also quits from this branch -- see this function's own
        // header comment on why "Escape's existing quit behavior becomes
        // secondary" means exactly this).
        if (escapeJustPressed) {
            return CameraCaptureDecision{/*captured=*/false, /*quitRequested=*/false};
        }
        // Already captured: an enterCaptureRequested this frame (not
        // expected in practice -- see the header's own comment on why the
        // real trigger is gated against this exact state, twice over) is a
        // no-op, not a re-trigger of anything. Nothing else changes state
        // while captured.
        return CameraCaptureDecision{/*captured=*/true, /*quitRequested=*/false};
    }

    // Not currently captured: Escape falls back to its original, pre-Phase-16
    // meaning -- quit -- taking precedence over a same-frame
    // enterCaptureRequested (see the header's own comment on why Escape wins
    // that vanishingly unlikely race rather than either being silently
    // dropped).
    if (escapeJustPressed) {
        return CameraCaptureDecision{/*captured=*/false, /*quitRequested=*/true};
    }
    if (enterCaptureRequested) {
        return CameraCaptureDecision{/*captured=*/true, /*quitRequested=*/false};
    }
    // The ordinary, overwhelmingly common per-frame case: nothing happened.
    return CameraCaptureDecision{/*captured=*/false, /*quitRequested=*/false};
}

// Post-review fix (after Phase 18a) -- see this function's own declaration
// in camera_capture.hpp for the full bug this closes. A plain conjunction:
// every one of the four conditions must hold, and being already captured
// short-circuits it before any of them are even consulted.
bool shouldRequestCameraCaptureFromDoubleClick(bool currentlyCaptured, bool anyItemHovered,
                                                bool mouseInsideToolbarRect, bool windowHovered,
                                                bool mouseDoubleClicked) {
    if (currentlyCaptured) {
        return false;
    }
    return !anyItemHovered && !mouseInsideToolbarRect && windowHovered && mouseDoubleClicked;
}

}  // namespace engine
