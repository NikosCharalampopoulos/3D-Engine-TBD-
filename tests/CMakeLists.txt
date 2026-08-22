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

# Phase 8d adds input_action_map_test, the same "plain executable, links
# only the pure logic file it's testing" shape as scene_serialization_test
# above. input_action_map.cpp has no Window/GL dependency (see its own
# header comment) -- it only needs GLFW's headers for the GLFW_KEY_*
# constants its default bindings and this test are both built from, not a
# live GLFW/GL context -- so this links the already-fetched `glfw` target
# for its include directories/key-code macros alone, same as it would for
# any other file that merely references GLFW_KEY_* constants.
add_executable(input_action_map_test
    input_action_map_test.cpp
    ${CMAKE_SOURCE_DIR}/src/input_action_map.cpp
)
target_include_directories(input_action_map_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(input_action_map_test PRIVATE glfw)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(input_action_map_test PRIVATE -Wall -Wextra)
endif()

add_test(NAME input_action_map_test COMMAND input_action_map_test)
