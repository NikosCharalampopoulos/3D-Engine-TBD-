#ifndef ENGINE_SHADING_MODE_HPP
#define ENGINE_SHADING_MODE_HPP

// Phase 18g: real Wireframe/Solid/Rendered viewport shading modes -- repurposes
// the Viewport toolbar's "lighting"/"texture-mode" buttons (Phase 17c icons),
// which since Phase 18a/18b-era code have actually just toggled
// Application::ssaoDisabled_/ssaoDebugMode_ -- the exact same two flags the
// pre-existing F1 debug panel ("Disable SSAO"/"SSAO debug view" checkboxes)
// already exposes, making the toolbar wiring purely redundant with
// functionality that already existed elsewhere. ssaoDisabled_/ssaoDebugMode_
// themselves, and the F1 checkboxes that drive them, are UNCHANGED by this
// phase -- only the toolbar's own two buttons stop driving them and start
// driving THIS instead.
//
// --- Three states, two buttons ---------------------------------------------
// A real editor-style shading-mode picker is naturally a 3-way exclusive
// choice, but this toolbar only has two button slots to repurpose (adding a
// third would mean a new icon glyph + a wider toolbar, real scope beyond
// "repurpose these two"). The confirmed mapping: neither button active is
// kRendered (today's default, completely unchanged -- full textures/
// lighting/SSAO/SSR/shadows); the toolbar's "texture-mode" button active is
// kSolid; its "lighting" button active is kWireframe. Clicking a button
// activates that mode and deactivates the other -- radio-button-style, not
// two independent toggles -- and clicking the CURRENTLY active one returns
// to kRendered, so all three states are reachable from exactly two buttons.
// decideNextEditShadingMode() below is that whole decision, pulled out pure
// and ImGui-free -- editor_ui.cpp's own renderViewportToolbar() calls it
// once per button click rather than re-deriving this logic inline, the same
// "small, standalone decision, unit-tested in isolation" shape this
// codebase's camera_capture.hpp/gizmo.hpp already establish.

namespace engine {

enum class ShadingMode {
    kRendered,
    kSolid,
    kWireframe,
};

// `current` is the persisted EDIT-mode choice (Application::editShadingMode_,
// see application.hpp's own Phase 18g comment for why this is a distinct
// member from "what's actually rendered this frame"); at most one of
// `wireframeButtonClicked`/`solidButtonClicked` is ever true in a real
// ImGui frame (each maps to exactly one Button() press this same frame) --
// if both were somehow true at once, wireframeButtonClicked wins,
// deterministically, the same "the least surprising rule" tie-break
// camera_capture.hpp's own decideCameraCapture() already picks for its own
// vanishingly-unlikely simultaneous-trigger case.
ShadingMode decideNextEditShadingMode(ShadingMode current, bool wireframeButtonClicked, bool solidButtonClicked);

// Phase 18g confirmed decision: Play mode always forces kRendered, restoring
// the prior Edit-mode choice the instant Play mode ends -- with NO separate
// save/restore bookkeeping anywhere, because `editShadingMode` itself is
// NEVER written by entering/leaving Play mode (only by an Edit-mode toolbar
// click, via decideNextEditShadingMode() above). This one function is that
// whole rule: `physicsRunning ? kRendered : editShadingMode`. Application::
// render() calls this ONCE per frame to get the mode that actually decides
// what gets drawn/skipped that frame; editor_ui.cpp's own toolbar highlight
// logic calls it too, so the two buttons' active/inactive state always
// honestly reflects what's really on screen (kRendered, highlighting
// neither button) even while Play mode is overriding a Wireframe/Solid Edit
// choice underneath.
ShadingMode effectiveShadingMode(bool physicsRunning, ShadingMode editShadingMode);

}  // namespace engine

#endif  // ENGINE_SHADING_MODE_HPP
