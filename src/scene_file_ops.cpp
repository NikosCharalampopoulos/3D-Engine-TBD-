// Phase 18i: see scene_file_ops.hpp's own header comment for the full
// design. This translation unit depends on nothing beyond <filesystem>
// (plus <cctype>/<algorithm> for pure string work) -- no ecs.hpp, no GLM, no
// GL/ImGui at all -- the same minimal-dependency shape asset_browser.cpp/
// asset_drop.cpp already have for the identical reason: tests/
// scene_file_ops_test.cpp links this file alone.

#include "engine/scene_file_ops.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

#include "engine/log.hpp"

namespace engine {

namespace {

namespace fs = std::filesystem;

constexpr const char* kSceneFileExtension = ".json";

// Every character sanitizeSceneName() lets through verbatim -- letters,
// digits, '_', '-'. See scene_file_ops.hpp's own top comment for why '.'
// (and therefore any possibility of "..") is deliberately NOT in this set.
bool isAllowedSceneNameChar(unsigned char c) {
    return std::isalnum(c) != 0 || c == '_' || c == '-';
}

}  // namespace

std::optional<std::string> sanitizeSceneName(const std::string& rawInput) {
    std::string out;
    out.reserve(rawInput.size());
    for (unsigned char c : rawInput) {
        if (isAllowedSceneNameChar(c)) {
            out.push_back(static_cast<char>(c));
        } else if (std::isspace(c) != 0) {
            // Interior whitespace collapses to '_' rather than being
            // dropped outright -- see this header's own top comment for why
            // ("My Cool Scene" should become "My_Cool_Scene", not
            // "MyCoolScene"). A run of several whitespace characters in a
            // row (e.g. "My   Scene") collapses to several '_'s in a row,
            // not just one -- harmless in a filename, and not worth the
            // extra bookkeeping a "collapse consecutive underscores" pass
            // would add for a cosmetic-only difference.
            out.push_back('_');
        }
        // Every other character (path separators, '.', punctuation, control
        // characters, anything outside ASCII) is simply dropped.
    }

    // Leading/trailing '_'s left over from stripped/whitespace characters at
    // either end (e.g. "  My Scene!!" -> "_My_Scene" before this trim) are
    // cosmetic noise, not anything unsafe -- trimmed purely so a name a user
    // typed with incidental leading/trailing punctuation doesn't come out
    // looking like it was meant to start or end with an underscore.
    std::size_t begin = out.find_first_not_of('_');
    if (begin == std::string::npos) {
        // Nothing but underscores (or nothing at all) survived sanitizing.
        return std::nullopt;
    }
    std::size_t end = out.find_last_not_of('_');
    out = out.substr(begin, end - begin + 1);

    if (out.empty()) {
        return std::nullopt;
    }

    // Defensive cap, not a real limit any legitimate name would hit --
    // keeps a pathologically long paste from producing an equally
    // pathologically long filename. Truncated, not rejected: a name that's
    // merely too long is still a perfectly usable name once shortened,
    // unlike an empty one.
    constexpr std::size_t kMaxSceneNameLength = 64;
    if (out.size() > kMaxSceneNameLength) {
        out.resize(kMaxSceneNameLength);
        // Re-trim in case truncation left a trailing '_'.
        end = out.find_last_not_of('_');
        out = out.substr(0, end + 1);
        if (out.empty()) {
            return std::nullopt;
        }
    }

    return out;
}

std::string sceneRelativePathForName(const std::string& sanitizedName) {
    return "assets/scenes/" + sanitizedName + kSceneFileExtension;
}

std::vector<std::string> listSceneFileNames(const std::string& scenesDir) {
    std::vector<std::string> names;

    const fs::path dir(scenesDir);
    std::error_code existsError;
    if (!fs::exists(dir, existsError) || !fs::is_directory(dir, existsError)) {
        // A missing assets/scenes/ directory is unremarkable, not an error
        // -- see this header's own comment.
        return names;
    }

    std::error_code iterError;
    fs::directory_iterator it(dir, iterError);
    if (iterError) {
        LOG_WARN("listSceneFileNames: could not list \"" + scenesDir + "\" (" + iterError.message() +
                  ") -- returning an empty list instead of aborting");
        return names;
    }

    const fs::directory_iterator end;
    while (it != end) {
        const fs::directory_entry& entry = *it;
        std::error_code typeError;
        const bool isRegularFile = entry.is_regular_file(typeError);
        if (!typeError && isRegularFile) {
            const fs::path& entryPath = entry.path();
            if (entryPath.extension().string() == kSceneFileExtension) {
                names.push_back(entryPath.stem().string());
            }
        }
        // An entry whose type can't be determined (a broken symlink, a
        // permission issue) is simply skipped, the same "one bad entry
        // doesn't abort the whole listing" tolerance asset_browser.cpp's own
        // buildNode() establishes -- unlike that function, this one has no
        // caller-visible way to report a single skipped entry (there's no
        // per-entry "node" here to represent a skip with), so it's just
        // silently left out of the result.
        it.increment(iterError);
        if (iterError) {
            LOG_WARN("listSceneFileNames: stopped listing \"" + scenesDir + "\" partway through (" +
                      iterError.message() + ") -- keeping the " + std::to_string(names.size()) +
                      " file(s) already found instead of discarding them");
            break;
        }
    }

    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace engine
