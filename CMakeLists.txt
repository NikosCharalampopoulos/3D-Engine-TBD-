cmake_minimum_required(VERSION 3.16)
project(3DEngineTBD LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Phase 0 note: everything lives in one executable target (engine_app) for
# now. Future phases will likely want to split this into a static/shared
# "engine" library (src/ minus main.cpp) plus a thin app target that links
# against it, so tests/ and multiple front-ends can reuse engine code
# without recompiling. Not done yet to avoid over-engineering Phase 0.
# ---------------------------------------------------------------------------

include(FetchContent)

# --- GLFW: windowing + GL context creation -------------------------------
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
# This environment (and the Xvfb headless harness) only needs the X11
# backend; Wayland support requires wayland-scanner which isn't installed
# and isn't needed here.
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)

# --- GLM: header-only math library ---------------------------------------
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(glfw glm)

# --- Assimp: model/scene loading (Phase 5) --------------------------------
# Assimp is a big, general-purpose asset importer library; left at its
# defaults it also builds its own tools, sample apps, a full test suite, an
# export pipeline, and ~50 format importers, all of which cost real build
# time we don't need. Every cache variable below is set (as a CACHE BOOL
# ... FORCE) *before* FetchContent_Declare/MakeAvailable runs, so it wins
# over the OPTION() calls in Assimp's own CMakeLists.txt -- CMake's option()
# only initializes a cache variable if it doesn't already exist, so
# declaring ours first here means Assimp's later option() calls are no-ops
# that keep our values instead of resetting them to Assimp's own defaults.
#
# BUILD_SHARED_LIBS is forced OFF here too (Assimp's own ADD_LIBRARY(assimp
# ...) call has no explicit STATIC/SHARED and just follows this global
# variable, defaulting to ON upstream). Doing this after glfw/glm are already
# fetched+built above means it can't retroactively affect them; forcing it
# static avoids engine_app needing a runtime rpath/LD_LIBRARY_PATH pointing
# at build/_deps/assimp-build for a shared libassimp.so at run time, which
# matters for tools/run_headless.sh launching the executable directly.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
# Assimp defaults ASSIMP_WARNINGS_AS_ERRORS to ON; a newer host compiler than
# Assimp v5.4.3 was tested against can easily trip a warning this project
# doesn't own and has no business failing our build over -- Assimp's own
# warnings aren't ours to fix (see the phase brief), so just don't let them
# be fatal.
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
# Let Assimp vendor/build its own bundled zlib instead of relying on a
# system copy. This project's dev container happens to have zlib1g-dev
# installed, but that's not portable -- a fresh Windows/macOS checkout (or
# a Linux box without the -dev package) has no system zlib for Assimp's
# find_package(ZLIB) to find, which would otherwise break the build on any
# machine other than this one.
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)

# Restrict importers to just what this phase's Model class + test asset
# actually need: OBJ (assets/models/scene.obj, hand-authored for this phase)
# and glTF (the other common runtime-friendly format, cheap to also enable)
# instead of Assimp's full ~50-importer default set.
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG        v5.4.3
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(assimp)

# --- GLAD (OpenGL 3.3 core loader) ----------------------------------------
# Vendored as plain source files rather than fetched: GLAD is a code
# generator, not a library with a stable git tag/CMake target, so a
# pre-generated set of glad.c/glad.h/khrplatform.h (public domain, generated
# for GL 3.3 core, no extensions) is checked into external/glad/. This keeps
# the build fully offline-reproducible for the loader itself.
add_library(glad STATIC external/glad/src/glad.c)
target_include_directories(glad PUBLIC external/glad/include)
target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(glad PRIVATE -Wall -Wextra)
endif()

# --- stb_image (single-header, vendored) ----------------------------------
add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE external/stb)

# --- engine_app ------------------------------------------------------------
add_executable(engine_app
    src/main.cpp
    src/window.cpp
    src/application.cpp
    src/shader.cpp
    src/mesh.cpp
    src/camera.cpp
    src/texture.cpp
    src/model.cpp
    src/input.cpp
    src/resource_manager.cpp
    src/shadow_map.cpp
    src/framebuffer.cpp
    src/skybox.cpp
    src/paths.cpp
)

target_include_directories(engine_app PRIVATE include)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(engine_app PRIVATE -Wall -Wextra)
endif()

target_link_libraries(engine_app PRIVATE
    glfw
    glm::glm
    glad
    stb_image
    assimp
)

if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(engine_app PRIVATE Threads::Threads)
endif()

# Asset paths are resolved at runtime relative to the executable's own
# directory (see src/paths.cpp), not the process's current working
# directory -- so copy assets/ next to the built binary on every build.
# This makes the exe runnable from any cwd (a different terminal directory,
# double-clicking it in a file browser, a multi-config generator's
# Debug/Release subfolder, etc.) instead of only working when launched from
# the source tree root.
add_custom_command(TARGET engine_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:engine_app>/assets"
    COMMENT "Copying assets/ next to engine_app"
)

enable_testing()
add_subdirectory(tests)
