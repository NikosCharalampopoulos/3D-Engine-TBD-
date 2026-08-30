// Phase 15g: see asset_drop.hpp's own header comment for the full design.
// This translation unit depends on nothing beyond <string> -- no ecs.hpp, no
// GLM, no GL/ImGui at all -- the same minimal-dependency shape
// asset_browser.cpp already has for the identical reason: tests/
// asset_drop_test.cpp links this file alone.

#include "engine/asset_drop.hpp"

namespace engine {

namespace {

constexpr const char* kModelPathPrefix = "assets/models/";
constexpr const char* kTexturePathPrefix = "assets/textures/";

}  // namespace

AssetDropCategory classifyAssetDropPath(const std::string& assetRelativePath) {
    if (assetRelativePath.rfind(kModelPathPrefix, 0) == 0) {
        return AssetDropCategory::kModel;
    }
    if (assetRelativePath.rfind(kTexturePathPrefix, 0) == 0) {
        return AssetDropCategory::kTexture;
    }
    return AssetDropCategory::kUnrecognized;
}

std::string modelBaseNameFromAssetPath(const std::string& assetRelativePath) {
    // Manual substr work, not std::filesystem::path::stem() -- this is name
    // parsing, not real filesystem I/O, the same reasoning model.cpp's own
    // directory-of-a-path helper already uses plain substr for (see that
    // file's own comment on its identical extraction).
    const std::size_t lastSlash = assetRelativePath.find_last_of('/');
    const std::string filename =
        lastSlash == std::string::npos ? assetRelativePath : assetRelativePath.substr(lastSlash + 1);
    const std::size_t lastDot = filename.find_last_of('.');
    const std::string stem = lastDot == std::string::npos ? filename : filename.substr(0, lastDot);
    return stem.empty() ? std::string("Model") : stem;
}

}  // namespace engine
