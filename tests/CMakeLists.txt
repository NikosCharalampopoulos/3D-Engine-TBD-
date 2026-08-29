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

# Phase 8e adds physics_test, testing engine::stepPhysics() (src/physics.cpp)
# in isolation -- same "plain executable, links only the pure logic file
# it's testing" shape as scene_serialization_test/input_action_map_test
# above. physics.cpp depends only on ecs.hpp/transform.hpp (both plain data,
# no GL/Window dependency at all -- see ecs.hpp's own header comment), so
# this needs no live GL context/GPU either -- just GLM for glm::vec3.
add_executable(physics_test
    physics_test.cpp
    ${CMAKE_SOURCE_DIR}/src/physics.cpp
)
target_include_directories(physics_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(physics_test PRIVATE glm::glm)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(physics_test PRIVATE -Wall -Wextra)
endif()

add_test(NAME physics_test COMMAND physics_test)

# Phase 14b adds transform_hierarchy_test, testing
# engine::resolveWorldMatrix() (src/transform_hierarchy.cpp) in isolation --
# same "plain executable, links only the pure logic file it's testing"
# shape as physics_test above. transform_hierarchy.cpp depends only on
# ecs.hpp/transform.hpp/log.hpp (all header-only or GL-free -- see
# ecs.hpp's own header comment), so this needs no live GL context/GPU
# either -- just GLM for glm::mat4/vec3/quat.
add_executable(transform_hierarchy_test
    transform_hierarchy_test.cpp
    ${CMAKE_SOURCE_DIR}/src/transform_hierarchy.cpp
)
target_include_directories(transform_hierarchy_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(transform_hierarchy_test PRIVATE glm::glm)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(transform_hierarchy_test PRIVATE -Wall -Wextra)
endif()

add_test(NAME transform_hierarchy_test COMMAND transform_hierarchy_test)

# Phase 14d adds scene_hierarchy_test, testing engine::buildSceneTree()
# (src/scene_hierarchy.cpp) in isolation -- same "plain executable, links
# only the pure logic file it's testing" shape as transform_hierarchy_test
# above. scene_hierarchy.cpp depends only on ecs.hpp/transform.hpp/
# transform_hierarchy.hpp's Parent struct (a plain, header-only aggregate --
# no transform_hierarchy.cpp function is ever called), so this needs neither
# that .cpp file nor a live window/GL context/GPU/Dear ImGui frame.
#
# Phase 17b extends scene_hierarchy.cpp to also read light.hpp's PointLight/
# DirectionalLight and camera_component.hpp's CameraComponent (for
# SceneTreeNode's new icon-selection flags) -- both are header-only as far
# as a plain getComponent<T>() != nullptr check needs, so, like Parent
# above, this test needs neither light.cpp (whose real functions --
# resolveActiveDirectionalLight()/collectPointLights() -- are never called
# here, just the two component STRUCT definitions its header declares) nor
# camera_component.cpp (which has no .cpp at all, per that header's own
# comment) -- no new link dependency, same include-dir-only build this
# target already had.
add_executable(scene_hierarchy_test
    scene_hierarchy_test.cpp
    ${CMAKE_SOURCE_DIR}/src/scene_hierarchy.cpp
)
target_include_directories(scene_hierarchy_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(scene_hierarchy_test PRIVATE glm::glm)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(scene_hierarchy_test PRIVATE -Wall -Wextra)
endif()

add_test(NAME scene_hierarchy_test COMMAND scene_hierarchy_test)

# Phase 14f adds ecs_test, testing engine::EntityRegistry::destroyEntity()
# (include/engine/ecs.hpp) in isolation. Unlike every test target above, this
# links NO extra src/*.cpp file at all -- ecs.hpp is entirely header-only/
# templated (see its own header comment), so there is no matching
# src/ecs.cpp for a test of it to link against; the header alone is the
# whole implementation under test.
add_executable(ecs_test
    ecs_test.cpp
)
target_include_directories(ecs_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ecs_test PRIVATE -Wall -Wextra)
endif()

add_test(NAME ecs_test COMMAND ecs_test)

# Phase 15a adds light_test, testing engine::collectPointLights()
# (src/light.cpp) in isolation -- same "plain executable, links only the
# pure logic file it's testing" shape as physics_test/transform_hierarchy_test
# above. light.cpp depends only on ecs.hpp/transform.hpp (both header-only or
# GL-free -- a later review pass moved collectPointLights()'s own logging
# out to Application, so light.cpp no longer touches log.hpp either), so
# this needs no live GL context/GPU -- just GLM for glm::vec3.
add_executable(light_test
    light_test.cpp
    ${CMAKE_SOURCE_DIR}/src/light.cpp
)
target_include_directories(light_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(light_test PRIVATE glm::glm)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(light_test PRIVATE -Wall -Wextra)
endif()

add_test(NAME light_test COMMAND light_test)

# Phase 15c adds camera_component_test, testing engine::CameraComponent
# (include/engine/camera_component.hpp) in isolation. Unlike light_test
# above, camera_component.hpp has no matching src/*.cpp at all -- it's a
# plain data struct with no logic function of its own (see its own header
# comment) -- so, like ecs_test above, this links no extra src/*.cpp file;
# the header alone (plus ecs.hpp/transform.hpp, both header-only or GL-free)
# is the whole implementation under test. No GL context/GPU needed either --
# just GLM for glm::vec3/glm::quat (via Transform).
add_executable(camera_component_test
    camera_component_test.cpp
)
target_include_directories(camera_component_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(camera_component_test PRIVATE glm::glm)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(camera_component_test PRIVATE -Wall -Wextra)
endif()

add_test(NAME camera_component_test COMMAND camera_component_test)

# Phase 15d adds asset_browser_test, testing engine::buildAssetTree()
# (src/asset_browser.cpp) in isolation -- same "plain executable, links only
# the pure logic file it's testing" shape as scene_hierarchy_test above.
# asset_browser.cpp depends only on <filesystem> (no ecs.hpp, no GLM, no GL/
# ImGui at all), so unlike every test target above it needs no engine
# library dependency beyond the standard library itself.
add_executable(asset_browser_test
    asset_browser_test.cpp
    ${CMAKE_SOURCE_DIR}/src/asset_browser.cpp
)
target_include_directories(asset_browser_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(asset_browser_test PRIVATE -Wall -Wextra)
endif()

add_test(NAME asset_browser_test COMMAND asset_browser_test)

# Phase 15f adds material_override_test, testing
# engine::resolveDiffuseTextureOverride() (src/material_override.cpp) in
# isolation -- same "plain executable, links only the pure logic file it's
# testing" shape as light_test/physics_test above. material_override.cpp
# depends only on ecs.hpp (header-only, GL-free -- see that header's own
# comment), so this needs no live GL context/GPU, no Dear ImGui frame, and --
# per the test's own comment -- no real, GPU-resident Texture either, so this
# links no GLM/GLFW/GL target at all, the same minimal-dependency shape
# ecs_test above already has.
add_executable(material_override_test
    material_override_test.cpp
    ${CMAKE_SOURCE_DIR}/src/material_override.cpp
)
target_include_directories(material_override_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(material_override_test PRIVATE -Wall -Wextra)
endif()

add_test(NAME material_override_test COMMAND material_override_test)

# Phase 15g adds asset_drop_test, testing engine::classifyAssetDropPath()/
# modelBaseNameFromAssetPath() (src/asset_drop.cpp) in isolation -- same
# "plain executable, links only the pure logic file it's testing" shape as
# asset_browser_test above. asset_drop.cpp depends on nothing beyond
# <string> (no ecs.hpp, no GLM, no GL/ImGui at all), so, like
# asset_browser_test, this needs no engine library dependency beyond the
# standard library itself.
add_executable(asset_drop_test
    asset_drop_test.cpp
    ${CMAKE_SOURCE_DIR}/src/asset_drop.cpp
)
target_include_directories(asset_drop_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(asset_drop_test PRIVATE -Wall -Wextra)
endif()

add_test(NAME asset_drop_test COMMAND asset_drop_test)

# Phase 16 adds camera_capture_test, testing engine::decideCameraCapture()
# (src/camera_capture.cpp) in isolation -- same "plain executable, links only
# the pure logic file it's testing" shape as asset_drop_test/
# material_override_test above. camera_capture.cpp itself depends on nothing
# beyond its own header (no ecs.hpp, no GLM, no GL/ImGui at all).
#
# Post-review addition: also links src/input_action_map.cpp and the `glfw`
# target (for its GLFW_KEY_* constants/headers only, the identical
# header-only use input_action_map_test's own CMake entry above already
# makes -- no live GLFW/GL context needed) -- this test's own new "held key
# across consecutive polls" regression block drives a REAL
# engine::InputActionMap to reproduce the reported bug's exact integration
# (a real held Escape press's edge-detection feeding decideCameraCapture()
# across multiple frames), not just decideCameraCapture()'s own pure logic in
# isolation.
add_executable(camera_capture_test
    camera_capture_test.cpp
    ${CMAKE_SOURCE_DIR}/src/camera_capture.cpp
    ${CMAKE_SOURCE_DIR}/src/input_action_map.cpp
)
target_include_directories(camera_capture_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(camera_capture_test PRIVATE glfw)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(camera_capture_test PRIVATE -Wall -Wextra)
endif()

add_test(NAME camera_capture_test COMMAND camera_capture_test)

# Phase 17b adds editor_icons_test, testing engine::sceneNodeIconGlyph()/
# assetNodeIconGlyph() (src/editor_icons.cpp) in isolation -- same "plain
# executable, links only the pure logic file it's testing" shape as
# asset_drop_test/light_test above. editor_icons.cpp depends only on
# asset_drop.hpp's AssetDropCategory enum (no ecs.hpp, no GLM, no GL/ImGui
# at all), so this links src/asset_drop.cpp alongside it (for that enum's
# values, used as literal test inputs -- classifyAssetDropPath() itself is
# never called here) and needs no live GL context/GPU/Dear ImGui frame/font
# atlas whatsoever.
add_executable(editor_icons_test
    editor_icons_test.cpp
    ${CMAKE_SOURCE_DIR}/src/editor_icons.cpp
    ${CMAKE_SOURCE_DIR}/src/asset_drop.cpp
)
target_include_directories(editor_icons_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(editor_icons_test PRIVATE -Wall -Wextra)
endif()

add_test(NAME editor_icons_test COMMAND editor_icons_test)

# Phase 17d adds window_chrome_test, testing engine::applyDragDelta()/
# decideMaximizeRestoreToggle() (src/window_chrome.cpp) in isolation -- same
# "plain executable, links only the pure logic file it's testing" shape as
# camera_capture_test/editor_icons_test above. window_chrome.cpp depends on
# nothing beyond <cmath> (no ecs.hpp, no GLM, no GLFW/GL/ImGui at all), so
# this needs no live GL context/GPU/Dear ImGui frame/real GLFW window
# whatsoever.
add_executable(window_chrome_test
    window_chrome_test.cpp
    ${CMAKE_SOURCE_DIR}/src/window_chrome.cpp
)
target_include_directories(window_chrome_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(window_chrome_test PRIVATE -Wall -Wextra)
endif()

add_test(NAME window_chrome_test COMMAND window_chrome_test)
