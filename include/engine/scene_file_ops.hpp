#ifndef ENGINE_SCENE_FILE_OPS_HPP
#define ENGINE_SCENE_FILE_OPS_HPP

// Phase 18i: the pure, GL/ImGui/ecs-free half of "multiple named scenes" --
// this engine had exactly ONE scene path it ever knew about through Phase
// 18h (kDefaultScenePath, application.cpp), always the same
// assets/scenes/default.json. This phase adds Save As.../Open Scene..., so
// there are now two small, standalone, "one obviously correct answer given
// the input" string/filesystem decisions neither of which has anything to do
// with ImGui or a live EntityRegistry -- exactly the kind of thing this
// codebase's own established pattern (asset_drop.hpp's
// classifyAssetDropPath()/modelBaseNameFromAssetPath(), asset_browser.hpp's
// buildAssetTree()) pulls into its own small file specifically so a unit
// test (tests/scene_file_ops_test.cpp) can exercise it without a live GL
// context, a real ResourceManager, or a Dear ImGui frame:
//   - sanitizeSceneName(): is a user-typed "Save As" name even usable as a
//     filename at all, and if so, what does it become?
//   - listSceneFileNames(): what scene files actually exist on disk to show
//     in an "Open Scene..." picker?
// The actual GL/ECS-touching other half -- clearing/repopulating registry_,
// calling scene_serialization.hpp's saveScene()/loadScene(), updating
// Application's own currentScenePath_ -- stays in application.cpp, the same
// "pure decision here, real side effects in Application" split
// resolveDiffuseTextureOverride()/classifyAssetDropPath() already establish
// for the identical reason (see those headers' own comments).
//
// --- sanitizeSceneName(): strip, don't just reject -----------------------
// A user typing a scene name into the Save As popup has no reason to know or
// care that ':' or '/' would be unsafe in a filename -- rejecting their
// whole input over one stray character (a trailing space from a careless
// paste, say) would be needlessly hostile UX for a feature this small.
// Instead, every character that isn't a letter, digit, '_', or '-' is
// dropped outright (not escaped, not percent-encoded -- just removed), and
// interior whitespace collapses to '_' so a name like "My Cool Scene"
// becomes "My_Cool_Scene" rather than silently losing its word breaks.
// Critically, '.' is NOT in the allowed set: this is what keeps a name like
// "../../etc/passwd" from ever becoming a path-traversal attempt at all --
// after sanitization it's just "etcpasswd", a single ordinary filename
// component, since sceneRelativePathForName() below is the only place that
// ever appends a directory separator or a ".json" extension. There is
// therefore no separate "reject '..'" or "reject '/'" special case needed:
// both characters are already outside the allowed set for an entirely
// different, simpler reason (they're just not letters/digits/'_'/'-'), and
// removing them has the traversal-proofing as a side effect, not as its own
// bespoke check.
//
// Rejects (returns std::nullopt) an input that sanitizes down to nothing at
// all -- an empty string, one that's pure whitespace, or one made up
// entirely of characters this function strips (e.g. "???") -- since
// "assets/scenes/.json" is not a name any real Save As should silently
// produce.

#include <optional>
#include <string>
#include <vector>

namespace engine {

// Sanitizes `rawInput` (the Save As popup's own text field contents) into a
// filename-safe scene name -- see this header's own top comment for the
// exact rule. Returns std::nullopt when nothing usable is left after
// sanitizing (an empty, whitespace-only, or entirely-stripped input) --
// Application::saveSceneAs()'s one real caller (EditorUI's own Save As
// popup, application.cpp/editor_ui.cpp) never calls it with such a name in
// the first place (the popup's own Save button is disabled whenever this
// returns std::nullopt for the current text field contents), but the
// function itself still reports it explicitly rather than silently
// returning some fallback name a user never actually typed.
std::optional<std::string> sanitizeSceneName(const std::string& rawInput);

// The assets/-relative scene file path a sanitized name resolves to, e.g.
// "My_Cool_Scene" -> "assets/scenes/My_Cool_Scene.json" -- the identical
// "assets/..." relative form ModelComponent::path/kDefaultScenePath's own
// pre-resolveAssetPath() form already use. `sanitizedName` is assumed to
// already be sanitizeSceneName()'s own output (or an already-known-good name
// from listSceneFileNames() below) -- this function does no validation of
// its own, matching modelBaseNameFromAssetPath()'s own "one obvious answer
// given a well-formed input" scope.
std::string sceneRelativePathForName(const std::string& sanitizedName);

// Lists the base names (i.e. filename with the ".json" extension stripped)
// of every regular *.json file directly under `scenesDir`, sorted
// alphabetically -- a FLAT, single-directory listing, deliberately not a
// recursive tree the way asset_browser.hpp's buildAssetTree() is: unlike
// assets/models/ or assets/textures/, assets/scenes/ has no meaningful
// subdirectory structure for a scene file to live under, so there is
// nothing here for a nested walk to actually find. `scenesDir` is the
// ALREADY-RESOLVED path to the assets/scenes/ directory itself (e.g.
// resolveAssetPath("assets/scenes") -- see paths.hpp), matching
// buildAssetTree()'s own `assetsRoot` parameter convention, so a test can
// point this at a scratch directory instead of this project's real
// assets/scenes/ tree.
//
// Every filesystem query goes through the std::error_code-taking overload,
// never the throwing one (the identical exception-safety discipline
// asset_browser.cpp's own buildNode()/tryIsDirectory() already establish,
// for the identical reason: this runs from inside EditorUI's own Open Scene
// popup handling every frame that popup is open, and a single unreadable
// entry must never crash the whole editor over it). A missing
// assets/scenes/ directory, or one that exists but is empty, both return an
// empty vector -- unremarkable, not an error, the same "a missing/empty
// category directory is silently skipped" convention buildAssetTree() also
// follows.
std::vector<std::string> listSceneFileNames(const std::string& scenesDir);

}  // namespace engine

#endif  // ENGINE_SCENE_FILE_OPS_HPP
