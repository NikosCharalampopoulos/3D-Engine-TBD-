#include "engine/paths.hpp"

#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace engine {

namespace {

std::filesystem::path getExecutableDir() {
#if defined(_WIN32)
    // MAX_PATH (260 chars) is not hit by any realistic install location for
    // this project; falling back to the cwd in the pathological case where
    // it is truncated just reintroduces the cwd-dependent behavior this
    // function exists to avoid, but that's an acceptable, rare edge case
    // rather than justifying a growing-buffer retry loop here.
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(buffer).parent_path();
#else
    char buffer[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return std::filesystem::current_path();
    }
    buffer[length] = '\0';
    return std::filesystem::path(buffer).parent_path();
#endif
}

}  // namespace

std::string resolveAssetPath(const std::string& relativePath) {
    static const std::filesystem::path exeDir = getExecutableDir();
    return (exeDir / relativePath).string();
}

}  // namespace engine
