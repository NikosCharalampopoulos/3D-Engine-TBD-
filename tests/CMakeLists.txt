# Phase 8b adds the first real test: scene_serialization_test, a plain
# executable (see its own header comment for why not a fetched framework
# yet) registered with add_test() so `ctest` (from the build dir) actually
# runs it. It links ONLY against src/scene_serialization.cpp (the pure
# JSON<->SceneEntityRecord half of scene (de)serialization -- see
# include/engine/scene_serialization.hpp's own header comment) plus this
# project's already-fetched GLM and nlohmann/json, so it needs neither a
# GLFW window nor a live OpenGL context nor Assimp/GLAD to build or run --
# unlike engine_app, which needs all of those.
add_executable(scene_serialization_test
    scene_serialization_test.cpp
    ${CMAKE_SOURCE_DIR}/src/scene_serialization.cpp
)
target_include_directories(scene_serialization_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(scene_serialization_test PRIVATE glm::glm nlohmann_json::nlohmann_json)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(scene_serialization_test PRIVATE -Wall -Wextra)
endif()

add_test(NAME scene_serialization_test COMMAND scene_serialization_test)
