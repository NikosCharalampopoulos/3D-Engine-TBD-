// Phase 18g's own test: exercises engine::decideNextEditShadingMode()/
// engine::effectiveShadingMode() (src/shading_mode.cpp) in isolation -- same
// "plain executable, links only the pure logic file it's testing" shape as
// camera_capture_test/gizmo_test above. shading_mode.cpp depends on nothing
// beyond its own header (no ecs.hpp, no GLM, no GL/ImGui at all), so this
// needs no live GL context/GPU/Dear ImGui frame whatsoever.

#include "engine/shading_mode.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int failures = 0;

const char* modeName(engine::ShadingMode mode) {
    switch (mode) {
        case engine::ShadingMode::kRendered:
            return "kRendered";
        case engine::ShadingMode::kSolid:
            return "kSolid";
        case engine::ShadingMode::kWireframe:
            return "kWireframe";
    }
    return "?";
}

void expectMode(engine::ShadingMode actual, engine::ShadingMode expected, const std::string& what) {
    if (actual != expected) {
        std::fprintf(stderr, "FAIL: %s (expected %s, got %s)\n", what.c_str(), modeName(expected), modeName(actual));
        ++failures;
    }
}

}  // namespace

int main() {
    using engine::decideNextEditShadingMode;
    using engine::effectiveShadingMode;
    using engine::ShadingMode;

    // --- decideNextEditShadingMode(): neither button clicked -- no change,
    // the overwhelmingly common per-frame case ------------------------------
    expectMode(decideNextEditShadingMode(ShadingMode::kRendered, false, false), ShadingMode::kRendered,
               "neither button clicked, starting at kRendered: stays kRendered");
    expectMode(decideNextEditShadingMode(ShadingMode::kSolid, false, false), ShadingMode::kSolid,
               "neither button clicked, starting at kSolid: stays kSolid");
    expectMode(decideNextEditShadingMode(ShadingMode::kWireframe, false, false), ShadingMode::kWireframe,
               "neither button clicked, starting at kWireframe: stays kWireframe");

    // --- the "lighting" button (wireframe) toggles kWireframe on from any
    // other mode, and back to kRendered when it's already active -----------
    expectMode(decideNextEditShadingMode(ShadingMode::kRendered, true, false), ShadingMode::kWireframe,
               "wireframe button clicked from kRendered: activates kWireframe");
    expectMode(decideNextEditShadingMode(ShadingMode::kSolid, true, false), ShadingMode::kWireframe,
               "wireframe button clicked from kSolid: switches straight to kWireframe (radio-button style)");
    expectMode(decideNextEditShadingMode(ShadingMode::kWireframe, true, false), ShadingMode::kRendered,
               "wireframe button clicked while already kWireframe: returns to kRendered");

    // --- the "texture-mode" button (solid) mirrors the same shape ----------
    expectMode(decideNextEditShadingMode(ShadingMode::kRendered, false, true), ShadingMode::kSolid,
               "solid button clicked from kRendered: activates kSolid");
    expectMode(decideNextEditShadingMode(ShadingMode::kWireframe, false, true), ShadingMode::kSolid,
               "solid button clicked from kWireframe: switches straight to kSolid (radio-button style)");
    expectMode(decideNextEditShadingMode(ShadingMode::kSolid, false, true), ShadingMode::kRendered,
               "solid button clicked while already kSolid: returns to kRendered");

    // --- both clicked in the same call (not reachable from two real
    // ImGui::Button() presses in one frame, but this pure function must
    // still answer deterministically): wireframe wins, per this header's own
    // documented tie-break --------------------------------------------------
    expectMode(decideNextEditShadingMode(ShadingMode::kRendered, true, true), ShadingMode::kWireframe,
               "both buttons clicked at once: wireframe takes precedence");

    // --- effectiveShadingMode(): Edit mode (physicsRunning=false) reads
    // editShadingMode directly, unmodified, for every one of the three
    // states -----------------------------------------------------------
    expectMode(effectiveShadingMode(false, ShadingMode::kRendered), ShadingMode::kRendered,
               "Edit mode: effective mode matches editShadingMode (kRendered)");
    expectMode(effectiveShadingMode(false, ShadingMode::kSolid), ShadingMode::kSolid,
               "Edit mode: effective mode matches editShadingMode (kSolid)");
    expectMode(effectiveShadingMode(false, ShadingMode::kWireframe), ShadingMode::kWireframe,
               "Edit mode: effective mode matches editShadingMode (kWireframe)");

    // --- effectiveShadingMode(): Play mode (physicsRunning=true) always
    // forces kRendered, regardless of what editShadingMode currently holds --
    // the whole "Play always looks Rendered, Edit's choice comes back
    // untouched on Stop" rule, in one function ------------------------------
    expectMode(effectiveShadingMode(true, ShadingMode::kRendered), ShadingMode::kRendered,
               "Play mode: effective mode is kRendered when editShadingMode was already kRendered");
    expectMode(effectiveShadingMode(true, ShadingMode::kSolid), ShadingMode::kRendered,
               "Play mode: effective mode is kRendered even though editShadingMode is kSolid");
    expectMode(effectiveShadingMode(true, ShadingMode::kWireframe), ShadingMode::kRendered,
               "Play mode: effective mode is kRendered even though editShadingMode is kWireframe");

    if (failures == 0) {
        std::printf("shading_mode_test: all checks passed\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "shading_mode_test: %d check(s) failed\n", failures);
    return EXIT_FAILURE;
}
