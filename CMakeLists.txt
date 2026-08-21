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

# --- GLAD (OpenGL 3.3 core loader) ----------------------------------------
# Vendored as plain source files rather than fetched: GLAD is a code
# generator, not a library with a stable git tag/CMake target, so a
# pre-generated set of glad.c/glad.h/khrplatform.h (public domain, generated
# for GL 3.3 core, no extensions) is checked into external/glad/. This keeps
# the build fully offline-reproducible for the loader itself.
add_library(glad STATIC external/glad/src/glad.c)
target_include_directories(glad PUBLIC external/glad/include)
target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})

# --- stb_image (single-header, vendored) ----------------------------------
add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE external/stb)

# --- engine_app ------------------------------------------------------------
add_executable(engine_app src/main.cpp)

target_include_directories(engine_app PRIVATE include)

target_link_libraries(engine_app PRIVATE
    glfw
    glm::glm
    glad
    stb_image
)

if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(engine_app PRIVATE Threads::Threads)
endif()

enable_testing()
add_subdirectory(tests)
