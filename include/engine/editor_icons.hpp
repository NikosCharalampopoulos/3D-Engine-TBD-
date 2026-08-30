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

// Phase 17c: four MORE codepoints, added to the SAME vendored subset/atlas
// exactly the way this header's own "Deliberately general, not hardcoded to
// tree rows" comment above (Phase 17b) said a future toolbar phase would --
// one more named char32_t constant each, appended to editor_ui.cpp's own
// kIconGlyphRanges array, re-subsetted into the SAME assets/fonts/
// editor-icons.ttf file alongside the original six above (see README.md's
// own Phase 17c section for the exact re-subsetting command and the
// before/after glyph-count proof that the original six still render
// unchanged). These four are for the Viewport toolbar row a reference
// mockup shows (README.md's own Phase 17c section) -- grid/lighting/
// texture-mode/undo/play/pause -- but that row only needs FOUR new glyphs,
// not six: its "lighting" button and its "texture-mode" button reuse
// kIconDirectionalLight/kIconTexture above VERBATIM (see
// ToolbarButton/toolbarButtonIconGlyph()'s own comment below for why) rather
// than vendoring a second, visually near-identical sun/image glyph under a
// new codepoint for no reason -- the same "don't grow the atlas for a
// glyph this engine would never draw distinctly" discipline Phase 17b's own
// header comment above already applied to the full ~1,400-glyph upstream
// range.
constexpr char32_t kIconGrid = 0xF00A;   // Font Awesome "table-cells" (classic 2x2/3x3 grid icon)
constexpr char32_t kIconUndo = 0xF0E2;   // Font Awesome "arrow-rotate-left" (the family's "undo" glyph)
constexpr char32_t kIconPlay = 0xF04B;   // Font Awesome "play"
constexpr char32_t kIconPause = 0xF04C;  // Font Awesome "pause"

// Phase 18h: one more codepoint, re-subsetted into the SAME vendored file
// the exact same way Phase 17c's own four grew Phase 17b's original six --
// see README.md's own Phase 18h section for the exact re-subsetting command
// and before/after glyph-count proof. Font Awesome Free 6.7.2 Solid's own
// "arrow-rotate-right" glyph (U+F01E) -- verified directly against the
// vendored upstream fa-solid-900.ttf's own cmap (fontTools'
// TTFont.getBestCmap()), not guessed from memory -- is this family's own
// "redo" glyph, the natural mirror of kIconUndo's "arrow-rotate-left" just
// above (F0E2/F01E are Font Awesome's own paired left/right rotate-arrow
// icons, used everywhere for undo/redo). Nothing in this engine's existing
// six-glyph set is a plausible redo substitute the way kLighting/
// kTextureMode reuse kIconDirectionalLight/kIconTexture verbatim (see
// ToolbarButton's own comment below) -- undo and redo are visually
// mirror-image, not identical, concepts, so this genuinely needs its own
// new glyph rather than reusing kIconUndo a second time.
constexpr char32_t kIconRedo = 0xF01E;  // Font Awesome "arrow-rotate-right" (the family's "redo" glyph)

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

// Phase 17c: the Viewport toolbar row's six buttons, left-to-right per the
// reference mockup (README.md's own Phase 17c section) -- a grid toggle, a
// lighting toggle, a texture-mode toggle, an undo button, and a play/pause
// pair. Named as an enum (not six separate bool parameters the way
// sceneNodeIconGlyph() above takes flags) because, unlike a Scene row's
// component flags, a toolbar button's identity IS the whole input -- there
// is no combination of "which buttons are present" to resolve precedence
// between, just "given this ONE button, which glyph." kNone deliberately
// does not exist: every toolbar button this engine draws names itself here
// explicitly, so a future button added to the row without also adding an
// enumerator here fails to compile at its own toolbarButtonIconGlyph() call
// site instead of silently drawing a leftover/wrong glyph.
enum class ToolbarButton {
    kGrid,
    kLighting,
    kTextureMode,
    kUndo,
    // Phase 18h: the redo button added alongside the now-real undo button --
    // see kIconRedo's own comment above.
    kRedo,
    kPlay,
    kPause,
};

// Given one Viewport-toolbar button, returns which codepoint it draws.
// Exhaustive `switch`, no `default:` -- the identical "let -Wswitch (this
// project builds with -Wextra) catch a future ToolbarButton enumerator this
// function forgot to handle" discipline assetNodeIconGlyph() above already
// establishes, rather than a default case that would silently swallow that
// mistake.
//
// kLighting and kTextureMode deliberately return kIconDirectionalLight/
// kIconTexture -- the SAME two constants a Scene row with a
// DirectionalLight/an Assets row under assets/textures/ already draws --
// not a second, newly-vendored "toolbar sun"/"toolbar image" glyph. Both
// toolbar concepts really are the identical idea a tree row already uses
// that glyph for (kLighting toggles screen-space ambient occlusion, a
// LIGHTING technique -- Application::ssaoDisabled_, see
// editor_ui.cpp's own Phase 17c comment; kTextureMode toggles showing the
// raw SSAO occlusion buffer as the Viewport's own rendered picture instead
// of the normal shaded scene -- Application::ssaoDebugMode_, a literal
// "swap which IMAGE is on screen" toggle), so reusing the constant is more
// honest than inventing a visually-near-duplicate second glyph purely so
// each ToolbarButton enumerator would map to its own unique constant name.
// Phase 18h: kUndo/kRedo now return real, distinct glyphs of their own
// (kIconUndo/kIconRedo above) -- both buttons were BeginDisabled()'d stubs
// with no real behavior through Phase 18g (kRedo did not exist at all until
// this phase), so this is the first time either constant is actually drawn
// live.
char32_t toolbarButtonIconGlyph(ToolbarButton button);

}  // namespace engine

#endif  // ENGINE_EDITOR_ICONS_HPP
