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

# --- nlohmann/json: header-only JSON library (Phase 8b scene files) ------
# Fetched the same way as GLFW/GLM/Assimp above (a normal, tagged git
# dependency) rather than hand-vendored like GLAD/stb_image below -- see
# scene_serialization.hpp's own header comment for why those two cases
# don't apply here: GLAD is vendored because it's a *code generator* with
# no single canonical pre-generated file to pull a tag for, which has
# nothing to do with a JSON *library* that ships an ordinary tagged
# release. JSON_BuildTests is forced off for the same reason
# ASSIMP_BUILD_TESTS is above: this project has no use for a dependency's
# own test suite, only its (single-header) library.
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(nlohmann_json)

# --- Dear ImGui: debug UI overlay (Phase 8c) ------------------------------
# Fetched the same way as GLFW/GLM/Assimp/nlohmann_json above (an ordinary
# tagged git dependency), not hand-vendored like GLAD/stb_image -- see
# README.md's Phase 8b note on that same GLAD-vendoring-precedent question
# for nlohmann_json, which applies identically here: Dear ImGui is an
# actively-maintained library with tagged releases, not a code generator
# with no canonical pre-generated file to pull a tag for.
#
# Unlike every other FetchContent'd dependency above, though, Dear ImGui
# ships no CMakeLists.txt of its own at all -- by upstream design, it's meant
# to be compiled directly into whatever project embeds it (see its own
# docs/README's "Getting Started" section), not consumed as a pre-built
# library with its own build system. So instead of
# FetchContent_MakeAvailable() handing back ready-made targets the way
# glfw/glm/assimp/nlohmann_json do, this only fetches the source tree
# (FetchContent_Populate-via-MakeAvailable still works for that -- it just
# has no CMakeLists.txt to add_subdirectory() into) and the `imgui` target
# right below builds its core sources + the GLFW/OpenGL3 backend files
# itself, the same "small first-party add_library() target" shape this
# file's own `glad` target below already uses for the same underlying
# reason (a dependency with no ready-made CMake target of its own).
# Phase 14a: switched from v1.92.9b to v1.92.9b-docking. Upstream Dear ImGui
# has never merged its docking feature (DockSpace/DockBuilder, io.ConfigFlags
# |= ImGuiConfigFlags_DockingEnable, ImGuiViewport, etc.) into `master` -- it
# only ever ships on a permanently-separate `docking` branch/tag family that
# upstream releases in lockstep with each ordinary tagged version. This was
# *assumed* to already be merged into mainline by this phase's own brief; it
# is not (verified directly: `git ls-remote --tags` against upstream shows a
# `v1.92.9b-docking` tag alongside plain `v1.92.9b`, and a clone of the
# latter has zero occurrences of ImGuiConfigFlags_DockingEnable/DockBuilder*/
# DockSpaceOverViewport anywhere in imgui.h/imgui_internal.h -- confirmed by
# grep before touching a single line of this project's own code). The
# `-docking` tag is the exact same v1.92.9b release plus docking; nothing
# else about this project's ImGui usage/version changes.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9b-docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

# Core widget/draw-list sources plus the two backend files this engine
# actually needs: imgui_impl_glfw (this project's own windowing library,
# already a dependency above) and imgui_impl_opengl3 (a GL3-core-profile
# renderer backend, matching this engine's own GL 4.3/4.1 core context --
# see window.hpp). imgui_demo.cpp is deliberately NOT included: nothing in
# this engine ever calls ImGui::ShowDemoWindow(), so compiling it would only
# add build time for code no code path reaches.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
# IMGUI_IMPL_OPENGL_LOADER_GLAD tells imgui_impl_opengl3.cpp to #include
# <glad/glad.h> for its GL function pointers/enums instead of bundling its
# own generated loader (imgui_impl_opengl3_loader.h, its default) -- this
# engine already loads every GL entry point through glad (see window.cpp),
# so reusing that one loader avoids two independently-generated sets of GL
# function pointers/typedefs existing in the same binary. PRIVATE: only
# imgui_impl_opengl3.cpp's own compilation needs to see this macro, not
# anything that merely links against the `imgui` target.
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD)
target_link_libraries(imgui PUBLIC glfw glad)

# --- GLAD (OpenGL 3.3 core loader) ----------------------------------------
# Vendored as plain source files rather than fetched: GLAD is a code
# generator, not a library with a stable git tag/CMake target, so a
# pre-generated set of glad.c/glad.h/khrplatform.h (public domain, generated
# for GL 3.3 core, no extensions) is checked into external/glad/. This keeps
# the build fully offline-reproducible for the loader itself.
# (Phase 12: the *context* Window now requests is GL 4.3 core, but this
# loader's own function/enum set is unchanged -- nothing new is called this
# phase, see README.md's Phase 12 section. A 4.3 context still exposes every
# GL 3.3 entry point this loader already declares; new entry points only need
# adding here once something actually calls them, e.g. Phase 13d's compute
# shaders.)
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
    src/input_action_map.cpp
    src/resource_manager.cpp
    src/shadow_map.cpp
    src/framebuffer.cpp
    src/skybox.cpp
    src/ibl_probe.cpp
    src/hdri_loader.cpp
    src/paths.cpp
    src/compute_shader.cpp
    src/cluster_light_culler.cpp
    src/ssao.cpp
    src/scene_serialization.cpp
    src/scene_loader.cpp
    src/debug_ui.cpp
    src/physics.cpp
    src/editor_ui.cpp
    src/light.cpp
    src/transform_hierarchy.cpp
    src/scene_hierarchy.cpp
    src/asset_browser.cpp
    src/material_override.cpp
    src/asset_drop.cpp
    src/camera_capture.cpp
    src/editor_icons.cpp
    src/window_chrome.cpp
    src/gizmo.cpp
    src/camera_component.cpp
    src/shading_mode.cpp
    src/undo_stack.cpp
    src/scene_file_ops.cpp
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
    nlohmann_json::nlohmann_json
    imgui
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
#
# Phase 17b's new assets/fonts/editor-icons.ttf (see editor_ui.cpp's own
# EditorUI constructor comment) needs no change here at all -- copy_directory
# already recurses through every subdirectory assets/ has, fonts/ included,
# the same way it already picked up assets/textures/skybox/ and
# assets/textures/hdri/ without this command ever needing to name them.
add_custom_command(TARGET engine_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:engine_app>/assets"
    COMMENT "Copying assets/ next to engine_app"
)

enable_testing()
add_subdirectory(tests)
