#include "engine/shading_mode.hpp"

namespace engine {

ShadingMode decideNextEditShadingMode(ShadingMode current, bool wireframeButtonClicked, bool solidButtonClicked) {
    // Wireframe wins if both were somehow true this same frame -- see this
    // header's own comment for why that's the deliberately-arbitrary but
    // deterministic tie-break, mirroring decideCameraCapture()'s own
    // "the key/gesture whose effect is the more emphatic one wins" instinct.
    if (wireframeButtonClicked) {
        return current == ShadingMode::kWireframe ? ShadingMode::kRendered : ShadingMode::kWireframe;
    }
    if (solidButtonClicked) {
        return current == ShadingMode::kSolid ? ShadingMode::kRendered : ShadingMode::kSolid;
    }
    // Neither button clicked this frame -- overwhelmingly the common case,
    // every frame the toolbar is simply drawn/hovered without a press.
    return current;
}

ShadingMode effectiveShadingMode(bool physicsRunning, ShadingMode editShadingMode) {
    return physicsRunning ? ShadingMode::kRendered : editShadingMode;
}

}  // namespace engine
