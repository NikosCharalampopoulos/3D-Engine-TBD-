// Phase 15g: tests engine::classifyAssetDropPath()/modelBaseNameFromAssetPath()
// (src/asset_drop.cpp) in isolation -- same "plain executable, links only the
// pure logic file it's testing" shape as asset_browser_test above.
// asset_drop.cpp depends on nothing beyond <string>, so this needs no live GL
// context/GPU, no Dear ImGui frame, no ecs.hpp -- just the standard library.
//
// This is exactly "given a dropped path, is it a model or texture category,
// and what should a freshly created entity be called" -- the one pair of
// decisions asset_drop.hpp's own header comment calls out as the whole pure
// half of this phase's design -- in isolation from Application::
// handleViewportAssetDrop()/spawnEntityFromDroppedModel() actually acting on
// either answer.

#include "engine/asset_drop.hpp"

#include <cassert>
#include <iostream>

int main() {
    using engine::AssetDropCategory;
    using engine::classifyAssetDropPath;
    using engine::modelBaseNameFromAssetPath;

    // --- classifyAssetDropPath() ------------------------------------------

    // The ordinary, expected case for each category -- a real file directly
    // under assets/models/ or assets/textures/.
    assert(classifyAssetDropPath("assets/models/falling_cube.obj") == AssetDropCategory::kModel);
    assert(classifyAssetDropPath("assets/textures/checker.png") == AssetDropCategory::kTexture);

    // Nested a level deeper -- assets/textures/skybox/, assets/textures/hdri/
    // (this project's own real subfolder structure, asset_browser.hpp's own
    // header comment) -- still classifies by top-level folder, not depth.
    assert(classifyAssetDropPath("assets/textures/skybox/right.png") == AssetDropCategory::kTexture);
    assert(classifyAssetDropPath("assets/models/nested/extra.obj") == AssetDropCategory::kModel);

    // Classification is by FOLDER, not file extension -- a model's own
    // sibling .mtl file (present in this project's own assets/models/, per
    // asset_browser.hpp's own "4 .obj+.mtl pairs" comment) still classifies
    // as kModel; whether resources_.getModel() can actually load it is a
    // separate, later concern (Application::spawnEntityFromDroppedModel()'s
    // own try/catch, application.cpp), not this function's job to predict.
    assert(classifyAssetDropPath("assets/models/falling_cube.mtl") == AssetDropCategory::kModel);

    // The bare top-level category folder itself, dropped directly rather
    // than a file beneath it -- has no trailing '/', so it does NOT match
    // either prefix and correctly falls to kUnrecognized rather than being
    // treated as some kind of degenerate model/texture path.
    assert(classifyAssetDropPath("assets/models") == AssetDropCategory::kUnrecognized);
    assert(classifyAssetDropPath("assets/textures") == AssetDropCategory::kUnrecognized);

    // A real asset path, but under a category asset_browser.hpp's own
    // allowlist deliberately never makes browsable/draggable in the first
    // place (assets/scenes/, assets/shaders/ -- see that header's own
    // comment) -- reachable here only defensively (nothing in this engine
    // can actually construct a drag payload for either), but still must
    // classify as kUnrecognized, not silently misfire as a model/texture.
    assert(classifyAssetDropPath("assets/scenes/default.json") == AssetDropCategory::kUnrecognized);
    assert(classifyAssetDropPath("assets/shaders/basic.vert") == AssetDropCategory::kUnrecognized);

    // Malformed/degenerate inputs -- empty, or not even rooted at "assets/"
    // at all -- must not crash and must classify as kUnrecognized.
    assert(classifyAssetDropPath("") == AssetDropCategory::kUnrecognized);
    assert(classifyAssetDropPath("models/falling_cube.obj") == AssetDropCategory::kUnrecognized);
    assert(classifyAssetDropPath("assets/") == AssetDropCategory::kUnrecognized);

    // --- modelBaseNameFromAssetPath() --------------------------------------

    // The ordinary case: strip the directory and the extension.
    assert(modelBaseNameFromAssetPath("assets/models/falling_cube.obj") == "falling_cube");
    assert(modelBaseNameFromAssetPath("assets/models/sphere.obj") == "sphere");

    // Nested a level deeper -- only the FILENAME's own stem matters, not any
    // part of the directory path leading up to it.
    assert(modelBaseNameFromAssetPath("assets/models/nested/extra.obj") == "extra");

    // No extension at all -- the whole filename is the stem.
    assert(modelBaseNameFromAssetPath("assets/models/noext") == "noext");

    // No directory component at all -- still extracts correctly.
    assert(modelBaseNameFromAssetPath("plain.obj") == "plain");

    // Pathological inputs that would otherwise produce an empty string (a
    // dotfile-shaped ".obj" with nothing before the extension, or a
    // completely empty path) fall back to the documented "Model" default
    // rather than handing the caller an empty NameComponent string.
    assert(modelBaseNameFromAssetPath("assets/models/.obj") == "Model");
    assert(modelBaseNameFromAssetPath("") == "Model");
    assert(modelBaseNameFromAssetPath("assets/models/") == "Model");

    std::cout << "asset_drop_test: all checks passed" << std::endl;
    return 0;
}
