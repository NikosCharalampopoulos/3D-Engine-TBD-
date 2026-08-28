#include "engine/asset_browser.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <utility>

#include "engine/log.hpp"

namespace engine {

namespace {

namespace fs = std::filesystem;

// See asset_browser.hpp's own header comment for exactly why these two --
// and only these two -- directories are "browsable," not assets/shaders/ or
// assets/scenes/ (both genuinely present on disk alongside these).
constexpr const char* kBrowsableCategories[] = {"models", "textures"};

// Bug-review fix (this phase's own review): determines whether `entryPath`
// is a directory WITHOUT ever throwing. The plain fs::is_directory(path)
// overload has to follow symlinks to answer "directory or not," and can
// throw std::filesystem_error for a path it cannot safely resolve that way
// -- a self-referencing/looping symlink (ELOOP) or a permission-denied
// parent are both realistic on-disk states (a stray symlink loop from an
// install artifact or backup tool, not a hypothetical), reproduced directly
// by this project's own review against a scratch assets/models/ tree. Sets
// `ok` to false when the type genuinely could not be determined -- the
// return value is meaningless in that case; every call site below checks
// `ok` before trusting it.
bool tryIsDirectory(const fs::path& entryPath, bool& ok) {
    std::error_code ec;
    const bool isDir = fs::is_directory(entryPath, ec);
    ok = !ec;
    return isDir;
}

// Recursively builds AT MOST one node for `entryPath` (a real, or believed-
// real, file or directory) and appends it to `outSiblings` -- appends
// NOTHING when this entry's own type can't be determined at all (see
// tryIsDirectory() above), which is this function's whole "degrade
// gracefully" mechanism: a single unreadable entry (broken/looping symlink,
// permission denied, removed out from under this walk mid-scan) is simply
// left out of the tree, exactly like a missing top-level category directory
// already was before this bug-review fix (buildAssetTree()'s own comment
// below) -- one bad entry never aborts the walk of everything else, or the
// whole engine's own startup (see this file's own review-fix comment on
// why that was the actual severity here: buildAssetTree() runs inside
// EditorUI's constructor, inside Application's own constructor, so an
// uncaught exception here previously meant the entire engine failed to
// launch over a filesystem hiccup confined to one asset subdirectory).
// `relativePath` already carries the assets/-relative path this node
// should report (see AssetTreeNode::relativePath's own comment).
void buildNode(const fs::path& entryPath, const std::string& relativePath, std::vector<AssetTreeNode>& outSiblings) {
    bool typeKnown = false;
    const bool isDir = tryIsDirectory(entryPath, typeKnown);
    if (!typeKnown) {
        LOG_WARN("Asset Browser: skipping unreadable entry \"" + relativePath +
                  "\" (broken/looping symlink, permission issue, or similar -- see asset_browser.cpp's own "
                  "buildNode() comment)");
        return;
    }

    AssetTreeNode node;
    node.name = entryPath.filename().string();
    node.relativePath = relativePath;
    node.isDirectory = isDir;

    if (isDir) {
        // Bug-review fix (second pass): deliberately NOT
        // fs::directory_options::skip_permission_denied. That flag's own
        // documented behavior is to swallow an EACCES-on-open failure
        // silently -- the iterator just produces zero entries, without ever
        // setting `iterEc` -- which is indistinguishable, both to this
        // function's own error_code check below and to a level designer
        // looking at the rendered Assets panel, from a directory that is
        // genuinely, unremarkably empty (this file's own first-pass fix
        // used that flag specifically to make a permission-denied directory
        // degrade for free, without noticing that "for free" meant "with no
        // diagnostic anywhere," an oversight the review's own second pass
        // caught). Omitting the flag means a permission-denied directory
        // instead falls through to the plain error_code failure path below
        // -- the exact same LOG_WARN-and-continue treatment every other
        // unreadable thing in this file already gets, just reached via
        // directory_iterator's own construction failure instead of
        // tryIsDirectory() above.
        std::error_code iterEc;
        fs::directory_iterator it(entryPath, iterEc);
        if (iterEc) {
            LOG_WARN("Asset Browser: could not list directory \"" + relativePath + "\" (" + iterEc.message() +
                      ") -- showing it with no children instead of aborting the whole tree build");
        } else {
            // Gather this directory's own direct entries first, then sort
            // as a whole -- directory_iterator's own enumeration order is
            // unspecified (see this header's own "Ordering" comment), so
            // there's no meaningful partial order to maintain by sorting
            // incrementally as entries arrive.
            //
            // Bug-review fix: advancing a directory_iterator (operator++)
            // can ALSO throw mid-iteration, not just at construction above
            // -- a directory that starts out readable but hits a
            // transient error partway through enumeration (e.g. something
            // removed concurrently) is the same "degrade, don't crash"
            // case as everything else in this function. `it.increment(ec)`
            // is the error_code-safe way to advance for exactly this
            // reason; a plain range-based for loop (which calls the
            // throwing operator++()) would silently reopen this same bug
            // one line down from the fix.
            std::vector<fs::directory_entry> entries;
            const fs::directory_iterator end;
            while (it != end) {
                entries.push_back(*it);
                it.increment(iterEc);
                if (iterEc) {
                    LOG_WARN("Asset Browser: stopped listing \"" + relativePath + "\" partway through (" +
                              iterEc.message() + ") -- keeping the " + std::to_string(entries.size()) +
                              " entr(y/ies) already found instead of discarding them");
                    break;
                }
            }

            std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
                // Directories before files (the "sort like a real
                // file-browser" convention this header's own comment
                // names), alphabetical within each group.
                //
                // Bug-review fix: also error_code-guarded, for the same
                // reason as everywhere else in this file -- a comparator
                // std::sort calls that can throw leaves std::sort's own
                // behavior on that exception unspecified (unlike a plain,
                // single call site, a predicate throwing mid-sort can
                // leave the range only partially rearranged). An entry
                // whose type can't be determined here is conservatively
                // sorted as "not a directory" for ordering purposes only
                // -- it's still genuinely skipped, via tryIsDirectory()
                // above, when this same entry's own node is actually built
                // a few lines below.
                std::error_code aEc;
                std::error_code bEc;
                const bool aIsDir = a.is_directory(aEc) && !aEc;
                const bool bIsDir = b.is_directory(bEc) && !bEc;
                if (aIsDir != bIsDir) {
                    return aIsDir;
                }
                return a.path().filename().string() < b.path().filename().string();
            });

            node.children.reserve(entries.size());
            for (const fs::directory_entry& entry : entries) {
                const std::string childRelative = relativePath + "/" + entry.path().filename().string();
                buildNode(entry.path(), childRelative, node.children);
            }
        }
    }

    outSiblings.push_back(std::move(node));
}

}  // namespace

std::vector<AssetTreeNode> buildAssetTree(const std::string& assetsRoot) {
    std::vector<AssetTreeNode> roots;
    const fs::path root(assetsRoot);

    for (const char* category : kBrowsableCategories) {
        const fs::path categoryPath = root / category;
        // A missing category directory is silently skipped, not an error --
        // see this header's own comment on why an absent/empty category is
        // unremarkable. Uses the non-throwing overloads (an std::error_code
        // out-param instead of an exception) specifically because "this
        // category doesn't exist" is an expected, ordinary outcome here, not
        // an exceptional one -- unlike, say, a malformed model file Model's
        // own Assimp-backed constructor genuinely should throw on.
        std::error_code existsError;
        if (!fs::exists(categoryPath, existsError) || !fs::is_directory(categoryPath, existsError)) {
            continue;
        }
        // Routed through buildNode() (not pushed directly) so a category
        // root gets the exact same "unreadable -> skip gracefully" handling
        // as everything beneath it, rather than a second, separately-
        // maintained copy of that logic here.
        buildNode(categoryPath, category, roots);
    }
    return roots;
}

}  // namespace engine
