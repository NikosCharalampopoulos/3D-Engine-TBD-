#ifndef ENGINE_ASSET_DROP_HPP
#define ENGINE_ASSET_DROP_HPP

// Phase 15g: the last item in the Phase 15 arc -- real drag-and-drop from
// the Assets panel (Phase 15d's own real, read-only file tree) into the
// Viewport, the interaction pattern this whole arc's roadmap named from the
// start (Blender's own primary Asset Browser gesture) and every phase since
// 15d has explicitly deferred (see asset_browser.hpp's own "Deliberately not
// done this phase" list, and material_override.hpp's own "Deliberately NOT
// done this phase" list).
//
// This header holds ONLY the pure, GL/ImGui/ecs-free half of that feature:
// given the assets/-relative path a drag-and-drop payload carries (see
// editor_ui.cpp's own Phase 15g comment for how that payload is built and
// carried), which of the two real actions this phase supports should run,
// and what should a freshly dropped model's own entity be called. Both
// questions are pure string logic with one obviously correct answer given
// the input -- exactly the kind of small, standalone decision this
// codebase's own established pattern (light.hpp's
// resolveActiveDirectionalLight()/collectPointLights(), material_override.hpp's
// resolveDiffuseTextureOverride(), asset_browser.hpp's buildAssetTree()) pulls
// out into its own file specifically so a unit test (tests/asset_drop_test.cpp)
// can exercise it without a live GL context, a real ResourceManager, or a
// Dear ImGui frame. The actual GL-touching, registry-mutating other half --
// spawning an entity, installing a MaterialOverride -- stays in
// application.cpp, the same "pure decision here, real side effects in
// Application" split resolveDiffuseTextureOverride()/
// Application::render() already establish for the identical reason.
//
// --- Why classification is needed at all, unlike every other ENGINE_DEBUG_*
// trigger this phase adds ---------------------------------------------------
// ENGINE_DEBUG_DROP_MODEL and ENGINE_DEBUG_DROP_TEXTURE (application.cpp)
// each already know which action they want -- the env var's own NAME says
// so, the identical "one separately-named var per action" shape
// ENGINE_DEBUG_CREATE/ENGINE_DEBUG_ASSIGN_TEXTURE already established. A
// real mouse drag has no such out-of-band channel: Dear ImGui's own
// SetDragDropPayload()/AcceptDragDropPayload() (imgui.h) carry one flat
// path string and nothing else (see editor_ui.cpp's own Phase 15g comment
// for the exact payload shape), so the Viewport's own drop handler has to
// figure out "model or texture" from the path alone before it can decide
// what to do -- classifyAssetDropPath() below is that one decision, made
// once, in one place, rather than re-derived ad hoc at the one real call
// site that needs it (Application::handleViewportAssetDrop(),
// application.cpp).
//
// --- Classification is by top-level folder, not file extension -----------
// Matches asset_browser.hpp's own "browsable category = which of
// assets/models/ or assets/textures/ this entry lives under" concept
// exactly (see that header's own comment) -- NOT a per-extension check.
// This means a file that happens to live under assets/models/ but isn't
// actually an Assimp-importable scene root (this project's own
// assets/models/*.mtl companion files, sitting in the identical tree
// alongside their own *.obj) still classifies as kModel here -- this
// function only answers "which ACTION should be attempted," not "will that
// action actually succeed." Whether it does is Application::
// spawnEntityFromDroppedModel()'s own concern (application.cpp), which
// wraps its resources_.getModel() call in a try/catch specifically because
// an arbitrary real file under assets/models/ -- unlike
// spawnEntityFromCreateMenu()'s own four checked-in, known-good constants --
// is not guaranteed loadable, and a failed load must degrade to a LOG_WARN,
// never crash the whole engine.

#include <string>

namespace engine {

// Which of this phase's two real Viewport-drop actions a dropped
// assets/-relative path should trigger -- kUnrecognized covers everything
// that isn't under assets/models/ or assets/textures/ at all (a bare
// "assets/models"/"assets/textures" top-level folder itself, dropped
// directly rather than a file beneath it; anything outside either browsable
// category; a malformed or empty string), which Application::
// handleViewportAssetDrop() (application.cpp) treats as a no-op, logged, not
// a crash.
enum class AssetDropCategory {
    kModel,
    kTexture,
    kUnrecognized,
};

// `assetRelativePath` is the SAME "assets/..." form
// AssetTreeNode::relativePath/ModelComponent::path/
// MaterialOverride::diffuseTexturePath already use (e.g.
// "assets/models/falling_cube.obj") -- see editor_ui.cpp's own
// renderAssetTreeNode() Phase 15g comment for where a real drag's own
// payload is built in exactly this form. A simple prefix check against
// "assets/models/"/"assets/textures/" (std::string::rfind(prefix, 0) == 0 --
// this project's own C++17 target predates std::string::starts_with) --
// deliberately not std::filesystem, which this file has no dependency on at
// all (this is string classification, not real filesystem I/O, the same
// distinction asset_drop.cpp's own modelBaseNameFromAssetPath() below draws
// for the identical reason application.cpp's own uniqueEntityName()/
// findEntityByName() neighbors already do).
AssetDropCategory classifyAssetDropPath(const std::string& assetRelativePath);

// Derives a short, human-readable base name for a freshly dropped model
// entity from its own filename -- "assets/models/falling_cube.obj" ->
// "falling_cube" -- matching every other Create-menu entity kind's own
// short display name (Application::spawnEntityFromCreateMenu()'s own
// baseName, application.cpp) rather than showing a full asset path in the
// Scene Hierarchy tree. Never returns an empty string: a pathologically
// empty or extension-only input (e.g. "assets/models/.obj") falls back to
// the literal "Model" -- Application::spawnPositionedEntity()'s own
// uniqueEntityName() call (application.cpp) still de-duplicates it against
// whatever's already in the scene exactly like every other kind, so this
// only ever needs to be a REASONABLE starting name, not a bulletproof one.
std::string modelBaseNameFromAssetPath(const std::string& assetRelativePath);

}  // namespace engine

#endif  // ENGINE_ASSET_DROP_HPP
