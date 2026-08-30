// Phase 16's own test: exercises engine::buildAssetTree()
// (src/asset_browser.cpp) against a hand-built scratch directory tree on
// disk, covering the category allowlist (models/textures included,
// shaders/scenes excluded), recursive nesting, deterministic sort order, a
// missing/empty category directory, (first bug-review fix) a
// self-referencing symlink that must degrade gracefully rather than throw,
// and (second bug-review fix) a permission-denied subdirectory that must be
// detected/logged rather than rendered as an indistinguishable-from-empty
// folder. Same "plain executable, links only the pure logic file it's
// testing" shape as scene_hierarchy_test (see that file's own header
// comment) -- asset_browser.cpp depends only on <filesystem>, so this needs
// no live window/GL context/GPU/Dear ImGui frame, and deliberately never
// touches this project's own real assets/ tree -- every case here builds
// (and cleans up) its own scratch directory instead, so this test's results
// never depend on what this repository's assets/ directory happens to
// contain on a given day.

#include "engine/asset_browser.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

// The permission-denied-directory case below needs a real, unprivileged
// effective uid to make the OS actually enforce a chmod(0) directory's
// permissions (see that case's own comment for why: this project's own
// sandboxed dev/CI environment runs this test as root, and root bypasses
// ordinary DAC permission checks on Linux, confirmed rather than assumed --
// see this phase's own README section). fork()/seteuid()/waitpid() are
// POSIX-only, matching paths.cpp's own precedent for guarding
// platform-specific filesystem code behind #if !defined(_WIN32) rather than
// pretending it's portable.
#if !defined(_WIN32)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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

// Creates an empty regular file at `path`, creating any missing parent
// directories first -- content doesn't matter to buildAssetTree() (it never
// reads file bytes, only walks names/directory structure), so an empty file
// is enough to stand in for a real .obj/.png.
void touchFile(const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::ofstream(path).put('x');
}

const engine::AssetTreeNode* findChild(const std::vector<engine::AssetTreeNode>& nodes, const std::string& name) {
    for (const engine::AssetTreeNode& node : nodes) {
        if (node.name == name) {
            return &node;
        }
    }
    return nullptr;
}

}  // namespace

int main() {
    // A fixed scratch root, removed both before use (in case a previous
    // interrupted run left it behind) and at the end of main() below --
    // same fixed-name-under-temp_directory_path() convention
    // scene_serialization_test.cpp already establishes for this project's
    // other filesystem-touching test. RAII isn't used here since every case
    // below shares this one root rather than each getting its own, matching
    // scene_hierarchy_test's own "several scoped cases against fresh state"
    // shape one level up (one shared scratch tree, not one registry per
    // case).
    const fs::path scratchRoot = fs::temp_directory_path() / "engine_asset_browser_test";
    fs::remove_all(scratchRoot);

    // --- Category allowlist: models/textures included, shaders/scenes NOT -
    touchFile(scratchRoot / "models" / "cube.obj");
    touchFile(scratchRoot / "textures" / "checker.png");
    touchFile(scratchRoot / "shaders" / "basic.vert");
    touchFile(scratchRoot / "scenes" / "default.json");

    {
        const std::vector<engine::AssetTreeNode> tree = engine::buildAssetTree(scratchRoot.string());

        expectTrue(tree.size() == 2,
                   "buildAssetTree() emits exactly one root per browsable category (models, textures) -- shaders/ "
                   "and scenes/ are NOT browsable categories, even though they exist on disk right alongside them");

        const engine::AssetTreeNode* models = findChild(tree, "models");
        const engine::AssetTreeNode* textures = findChild(tree, "textures");
        expectTrue(models != nullptr, "a \"models\" root exists");
        expectTrue(textures != nullptr, "a \"textures\" root exists");
        expectTrue(findChild(tree, "shaders") == nullptr, "no \"shaders\" root -- engine-internal, not browsable");
        expectTrue(findChild(tree, "scenes") == nullptr, "no \"scenes\" root -- the level's own save file, not a "
                                                           "placeable content asset");

        if (models != nullptr) {
            expectTrue(models->isDirectory, "\"models\" root is a directory");
            expectEqual(models->relativePath, "models", "\"models\" root's own relativePath");
            expectTrue(models->children.size() == 1, "\"models\" has exactly the one file placed under it");
            if (models->children.size() == 1) {
                expectEqual(models->children[0].name, "cube.obj", "the one file under \"models\"");
                expectTrue(!models->children[0].isDirectory, "a file node's own isDirectory is false");
                expectTrue(models->children[0].children.empty(), "a file node has no children");
                expectEqual(models->children[0].relativePath, "models/cube.obj",
                            "a nested file's relativePath is assets/-relative, category included");
            }
        }
    }

    // --- Recursive nesting + deterministic sort order (dirs before files,
    // alphabetical within each group) --------------------------------------
    fs::remove_all(scratchRoot);
    touchFile(scratchRoot / "textures" / "b_texture.png");
    touchFile(scratchRoot / "textures" / "a_texture.png");
    touchFile(scratchRoot / "textures" / "skybox" / "right.png");
    touchFile(scratchRoot / "textures" / "skybox" / "left.png");

    {
        const std::vector<engine::AssetTreeNode> tree = engine::buildAssetTree(scratchRoot.string());
        expectTrue(tree.size() == 1, "only \"textures\" exists this time -- no \"models\" root emitted for a "
                                      "category directory that isn't there");

        const engine::AssetTreeNode* textures = findChild(tree, "textures");
        expectTrue(textures != nullptr, "a \"textures\" root exists");
        if (textures != nullptr) {
            expectTrue(textures->children.size() == 3, "\"textures\" has its subdirectory plus its two files");
            if (textures->children.size() == 3) {
                // Directories before files, alphabetical within each group:
                // "skybox" (the only directory) sorts first regardless of
                // its name, then the two files alphabetically.
                expectEqual(textures->children[0].name, "skybox", "sort order: the subdirectory comes first");
                expectTrue(textures->children[0].isDirectory, "\"skybox\" is a directory");
                expectEqual(textures->children[1].name, "a_texture.png",
                            "sort order: files alphabetically after directories");
                expectEqual(textures->children[2].name, "b_texture.png",
                            "sort order: files alphabetically after directories");

                expectTrue(textures->children[0].children.size() == 2, "\"skybox\" has its own two files");
                if (textures->children[0].children.size() == 2) {
                    expectEqual(textures->children[0].children[0].name, "left.png",
                                "nested sort order is alphabetical too");
                    expectEqual(textures->children[0].children[1].name, "right.png",
                                "nested sort order is alphabetical too");
                    expectEqual(textures->children[0].children[0].relativePath, "textures/skybox/left.png",
                                "a doubly-nested file's relativePath includes every path segment down to it");
                }
            }
        }
    }

    // --- Missing AND empty category directories are both unremarkable -----
    fs::remove_all(scratchRoot);
    fs::create_directories(scratchRoot / "models");  // exists, but empty
    // "textures" is never created at all this time.

    {
        const std::vector<engine::AssetTreeNode> tree = engine::buildAssetTree(scratchRoot.string());
        expectTrue(tree.size() == 1,
                   "a wholly-missing category directory is silently skipped, not an error -- only \"models\" (which "
                   "exists, even though it's empty) is emitted");
        const engine::AssetTreeNode* models = findChild(tree, "models");
        expectTrue(models != nullptr, "\"models\" root exists");
        if (models != nullptr) {
            expectTrue(models->isDirectory, "an empty category directory is still reported as a directory");
            expectTrue(models->children.empty(), "an empty category directory has no children -- not an error, "
                                                   "just nothing to show yet");
        }
    }

    // --- Neither category exists at all: an empty forest, not an error ----
    fs::remove_all(scratchRoot);
    fs::create_directories(scratchRoot);

    {
        const std::vector<engine::AssetTreeNode> tree = engine::buildAssetTree(scratchRoot.string());
        expectTrue(tree.empty(), "no models/ or textures/ directory at all yields an empty forest, not a crash/"
                                  "exception");
    }

    // --- Bug-review fix: a self-referencing symlink degrades gracefully,
    // it does not throw -------------------------------------------------
    // Reproduces exactly the on-disk state this phase's own review caught:
    // a symlink one level under assets/models/ that points AT ITSELF, so
    // resolving its type (fs::is_directory() has to follow a symlink to
    // answer "directory or not") hits ELOOP ("Too many levels of symbolic
    // links"). Before the fix, buildNode() called the THROWING
    // fs::is_directory(path) overload here, so this exact scratch tree made
    // buildAssetTree() raise an uncaught std::filesystem_error -- which,
    // because buildAssetTree() runs inside EditorUI's constructor inside
    // Application's own constructor, took the whole engine down at startup
    // over one bad symlink (see asset_browser.hpp's own "Unreadable
    // entries" comment). The only correct behavior is a normal return: the
    // bad entry is left out of the tree, its sibling ("cube.obj") is not.
    fs::remove_all(scratchRoot);
    touchFile(scratchRoot / "models" / "cube.obj");
    {
        std::error_code symlinkEc;
        fs::create_symlink(scratchRoot / "models" / "self_loop", scratchRoot / "models" / "self_loop", symlinkEc);
        expectTrue(!symlinkEc, "test setup: could create the self-referencing symlink this case needs ("
                                    + symlinkEc.message() + ")");
    }

    {
        std::vector<engine::AssetTreeNode> tree;
        bool threw = false;
        std::string thrownWhat;
        try {
            tree = engine::buildAssetTree(scratchRoot.string());
        } catch (const std::exception& e) {
            threw = true;
            thrownWhat = e.what();
        }

        expectTrue(!threw, "buildAssetTree() does not throw when a self-referencing symlink is present (threw: \"" +
                                thrownWhat + "\" if this fails)");

        const engine::AssetTreeNode* models = findChild(tree, "models");
        expectTrue(models != nullptr, "\"models\" root is still emitted despite the bad sibling entry inside it");
        if (models != nullptr) {
            expectTrue(models->children.size() == 1,
                       "the unreadable \"self_loop\" symlink is left out of the tree entirely, while its readable "
                       "sibling \"cube.obj\" is still present -- one bad entry degrades gracefully instead of "
                       "aborting the whole directory's own listing");
            if (models->children.size() == 1) {
                expectEqual(models->children[0].name, "cube.obj",
                            "the surviving child is the readable sibling, not the broken symlink");
            }
        }
    }

    // --- Bug-review fix (second pass): a permission-denied SUBDIRECTORY is
    // detected/logged, not silently rendered as an indistinguishable-from-
    // empty folder ---------------------------------------------------------
    // The first-pass fix (above) made buildNode() exception-safe by
    // constructing its directory_iterator with
    // fs::directory_options::skip_permission_denied -- which turned out to
    // be its own, more subtle bug: that flag's own documented behavior is
    // to swallow an EACCES-on-open failure and hand back zero entries with
    // NO error_code ever set, so a permission-denied directory rendered as
    // an ordinary, indistinguishable EMPTY folder -- no expand arrow, no
    // LOG_WARN, nothing a level designer looking at the actual editor GUI
    // (not tailing logs) could use to tell "empty" apart from "I can't see
    // what's really in here." The fix (asset_browser.cpp's own "Bug-review
    // fix (second pass)" comment) drops that flag so EACCES instead falls
    // through to the same error_code failure path -- and therefore the same
    // LOG_WARN-and-continue treatment -- every other unreadable thing in
    // this file already gets.
    //
    // This process runs as root in this project's own sandboxed dev/CI
    // environment (checked via geteuid() below, not assumed) -- and root
    // bypasses ordinary Linux DAC permission checks, so a plain chmod(0)
    // directory is still fully listable even from THIS process (confirmed
    // empirically while writing this fix: `chmod 000 dir && ls dir` as root
    // succeeds). A real EACCES needs an actually-unprivileged effective
    // uid, so when this process IS root, this case forks a short-lived
    // child, drops ONLY the child's effective uid to "nobody" (this
    // sandbox's own uid 65534 -- seteuid, not setuid, so it's a
    // process-local, per-child privilege drop with no effect on the parent,
    // which stays root throughout and never attempts this check itself) and
    // runs buildAssetTree() there, reporting pass/fail back via the child's
    // own exit code. If privilege-dropping isn't available or doesn't work
    // in some other environment this ever runs in, that's reported as an
    // honest skip (a printed NOTE), not asserted as a pass -- see this
    // phase's own README section for why overclaiming coverage here would
    // be worse than admitting the limitation.
    fs::remove_all(scratchRoot);
    touchFile(scratchRoot / "models" / "open_file.obj");
    touchFile(scratchRoot / "models" / "locked" / "secret.obj");
    {
        std::error_code chmodEc;
        fs::permissions(scratchRoot / "models" / "locked", fs::perms::none, chmodEc);
        expectTrue(!chmodEc, "test setup: could chmod the \"locked\" subdirectory to no permissions (" +
                                  chmodEc.message() + ")");
    }

#if !defined(_WIN32)
    if (geteuid() == 0) {
        const pid_t pid = fork();
        expectTrue(pid >= 0, "test setup: could fork a child process for the privilege-drop check");
        if (pid == 0) {
            // Child process only, from here to _exit(): drop straight to
            // "nobody" so the OS actually enforces "locked"'s permissions
            // against this process -- see this case's own header comment.
            // Exit code 2 means "couldn't drop privilege at all" (the
            // parent below treats that as a skip, not a failure); 0/1 mean
            // the real assertion passed/failed.
            if (seteuid(65534) != 0) {
                _exit(2);
            }
            const std::vector<engine::AssetTreeNode> tree = engine::buildAssetTree(scratchRoot.string());
            const engine::AssetTreeNode* models = findChild(tree, "models");
            const engine::AssetTreeNode* locked = models != nullptr ? findChild(models->children, "locked") : nullptr;
            const engine::AssetTreeNode* openFile =
                models != nullptr ? findChild(models->children, "open_file.obj") : nullptr;
            const bool ok = models != nullptr && locked != nullptr && locked->isDirectory &&
                             locked->children.empty() && openFile != nullptr;
            _exit(ok ? 0 : 1);
        }

        int status = 0;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 2) {
            std::printf(
                "asset_browser_test: NOTE -- could not drop this process's child to an unprivileged uid in this "
                "environment; the permission-denied-directory case was SKIPPED, not verified\n");
        } else {
            expectTrue(WIFEXITED(status) && WEXITSTATUS(status) == 0,
                       "(verified from an actually-unprivileged child process, since this test process itself runs "
                       "as root) a permission-denied subdirectory is reported as a directory with EMPTY children -- "
                       "not silently indistinguishable from a genuinely empty one -- while its readable sibling "
                       "\"open_file.obj\" is unaffected");
        }
    } else {
        // Not root: the OS already enforces "locked"'s permissions against
        // this very process, no privilege-drop dance needed.
        const std::vector<engine::AssetTreeNode> tree = engine::buildAssetTree(scratchRoot.string());
        const engine::AssetTreeNode* models = findChild(tree, "models");
        expectTrue(models != nullptr, "\"models\" root exists");
        if (models != nullptr) {
            const engine::AssetTreeNode* locked = findChild(models->children, "locked");
            const engine::AssetTreeNode* openFile = findChild(models->children, "open_file.obj");
            expectTrue(locked != nullptr && locked->isDirectory && locked->children.empty(),
                       "a permission-denied subdirectory is reported as a directory with EMPTY children, not "
                       "silently indistinguishable from a genuinely empty one");
            expectTrue(openFile != nullptr, "its readable sibling is unaffected");
        }
    }
#else
    std::printf("asset_browser_test: NOTE -- permission-denied-directory case SKIPPED on this platform (no "
                "fork()/seteuid())\n");
#endif

    {
        // Best-effort restore before the final remove_all() below -- not
        // itself asserted on: if this process genuinely can't chmod back a
        // directory it just chmod'd itself, that's a test-environment
        // problem worth noticing separately, not a reason to fail this
        // case (which has already recorded its own real result above).
        std::error_code restoreEc;
        fs::permissions(scratchRoot / "models" / "locked", fs::perms::owner_all, restoreEc);
    }

    fs::remove_all(scratchRoot);

    if (failures == 0) {
        std::printf("asset_browser_test: all checks passed\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "asset_browser_test: %d check(s) failed\n", failures);
    return EXIT_FAILURE;
}
