#ifndef ENGINE_EDITOR_ICONS_HPP
#define ENGINE_EDITOR_ICONS_HPP

// Phase 17b: the first item in a new "Phase 17: visual design" arc (built
// out of the arc's own lettered order -- see README.md's own Phase 17b
// section for why 17a, a base ImGui color/rounding theme pass, hasn't
// landed yet and isn't assumed by anything here). Closes the gap
// editor_ui.cpp's own renderSceneTreeNode() (Phase 14d) has documented,
// verbatim, since it was written: every Scene Hierarchy/Assets Browser row
// has been plain, icon-less text because no icon font existed anywhere in
// this engine's ImGui font atlas. This header is the pure half of finally
// closing that gap.
//
// --- What lives here, and why it's pulled out of editor_ui.cpp -----------
// Two small decision functions, both pure and GL/ImGui-free with exactly
// one right answer given their inputs -- the same "small, standalone
// decision, unit-tested in isolation" shape light.hpp's
// resolveActiveDirectionalLight(), material_override.hpp's
// resolveDiffuseTextureOverride(), asset_drop.hpp's
// classifyAssetDropPath(), and camera_capture.hpp's decideCameraCapture()
// already establish:
//   - the six Unicode codepoints this engine's icon font atlas actually
//     merges in (the real ImGui font-atlas merge itself -- AddFontFromFileTTF()
//     + ImFontConfig::MergeMode -- happens in editor_ui.cpp's own EditorUI
//     constructor, which has no business appearing in this GL/ImGui-free
//     header);
//   - sceneNodeIconGlyph()/assetNodeIconGlyph() below: given a Scene
//     Hierarchy row's own component flags, or an Assets Browser row's own
//     directory-ness + asset_drop.hpp category, which ONE of those six
//     codepoints that row's icon should draw.
// Both functions are exercised by tests/editor_icons_test.cpp with no live
// GL context, Dear ImGui frame, or font atlas at all -- plain switch-shaped
// logic over bools/an enum.
//
// --- The actual font, and why these six specific codepoints --------------
// Font Awesome Free 6.7.2's Solid style (fa-solid-900.ttf) -- OFL 1.1
// licensed (the "Fonts" section of that release's own LICENSE.txt; Font
// Awesome's icon ARTWORK is separately CC BY 4.0, irrelevant here since
// only the font FILE -- glyph outlines, not SVGs -- is vendored), a
// permissive license this project's own established dependency bar (MIT/
// OFL/public-domain-or-similar, the same standard CMakeLists.txt's own
// GLFW/GLM/Assimp/Dear ImGui/nlohmann_json fetches and external/stb's
// vendored stb_image.h already meet) is comfortable with. See README.md's
// own Phase 17b section for the exact subsetting/provenance writeup --
// this project vendors a hand-subsetted ~2 KB slice of that font
// containing ONLY these six glyphs, not the full ~420 KB release file.
//
// The six codepoints below are the entire reason this project vendors any
// of Font Awesome Free at all -- this phase's own brief explicitly does
// NOT call for merging that font's full solid glyph range (real, separate,
// much bigger atlas-memory and vendored-file-size cost for ~1,400 icons
// this engine would never draw), just these:
//   folder     -- an empty/organizational Scene row (Phase 14f's "Empty"
//                 Create-menu kind); any Assets Browser directory row.
//   cube       -- a Scene row with a ModelComponent; an Assets row under
//                 assets/models/.
//   lightbulb  -- a Scene row with a PointLight.
//   sun        -- a Scene row with a DirectionalLight.
//   camera     -- a Scene row with a CameraComponent.
//   image      -- an Assets row under assets/textures/.
// Deliberately declared here as named char32_t constants, not ImGui's own
// ImWchar -- this header has no ImGui dependency at all (its only real
// include is <cstdint>), so editor_ui.cpp's own font-atlas merge builds its
// ImWchar glyph-ranges array FROM these constants (one `static_cast<ImWchar>`
// per entry -- see that file's own Phase 17b comment), not the other way
// around. That keeps the codepoints the atlas actually bakes and the
// codepoints the row-drawing code prints from ever silently drifting apart
// -- the identical "one constant, every consumer reads the same copy"
// discipline editor_ui.cpp's own kAssetDragDropPayloadType constant already
// established for SetDragDropPayload()/AcceptDragDropPayload(), just
// applied to six shared codepoints instead of one shared payload-type tag.
//
// --- Deliberately general, not hardcoded to tree rows ---------------------
// This phase's own brief is explicit that a LATER phase (the toolbar --
// grid/lighting/texture/play-pause toggle icons -- is a separate, later
// item in this same arc, NOT this phase's own scope) will want to merge
// more icons into the same font/atlas. Nothing here forces that to be
// awkward: a future phase adds one more named char32_t constant alongside
// these six and appends it to editor_ui.cpp's own icon glyph-ranges array
// (see that file's own Phase 17b comment) -- the SAME ImFont this phase's
// own AddFontFromFileTTF()/MergeMode call already builds keeps growing, no
// rearchitecting of the merge mechanism itself, just "one more codepoint"
// each time.

#include <cstdint>

#include "engine/asset_drop.hpp"

namespace engine {

// Font Awesome Free 6.7.2 Solid's own Unicode Private Use Area codepoints
// for each glyph this engine actually draws -- see this header's own top
// comment for which row/category each one represents, and README.md's own
// Phase 17b section for exactly how this project's own vendored subset
// file was produced from the upstream font.
constexpr char32_t kIconFolder = 0xF07B;            // Font Awesome "folder"
constexpr char32_t kIconMesh = 0xF1B2;              // Font Awesome "cube"
constexpr char32_t kIconPointLight = 0xF0EB;        // Font Awesome "lightbulb"
constexpr char32_t kIconDirectionalLight = 0xF185;  // Font Awesome "sun"
constexpr char32_t kIconCamera = 0xF030;            // Font Awesome "camera"
constexpr char32_t kIconTexture = 0xF03E;           // Font Awesome "image"

// Given one Scene Hierarchy row's own component flags (see
// scene_hierarchy.hpp's own SceneTreeNode fields -- set once, in
// buildSceneTree(), from the same registry.getComponent<T>() checks every
// other per-entity UI in this engine already does), returns which of the
// six codepoints above that row's icon should draw.
//
// Precedence when an entity has more than one of these flags set at once
// (never produced by any Create-menu path in this engine today -- every
// kPointLight/kDirectionalLight/kCamera case explicitly gives its entity NO
// ModelComponent, see application.cpp's own spawnEntityFromCreateMenu()
// comment -- but not actually impossible, e.g. a hand-edited scene JSON
// combining a ModelComponent with a PointLight on the same entity, which
// scene_loader.cpp's schema has never forbidden): ModelComponent wins over
// every light/camera flag, since it's literally what that entity draws in
// the Viewport -- the most visually load-bearing fact about the row; a
// light flag wins over Camera, since a light actually affects this frame's
// rendered image while a CameraComponent today (camera_component.hpp's own
// header comment) does not; PointLight is checked before DirectionalLight
// only because that's this engine's own Point-Light-then-Directional-Light-
// then-Camera arc order (light.hpp's top comment) -- there is no
// rendering-significance difference between the two to break the tie on
// otherwise. An entity with none of the four flags set (this phase's own
// "empty/organizational node" case, Phase 14f's "Empty" Create-menu kind)
// gets the generic folder icon -- the same glyph an Assets Browser
// directory row uses, since both mean the same thing: "a group, not a
// specific piece of content."
char32_t sceneNodeIconGlyph(bool hasModel, bool hasPointLight, bool hasDirectionalLight, bool hasCamera);

// Given one Assets Browser row -- whether it's a directory, and (for a
// file) which AssetDropCategory (asset_drop.hpp) it falls under, the exact
// same classification editor_ui.cpp's own Viewport-drop handling already
// derives from an identical "assets/" + AssetTreeNode::relativePath string
// (see this header's own top comment for why reusing that enum here,
// rather than inventing a second "what kind of asset is this" type, keeps
// the two from ever disagreeing) -- returns which codepoint that row's icon
// should draw. `isDirectory` is checked FIRST and wins outright: every
// directory row (a bare "models"/"textures" category folder, or a
// subfolder like assets/textures/skybox/) gets the folder icon regardless
// of what classifyAssetDropPath() would say about its own relativePath
// (which, for a bare category folder, is kUnrecognized anyway -- see that
// function's own header comment: it only recognizes paths strictly BENEATH
// "assets/models/"/"assets/textures/", not those two prefixes themselves).
// kUnrecognized on a FILE row is unreachable through any path
// buildAssetTree() (asset_browser.hpp) actually walks -- every file
// reachable there already lives under one of the two browsable category
// directories asset_drop.hpp's own two prefixes match -- but is still
// handled explicitly, falling back to the folder icon, the same "safe,
// harmless default for a path that should be impossible" instinct
// asset_browser.cpp's own unreadable-entry handling already models,
// applied here instead of an assert/crash.
char32_t assetNodeIconGlyph(bool isDirectory, AssetDropCategory category);

}  // namespace engine

#endif  // ENGINE_EDITOR_ICONS_HPP
