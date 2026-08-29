// Phase 17b: tests engine::sceneNodeIconGlyph()/assetNodeIconGlyph()
// (src/editor_icons.cpp) in isolation -- same "plain executable, links only
// the pure logic file it's testing" shape asset_drop_test/light_test above.
// editor_icons.cpp depends on nothing beyond editor_icons.hpp itself (which
// pulls in asset_drop.hpp for AssetDropCategory -- no ecs.hpp, no GLM, no
// GL/ImGui at all), so this links src/asset_drop.cpp alongside it (for
// classifyAssetDropPath()'s own AssetDropCategory enum values, used as
// literal test inputs below -- nothing here calls that function itself) and
// needs no live GL context/GPU/Dear ImGui frame/font atlas whatsoever.
//
// This is exactly "given a Scene Hierarchy row's component flags, or an
// Assets Browser row's directory-ness + category, which one codepoint
// should its icon draw" -- the whole pure half of this phase's design --
// in isolation from the real ImGui font-atlas merge/row-drawing code
// (editor_ui.cpp's own EditorUI constructor/renderSceneTreeNode()/
// renderAssetTreeNode()) actually acting on either answer.

#include "engine/editor_icons.hpp"

#include <cassert>
#include <iostream>

int main() {
    using engine::AssetDropCategory;
    using engine::assetNodeIconGlyph;
    using engine::sceneNodeIconGlyph;

    // --- sceneNodeIconGlyph() -----------------------------------------------

    // The ordinary, single-flag case for each of the four component kinds
    // this phase actually distinguishes -- exactly what every real
    // Create-menu entity (Phase 14f/15a/15b/15c) produces.
    assert(sceneNodeIconGlyph(/*hasModel=*/true, false, false, false) == engine::kIconMesh);
    assert(sceneNodeIconGlyph(false, /*hasPointLight=*/true, false, false) == engine::kIconPointLight);
    assert(sceneNodeIconGlyph(false, false, /*hasDirectionalLight=*/true, false) ==
           engine::kIconDirectionalLight);
    assert(sceneNodeIconGlyph(false, false, false, /*hasCamera=*/true) == engine::kIconCamera);

    // No flags at all -- Phase 14f's "Empty" Create-menu kind, or any
    // organizational grouping node -- falls back to the generic folder icon.
    assert(sceneNodeIconGlyph(false, false, false, false) == engine::kIconFolder);

    // Precedence when more than one flag is set at once (not reachable
    // through any Create-menu path today, but not schema-forbidden either
    // -- see this function's own header comment): Model beats every
    // light/camera flag...
    assert(sceneNodeIconGlyph(true, true, true, true) == engine::kIconMesh);
    assert(sceneNodeIconGlyph(true, false, false, true) == engine::kIconMesh);
    // ...a light flag beats Camera...
    assert(sceneNodeIconGlyph(false, false, true, true) == engine::kIconDirectionalLight);
    assert(sceneNodeIconGlyph(false, true, false, true) == engine::kIconPointLight);
    // ...and PointLight is checked before DirectionalLight when both are set.
    assert(sceneNodeIconGlyph(false, true, true, false) == engine::kIconPointLight);

    // --- assetNodeIconGlyph() -----------------------------------------------

    // Every directory row gets the folder icon, regardless of what category
    // its own (irrelevant, in this case) AssetDropCategory argument names --
    // isDirectory wins outright, per this function's own header comment.
    assert(assetNodeIconGlyph(/*isDirectory=*/true, AssetDropCategory::kModel) == engine::kIconFolder);
    assert(assetNodeIconGlyph(true, AssetDropCategory::kTexture) == engine::kIconFolder);
    assert(assetNodeIconGlyph(true, AssetDropCategory::kUnrecognized) == engine::kIconFolder);

    // A file row's icon follows its own AssetDropCategory: kModel -> the
    // same mesh/cube glyph a Scene row with a ModelComponent gets (both
    // represent "a real 3D model," Scene-side or Assets-side), kTexture ->
    // the image glyph.
    assert(assetNodeIconGlyph(/*isDirectory=*/false, AssetDropCategory::kModel) == engine::kIconMesh);
    assert(assetNodeIconGlyph(false, AssetDropCategory::kTexture) == engine::kIconTexture);

    // kUnrecognized on a FILE row is unreachable through any path
    // buildAssetTree() actually walks (this function's own header comment),
    // but is still defined behavior -- the same safe folder-icon fallback a
    // directory row gets, rather than an assert/crash on an input that
    // should be impossible.
    assert(assetNodeIconGlyph(false, AssetDropCategory::kUnrecognized) == engine::kIconFolder);

    std::cout << "editor_icons_test: all checks passed" << std::endl;
    return 0;
}
