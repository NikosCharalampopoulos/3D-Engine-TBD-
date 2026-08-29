// Phase 17b: see editor_icons.hpp's own header comment for the full design.
// This translation unit depends on nothing beyond its own header (which
// itself depends only on <cstdint> and asset_drop.hpp -- no ecs.hpp, no
// GLM, no GL/ImGui at all) -- the same minimal-dependency shape
// asset_drop.cpp already has for the identical reason: tests/
// editor_icons_test.cpp links this file (plus asset_drop.cpp, for
// AssetDropCategory) alone.
//
// Phase 17c: toolbarButtonIconGlyph() (bottom of this file) is the identical
// shape, added for the Viewport toolbar row's own icon selection -- still no
// new dependency (ToolbarButton is defined in this same header).

#include "engine/editor_icons.hpp"

namespace engine {

char32_t sceneNodeIconGlyph(bool hasModel, bool hasPointLight, bool hasDirectionalLight, bool hasCamera) {
    // See this function's own header comment for the precedence reasoning
    // -- Model, then the two lights (Point before Directional, an
    // arbitrary but documented tie-break), then Camera, then the generic
    // folder fallback.
    if (hasModel) {
        return kIconMesh;
    }
    if (hasPointLight) {
        return kIconPointLight;
    }
    if (hasDirectionalLight) {
        return kIconDirectionalLight;
    }
    if (hasCamera) {
        return kIconCamera;
    }
    return kIconFolder;
}

char32_t assetNodeIconGlyph(bool isDirectory, AssetDropCategory category) {
    if (isDirectory) {
        return kIconFolder;
    }
    // No `default:` case -- see this function's own header comment on why
    // kUnrecognized is handled explicitly rather than falling through a
    // default, and left the compiler's own -Wswitch exhaustiveness check
    // (this project builds with -Wextra) able to flag a future
    // AssetDropCategory enumerator this function forgot to handle.
    switch (category) {
        case AssetDropCategory::kModel:
            return kIconMesh;
        case AssetDropCategory::kTexture:
            return kIconTexture;
        case AssetDropCategory::kUnrecognized:
            return kIconFolder;
    }
    return kIconFolder;  // Unreachable: every enumerator is handled above.
}

char32_t toolbarButtonIconGlyph(ToolbarButton button) {
    // See this function's own header comment for why kLighting/kTextureMode
    // return the SAME constants a Scene/Assets tree row already draws,
    // rather than a second, newly-vendored glyph.
    switch (button) {
        case ToolbarButton::kGrid:
            return kIconGrid;
        case ToolbarButton::kLighting:
            return kIconDirectionalLight;
        case ToolbarButton::kTextureMode:
            return kIconTexture;
        case ToolbarButton::kUndo:
            return kIconUndo;
        case ToolbarButton::kPlay:
            return kIconPlay;
        case ToolbarButton::kPause:
            return kIconPause;
    }
    return kIconGrid;  // Unreachable: every enumerator is handled above.
}

}  // namespace engine
