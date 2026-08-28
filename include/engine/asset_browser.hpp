#ifndef ENGINE_ASSET_BROWSER_HPP
#define ENGINE_ASSET_BROWSER_HPP

// Phase 16: turns the actual on-disk assets/ directory tree into the nested
// AssetTreeNode shape the Assets panel displays (see editor_ui.cpp's own
// Phase 16 comment for the ImGui-facing half) -- same "pure data vs. GL/
// ImGui-facing" split scene_hierarchy.hpp's own header comment documents for
// buildSceneTree(), just walking std::filesystem instead of the ECS:
// buildAssetTree() below depends only on <filesystem> (no GL, no ImGui, no
// ecs.hpp), so its own directory-walking/sorting logic can be exercised by a
// plain unit test (tests/asset_browser_test.cpp) against a scratch directory
// on disk, without a live GL context, Dear ImGui frame, or this project's
// own real assets/ tree.
//
// --- Which assets/ subdirectories are "browsable" --------------------------
// Only assets/models/ and assets/textures/ -- the two asset kinds this
// engine's own ResourceManager (resource_manager.hpp) actually loads and
// caches BY PATH for something a level designer places into a scene
// (getModel()/getTexture()). Everything else actually present under assets/
// today is deliberately excluded:
//   - assets/shaders/ -- GLSL source the renderer itself is built from.
//     Every path under it is a resolveAssetPath()'d constant baked into
//     application.cpp at static-init time, never something a level designer
//     picks per-entity through a browser. Engine implementation detail, not
//     user-facing content -- the same "genuinely different KIND of thing"
//     distinction camera_component.hpp's own header comment draws between
//     what a camera optically IS and how input drives it, just applied here
//     to "renderer machinery" vs. "placeable content."
//   - assets/scenes/ -- today just default.json, the currently-loaded
//     level's own save/load file (scene_serialization.hpp), not a content
//     asset placed INTO a level the way a model or texture is.
//     ResourceManager has no "Scene" cache entry at all -- getShader()/
//     getTexture()/getModel() are its whole surface -- so this function's
//     own two-directory allowlist is exactly ResourceManager's own real
//     asset-type list, one entry short (Shader excluded for the identical
//     "engine machinery, not placeable content" reason assets/shaders/ is,
//     immediately above). This is the actual, checked-not-assumed boundary
//     the allowlist mirrors, not an arbitrary guess at what "feels like" an
//     asset.
// A category directory that doesn't exist on disk (nothing requires either
// to exist) is silently skipped rather than treated as an error --
// buildAssetTree()'s own contract is "show what's actually there," and an
// empty/missing category is a completely unremarkable, expected state for a
// project that simply hasn't added one yet.
//
// --- Unreadable entries never abort the walk (bug-review fix) -------------
// buildAssetTree() and everything it calls are exception-safe with respect
// to the filesystem: every std::filesystem query goes through the
// std::error_code-taking overload, never the throwing one, all the way down
// the recursion (see asset_browser.cpp's own buildNode()/tryIsDirectory()
// comments for the mechanics). A single entry this function cannot safely
// read -- a broken or self-referencing/looping symlink (ELOOP), a
// permission-denied subdirectory, something removed out from under this
// walk mid-scan -- is simply left out of the returned tree, with a
// LOG_WARN noting what was skipped and why, rather than raising a
// std::filesystem_error. This matters well beyond one missing tree row:
// buildAssetTree() runs inside EditorUI's constructor, which runs inside
// Application's own constructor, so before this fix a single bad entry
// anywhere under assets/models/ or assets/textures/ propagated an uncaught
// exception all the way out of Application's constructor -- main.cpp's
// top-level try/catch turned that into a fatal, whole-engine launch
// failure over a filesystem hiccup confined to one asset subdirectory. The
// same "one bad thing doesn't take down everything else" policy the
// missing-category handling above already established now applies one
// level deeper, per-entry, not just per-category.
//
// A permission-denied SUBDIRECTORY (readable enough to know it exists and
// is a directory, but not enough to list its contents -- EACCES on open,
// as opposed to ELOOP/ENOENT on a single entry's own stat) gets the exact
// same "left out with a LOG_WARN" treatment, reached via
// directory_iterator's own construction failing rather than
// tryIsDirectory() above -- deliberately NOT
// fs::directory_options::skip_permission_denied, whose own documented
// behavior is to swallow that failure and hand back zero entries with no
// error_code set at all. That would make a permission-denied directory
// silently indistinguishable, both to this function's own logic and to a
// level designer looking at the rendered Assets panel, from one that is
// genuinely, unremarkably empty -- exactly the kind of silent data loss
// this whole section exists to avoid, so this function does its own
// explicit error_code check instead of delegating to that flag.
//
// --- Ordering ----------------------------------------------------------
// Unlike buildSceneTree()'s own "creation order" (a meaningful, stable
// concept for ECS entities -- see that header's own comment),
// std::filesystem::directory_iterator's own enumeration order is
// unspecified by the standard and has been observed to vary by filesystem/
// platform. buildAssetTree() sorts every directory's own entries itself
// (subdirectories before files, alphabetically within each group -- the
// same convention most real file-browser UIs use) so this function's own
// output -- and therefore what the Assets panel actually displays, and what
// a unit test can assert against -- is deterministic regardless of what
// order the OS happens to hand entries back in.
//
// --- Caching -------------------------------------------------------------
// Called exactly once (by EditorUI's own constructor -- see editor_ui.cpp's
// own Phase 16 comment), not every frame the way buildSceneTree() is.
// buildSceneTree()'s own "rebuilt fresh every frame -- a handful of
// entities, so no reason to cache" comment does NOT apply here: the ECS
// tree can change any frame something is created/deleted through the Scene
// panel's own Create menu (Phase 14f), but nothing in this engine ever
// writes into assets/ at runtime today -- no import feature exists (see
// this phase's own README section on deliberately not building one), and
// this phase's Asset Browser is read-only -- so the tree buildAssetTree()
// returns cannot change between one frame and the next during a single run.
// Re-walking the filesystem every frame would just be repeated syscalls
// against a tree that's already known to be identical to the one built a
// moment ago; a one-time build, refreshed only if a future phase adds a way
// for assets/ to change at runtime (an import feature, a filesystem watcher,
// etc.), is the correct match for what actually varies here, the same way
// ResourceManager's own get-or-load cache (resource_manager.hpp) is a
// deliberate, documented choice matched to ITS OWN "never reloads once
// loaded" contract rather than a default reached for without thinking about
// it.

#include <string>
#include <vector>

namespace engine {

// One row of the Assets panel's tree -- a real file or directory under
// assets/models/ or assets/textures/. Owns its children by value, same
// "built once per call, walked top-down" shape as scene_hierarchy.hpp's own
// SceneTreeNode.
struct AssetTreeNode {
    // This entry's own filename, e.g. "falling_cube.obj" or "skybox" -- NOT
    // a full path. What the Assets panel's tree row actually displays as its
    // label.
    std::string name;
    // Relative to assets/ itself, e.g. "models/falling_cube.obj" -- a
    // stable, portable identity for this row (used as editor_ui.cpp's own
    // click-to-select key, see renderAssetTreeNode()) that stays meaningful
    // independent of wherever assets/ physically resolves to on this
    // machine (see paths.hpp's own resolveAssetPath() comment on why that
    // varies by launch location).
    std::string relativePath;
    bool isDirectory = false;
    std::vector<AssetTreeNode> children;  // always empty for a file.
};

// Builds the whole forest of top-level AssetTreeNodes -- one per browsable
// category directory (see this header's own comment above for exactly which,
// and why) that actually exists under `assetsRoot` -- recursively populated
// from the real filesystem. `assetsRoot` is the ALREADY-RESOLVED path to the
// assets/ directory itself (e.g. resolveAssetPath("assets") -- see
// paths.hpp); this function does no path resolution of its own, so a test
// can point it at a scratch directory instead of this project's real
// assets/ tree.
std::vector<AssetTreeNode> buildAssetTree(const std::string& assetsRoot);

}  // namespace engine

#endif  // ENGINE_ASSET_BROWSER_HPP
