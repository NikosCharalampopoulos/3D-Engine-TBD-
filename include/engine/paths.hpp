#ifndef ENGINE_PATHS_HPP
#define ENGINE_PATHS_HPP

#include <string>

namespace engine {

// Resolves a path (e.g. "assets/shaders/basic.vert") relative to the
// running executable's own directory, not the process's current working
// directory. Asset loading must not depend on cwd: the same binary needs
// to work whether launched from the source tree root, a build/Debug
// subfolder, or by double-clicking it in a file browser. CMake copies
// assets/ next to the built executable (see CMakeLists.txt's POST_BUILD
// step) so this always resolves correctly regardless of launch directory.
std::string resolveAssetPath(const std::string& relativePath);

}  // namespace engine

#endif  // ENGINE_PATHS_HPP
