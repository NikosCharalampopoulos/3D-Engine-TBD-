// Phase 18i: tests engine::sanitizeSceneName()/sceneRelativePathForName()/
// listSceneFileNames() (src/scene_file_ops.cpp) in isolation -- same "plain
// executable, links only the pure logic file it's testing" shape as
// asset_drop_test/asset_browser_test above. scene_file_ops.cpp depends only
// on <filesystem> (no ecs.hpp, no GLM, no GL/ImGui at all), so this needs no
// live GL context/GPU/Dear ImGui frame either, and -- for
// listSceneFileNames() -- builds its own scratch directory rather than
// touching this project's real assets/scenes/ tree, the same discipline
// asset_browser_test.cpp already establishes for buildAssetTree().

#include "engine/scene_file_ops.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;

int failures = 0;

void expectTrue(bool condition, const std::string& what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

void expectEqual(const std::string& actual, const std::string& expected, const std::string& what) {
    expectTrue(actual == expected, what + " (expected \"" + expected + "\", got \"" + actual + "\")");
}

void touchFile(const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::ofstream(path).put('x');
}

}  // namespace

int main() {
    using engine::listSceneFileNames;
    using engine::sanitizeSceneName;
    using engine::sceneRelativePathForName;

    // --- sanitizeSceneName(): the ordinary case ----------------------------
    {
        const std::optional<std::string> result = sanitizeSceneName("my_level_1");
        expectTrue(result.has_value(), "an already-clean name sanitizes to itself");
        if (result.has_value()) {
            expectEqual(*result, "my_level_1", "an already-clean name is returned unchanged");
        }
    }

    // Interior whitespace collapses to '_', preserving word breaks.
    {
        const std::optional<std::string> result = sanitizeSceneName("My Cool Scene");
        expectTrue(result.has_value(), "a spaced name sanitizes to something");
        if (result.has_value()) {
            expectEqual(*result, "My_Cool_Scene", "interior whitespace becomes '_', not dropped");
        }
    }

    // Unsafe/unrelated characters are stripped, not rejected outright.
    {
        const std::optional<std::string> result = sanitizeSceneName("Level:One/Two\\Three?!");
        expectTrue(result.has_value(), "a name with unsafe characters still sanitizes to something usable");
        if (result.has_value()) {
            expectEqual(*result, "LevelOneTwoThree", "unsafe characters are stripped outright, not escaped");
        }
    }

    // '.' is never allowed through -- this is what makes "../../etc/passwd"
    // harmless: every '.' and '/' is simply gone, leaving one ordinary
    // filename component with no directory-traversal meaning left at all.
    {
        const std::optional<std::string> result = sanitizeSceneName("../../etc/passwd");
        expectTrue(result.has_value(), "a path-traversal-shaped input still sanitizes to SOME name");
        if (result.has_value()) {
            expectEqual(*result, "etcpasswd",
                        "'.' and '/' are stripped -- no path-traversal meaning survives sanitizing");
        }
    }

    // Leading/trailing incidental punctuation trims away any resulting
    // leading/trailing '_'.
    {
        const std::optional<std::string> result = sanitizeSceneName("  !!My Scene!!  ");
        expectTrue(result.has_value(), "surrounding punctuation/whitespace still sanitizes to something");
        if (result.has_value()) {
            expectEqual(*result, "My_Scene", "leading/trailing '_' left over from stripped characters is trimmed");
        }
    }

    // Empty, whitespace-only, and entirely-stripped inputs are all rejected.
    expectTrue(!sanitizeSceneName("").has_value(), "an empty name is rejected");
    expectTrue(!sanitizeSceneName("   ").has_value(), "a whitespace-only name is rejected");
    expectTrue(!sanitizeSceneName("???").has_value(), "a name that strips down to nothing is rejected");
    expectTrue(!sanitizeSceneName("...").has_value(), "a name made entirely of '.' is rejected (nothing survives)");

    // A pathologically long name is truncated, not rejected outright.
    {
        const std::string longInput(500, 'a');
        const std::optional<std::string> result = sanitizeSceneName(longInput);
        expectTrue(result.has_value(), "a very long name still sanitizes to something, truncated rather than "
                                        "rejected");
        if (result.has_value()) {
            expectTrue(result->size() <= 64, "a very long name is capped, not passed through verbatim (got " +
                                                  std::to_string(result->size()) + " chars)");
        }
    }

    // --- sceneRelativePathForName() -----------------------------------------
    expectEqual(sceneRelativePathForName("default"), "assets/scenes/default.json",
                "the well-known default scene name resolves to the exact existing kDefaultScenePath's own relative "
                "form -- Open Scene's \"default\" entry and this engine's original single scene file must be the "
                "SAME file, not a look-alike");
    expectEqual(sceneRelativePathForName("My_Cool_Scene"), "assets/scenes/My_Cool_Scene.json",
                "an ordinary sanitized name resolves under assets/scenes/ with a \".json\" extension appended");

    // --- listSceneFileNames() -----------------------------------------------
    const fs::path scratchRoot = fs::temp_directory_path() / "engine_scene_file_ops_test";
    fs::remove_all(scratchRoot);

    // A missing directory yields an empty list, not an error.
    {
        const std::vector<std::string> names = listSceneFileNames((scratchRoot / "scenes").string());
        expectTrue(names.empty(), "a wholly-missing assets/scenes/ directory yields an empty list, not a crash");
    }

    // An existing-but-empty directory also yields an empty list.
    fs::create_directories(scratchRoot / "scenes");
    {
        const std::vector<std::string> names = listSceneFileNames((scratchRoot / "scenes").string());
        expectTrue(names.empty(), "an empty assets/scenes/ directory yields an empty list");
    }

    // Only *.json files are listed, sorted alphabetically, extension
    // stripped; non-.json siblings and a subdirectory are both ignored (this
    // is a FLAT listing, not a recursive tree).
    touchFile(scratchRoot / "scenes" / "zeta.json");
    touchFile(scratchRoot / "scenes" / "alpha.json");
    touchFile(scratchRoot / "scenes" / "default.json");
    touchFile(scratchRoot / "scenes" / "notes.txt");
    fs::create_directories(scratchRoot / "scenes" / "backup");
    touchFile(scratchRoot / "scenes" / "backup" / "old.json");
    {
        const std::vector<std::string> names = listSceneFileNames((scratchRoot / "scenes").string());
        expectTrue(names.size() == 3, "exactly the three top-level *.json files are listed -- \"notes.txt\" and "
                                       "\"backup/old.json\" are both excluded (wrong extension; nested, not "
                                       "top-level, respectively) (got " +
                                           std::to_string(names.size()) + ")");
        if (names.size() == 3) {
            expectEqual(names[0], "alpha", "sorted alphabetically, extension stripped");
            expectEqual(names[1], "default", "sorted alphabetically, extension stripped");
            expectEqual(names[2], "zeta", "sorted alphabetically, extension stripped");
        }
    }

    fs::remove_all(scratchRoot);

    if (failures == 0) {
        std::printf("scene_file_ops_test: all checks passed\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "scene_file_ops_test: %d check(s) failed\n", failures);
    return EXIT_FAILURE;
}
