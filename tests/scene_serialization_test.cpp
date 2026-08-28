// Phase 8b's round-trip test: writes a handful of fabricated
// SceneEntityRecords out via writeSceneRecords(), reads them back via
// parseSceneRecords(), and checks every field survived -- proving the
// scene file format actually round-trips instead of only being eyeballed
// once. Links against scene_serialization.cpp ALONE (see
// scene_serialization.hpp's own header comment on why that file has no
// GL/ResourceManager/EntityRegistry dependency), so this test needs no
// window, no GL context, and no GPU -- unlike this engine's other
// verification (tools/run_headless.sh), which is how everything
// GL-touching in this project is checked instead, per this project's own
// "verify at the boundary that's actually load-bearing" approach.
//
// Not a full test framework (no Catch2/GoogleTest FetchContent'd) --
// tests/CMakeLists.txt was still the Phase 0 placeholder with nothing to
// extend (see this phase's README section), and one plain executable that
// returns nonzero on the first failed check is all this single test case
// needs; a real framework is the right call once there are enough test
// cases for its fixtures/reporting to earn their build-time cost.

#include "engine/scene_serialization.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expectTrue(bool condition, const std::string& what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

void expectVec3Near(const glm::vec3& actual, const glm::vec3& expected, const std::string& what) {
    expectTrue(glm::all(glm::epsilonEqual(actual, expected, 1e-5f)), what);
}

void expectQuatNear(const glm::quat& actual, const glm::quat& expected, const std::string& what) {
    expectTrue(glm::epsilonEqual(actual.w, expected.w, 1e-5f) && glm::epsilonEqual(actual.x, expected.x, 1e-5f) &&
                   glm::epsilonEqual(actual.y, expected.y, 1e-5f) && glm::epsilonEqual(actual.z, expected.z, 1e-5f),
               what);
}

// Exercises the schema's optional fields (see scene_serialization.hpp):
// one entity with every field explicitly set (including a "model" block)
// and one with only a name -- everything else should come back at
// Transform's own identity defaults, and modelPath should come back empty
// rather than some placeholder. A third entity (Phase 8e) exercises
// "rigidBody"/"collider" -- both present, with non-default field values, so
// a bug that silently dropped either block or fell back to RigidBody/
// Collider's own defaults would show up as a mismatch. A fourth/fifth pair
// (Phase 14b) exercises "parent" specifically listed CHILD-BEFORE-PARENT in
// vector (and therefore file) order, proving forward-reference resolution
// actually works rather than only happening to work when scenes are
// authored parent-first (see this file's own "Malformed input: 'parent'
// names an entity that doesn't exist" case, further below, for the sibling
// negative test). A sixth/seventh/eighth entity (Phase 15e) exercise
// "pointLight"/"directionalLight"/"camera" -- each with non-default field
// values, the same "a dropped block or a silent fall-back to the
// component's own struct defaults shows up as a mismatch" reasoning the
// rigidBody/collider case above already established. The directionalLight
// entity's "active" marker is deliberately set true here, and a SECOND
// directionalLight entity (ninth) is added with "active" left false, so the
// round-trip below can confirm the marker is preserved per-entity, not just
// "some boolean survived somewhere."
std::vector<engine::SceneEntityRecord> makeTestRecords() {
    std::vector<engine::SceneEntityRecord> records;

    engine::SceneEntityRecord full;
    full.name = "rotated_textured_thing";
    full.position = glm::vec3(1.5f, -2.0f, 3.25f);
    full.rotation = glm::angleAxis(glm::radians(37.0f), glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f)));
    full.scale = glm::vec3(2.0f, 0.5f, 1.0f);
    full.modelPath = "assets/models/scene.obj";
    records.push_back(full);

    engine::SceneEntityRecord bare;
    bare.name = "transform_only";
    records.push_back(bare);

    engine::SceneEntityRecord physical;
    physical.name = "falling_thing";
    physical.position = glm::vec3(0.0f, 3.0f, 0.0f);
    physical.modelPath = "assets/models/falling_cube.obj";
    physical.hasRigidBody = true;
    physical.rigidBodyGravity = false;  // deliberately non-default (true)
    physical.rigidBodyVelocity = glm::vec3(0.5f, -1.5f, 0.0f);
    physical.hasCollider = true;
    physical.colliderHalfExtent = 0.35f;  // deliberately non-default (0.25)
    records.push_back(physical);

    // Phase 14b: a fourth entity ("child_of_later") whose "parent" names a
    // FIFTH entity ("declared_later") that appears AFTER it in this same
    // vector -- writeSceneRecords() preserves vector order, so this
    // deliberately produces a JSON file where the child's own object comes
    // before its parent's, exercising parseSceneRecords()'s two-pass
    // "resolve parent names only after every entity's name is known"
    // handling (see scene_serialization.hpp's own "Parent references"
    // comment) rather than a file that happens to only ever list parents
    // first.
    engine::SceneEntityRecord childListedFirst;
    childListedFirst.name = "child_of_later";
    childListedFirst.position = glm::vec3(1.0f, 0.0f, 0.0f);
    childListedFirst.parentName = "declared_later";
    records.push_back(childListedFirst);

    engine::SceneEntityRecord parentListedSecond;
    parentListedSecond.name = "declared_later";
    parentListedSecond.position = glm::vec3(4.0f, 0.0f, 0.0f);
    records.push_back(parentListedSecond);

    // Phase 15e: a Point Light entity -- every pointLight* field deliberately
    // non-default (PointLight{}'s own defaults are plain white / (1.0, 0.7,
    // 1.8), see light.hpp), so a bug that silently fell back to those
    // defaults instead of actually round-tripping the authored values would
    // show up as a mismatch below.
    engine::SceneEntityRecord pointLight;
    pointLight.name = "lamp";
    pointLight.position = glm::vec3(0.0f, 2.0f, 0.0f);
    pointLight.hasPointLight = true;
    pointLight.pointLightColor = glm::vec3(1.0f, 0.6f, 0.2f);
    pointLight.pointLightConstant = 1.2f;
    pointLight.pointLightLinear = 0.45f;
    pointLight.pointLightQuadratic = 0.9f;
    records.push_back(pointLight);

    // A Directional Light entity that IS the active one -- "active": true
    // must round-trip exactly, not just direction/color.
    engine::SceneEntityRecord activeSun;
    activeSun.name = "sun";
    activeSun.hasDirectionalLight = true;
    activeSun.directionalLightDirection = glm::vec3(0.1f, -0.8f, 0.35f);
    activeSun.directionalLightColor = glm::vec3(1.0f, 0.85f, 0.6f);
    activeSun.directionalLightActive = true;
    records.push_back(activeSun);

    // A second Directional Light entity that is NOT active -- proves
    // "active" is tracked per-entity, not a single file-wide flag that
    // would (incorrectly) leak true onto every directionalLight block.
    engine::SceneEntityRecord inactiveMoon;
    inactiveMoon.name = "backup_moon";
    inactiveMoon.hasDirectionalLight = true;
    inactiveMoon.directionalLightDirection = glm::vec3(-0.2f, -0.5f, -0.1f);
    inactiveMoon.directionalLightColor = glm::vec3(0.4f, 0.5f, 0.9f);
    inactiveMoon.directionalLightActive = false;
    records.push_back(inactiveMoon);

    // A Camera entity -- fov/near/far all deliberately non-default
    // (CameraComponent{}'s own defaults are 60.0/0.1/100.0, see
    // camera_component.hpp).
    engine::SceneEntityRecord camera;
    camera.name = "scout_camera";
    camera.hasCameraComponent = true;
    camera.cameraFovYDeg = 75.5f;
    camera.cameraNearPlane = 0.25f;
    camera.cameraFarPlane = 500.0f;
    records.push_back(camera);

    return records;
}

}  // namespace

int main() {
    const std::filesystem::path tempPath =
        std::filesystem::temp_directory_path() / "engine_scene_serialization_test.json";

    // --- Round trip: write, then read back, and compare every field -----
    const std::vector<engine::SceneEntityRecord> original = makeTestRecords();
    engine::writeSceneRecords(original, tempPath.string());
    const std::vector<engine::SceneEntityRecord> reloaded = engine::parseSceneRecords(tempPath.string());

    expectTrue(reloaded.size() == original.size(), "reloaded entity count matches");
    if (reloaded.size() == original.size()) {
        for (std::size_t i = 0; i < original.size(); ++i) {
            const std::string tag = "entities[" + std::to_string(i) + "] (\"" + original[i].name + "\")";
            expectTrue(reloaded[i].name == original[i].name, tag + ".name");
            expectVec3Near(reloaded[i].position, original[i].position, tag + ".position");
            expectQuatNear(reloaded[i].rotation, original[i].rotation, tag + ".rotation");
            expectVec3Near(reloaded[i].scale, original[i].scale, tag + ".scale");
            expectTrue(reloaded[i].modelPath == original[i].modelPath, tag + ".modelPath");
            expectTrue(reloaded[i].hasRigidBody == original[i].hasRigidBody, tag + ".hasRigidBody");
            expectTrue(reloaded[i].rigidBodyGravity == original[i].rigidBodyGravity, tag + ".rigidBodyGravity");
            expectVec3Near(reloaded[i].rigidBodyVelocity, original[i].rigidBodyVelocity, tag + ".rigidBodyVelocity");
            expectTrue(reloaded[i].hasCollider == original[i].hasCollider, tag + ".hasCollider");
            expectTrue(reloaded[i].colliderHalfExtent == original[i].colliderHalfExtent, tag + ".colliderHalfExtent");
            expectTrue(reloaded[i].parentName == original[i].parentName, tag + ".parentName");

            // Phase 15e: PointLight/DirectionalLight/CameraComponent fields --
            // same "has* plus every value" coverage rigidBody/collider above
            // already get.
            expectTrue(reloaded[i].hasPointLight == original[i].hasPointLight, tag + ".hasPointLight");
            expectVec3Near(reloaded[i].pointLightColor, original[i].pointLightColor, tag + ".pointLightColor");
            expectTrue(reloaded[i].pointLightConstant == original[i].pointLightConstant, tag + ".pointLightConstant");
            expectTrue(reloaded[i].pointLightLinear == original[i].pointLightLinear, tag + ".pointLightLinear");
            expectTrue(reloaded[i].pointLightQuadratic == original[i].pointLightQuadratic,
                       tag + ".pointLightQuadratic");

            expectTrue(reloaded[i].hasDirectionalLight == original[i].hasDirectionalLight,
                       tag + ".hasDirectionalLight");
            expectVec3Near(reloaded[i].directionalLightDirection, original[i].directionalLightDirection,
                            tag + ".directionalLightDirection");
            expectVec3Near(reloaded[i].directionalLightColor, original[i].directionalLightColor,
                            tag + ".directionalLightColor");
            expectTrue(reloaded[i].directionalLightActive == original[i].directionalLightActive,
                       tag + ".directionalLightActive");

            expectTrue(reloaded[i].hasCameraComponent == original[i].hasCameraComponent,
                       tag + ".hasCameraComponent");
            expectTrue(reloaded[i].cameraFovYDeg == original[i].cameraFovYDeg, tag + ".cameraFovYDeg");
            expectTrue(reloaded[i].cameraNearPlane == original[i].cameraNearPlane, tag + ".cameraNearPlane");
            expectTrue(reloaded[i].cameraFarPlane == original[i].cameraFarPlane, tag + ".cameraFarPlane");
        }
    }

    // --- Malformed input: missing file --------------------------------
    bool threwForMissingFile = false;
    try {
        engine::parseSceneRecords((std::filesystem::temp_directory_path() / "engine_scene_test_missing.json").string());
    } catch (const std::exception&) {
        threwForMissingFile = true;
    }
    expectTrue(threwForMissingFile, "parseSceneRecords throws on a missing file instead of returning empty/crashing");

    // --- Malformed input: invalid JSON syntax ---------------------------
    const std::filesystem::path invalidJsonPath =
        std::filesystem::temp_directory_path() / "engine_scene_test_invalid.json";
    {
        std::ofstream invalidFile(invalidJsonPath);
        invalidFile << "{ this is not valid json ";
    }
    bool threwForInvalidJson = false;
    try {
        engine::parseSceneRecords(invalidJsonPath.string());
    } catch (const std::exception&) {
        threwForInvalidJson = true;
    }
    expectTrue(threwForInvalidJson, "parseSceneRecords throws on syntactically invalid JSON");
    std::filesystem::remove(invalidJsonPath);

    // --- Malformed input: valid JSON, wrong schema -----------------------
    const std::filesystem::path badSchemaPath = std::filesystem::temp_directory_path() / "engine_scene_test_schema.json";
    {
        std::ofstream badSchemaFile(badSchemaPath);
        // Valid JSON, but "entities" is an object, not an array -- and the
        // one nested "entity" has no "name" -- both schema violations
        // parseSceneRecords() should reject rather than silently coercing.
        badSchemaFile << R"({"entities": {"not": "an array"}})";
    }
    bool threwForBadSchema = false;
    try {
        engine::parseSceneRecords(badSchemaPath.string());
    } catch (const std::exception&) {
        threwForBadSchema = true;
    }
    expectTrue(threwForBadSchema, "parseSceneRecords throws when \"entities\" isn't an array");
    std::filesystem::remove(badSchemaPath);

    // --- Malformed input: "pointLight"/"directionalLight"/"camera" present
    // but not an object (Phase 15e) -- the identical "presence alone sets
    // has*, but the block itself must actually be an object" contract
    // "rigidBody"/"collider" already enforce (scene_serialization.cpp's own
    // `if (!xxxJson.is_object())` checks), exercised here for the three new
    // blocks the same way the "entities isn't an array" case above exercises
    // the top-level schema.
    const std::filesystem::path badPointLightPath =
        std::filesystem::temp_directory_path() / "engine_scene_test_bad_pointlight.json";
    {
        std::ofstream badPointLightFile(badPointLightPath);
        // "pointLight" as a plain string instead of an object.
        badPointLightFile << R"({"entities": [{"name": "lamp", "pointLight": "not an object"}]})";
    }
    bool threwForBadPointLight = false;
    try {
        engine::parseSceneRecords(badPointLightPath.string());
    } catch (const std::exception&) {
        threwForBadPointLight = true;
    }
    expectTrue(threwForBadPointLight, "parseSceneRecords throws when \"pointLight\" isn't an object");
    std::filesystem::remove(badPointLightPath);

    const std::filesystem::path badDirectionalLightPath =
        std::filesystem::temp_directory_path() / "engine_scene_test_bad_directionallight.json";
    {
        std::ofstream badDirectionalLightFile(badDirectionalLightPath);
        // "directionalLight" as a plain array instead of an object.
        badDirectionalLightFile << R"({"entities": [{"name": "sun", "directionalLight": [1, 2, 3]}]})";
    }
    bool threwForBadDirectionalLight = false;
    try {
        engine::parseSceneRecords(badDirectionalLightPath.string());
    } catch (const std::exception&) {
        threwForBadDirectionalLight = true;
    }
    expectTrue(threwForBadDirectionalLight, "parseSceneRecords throws when \"directionalLight\" isn't an object");
    std::filesystem::remove(badDirectionalLightPath);

    const std::filesystem::path badCameraPath =
        std::filesystem::temp_directory_path() / "engine_scene_test_bad_camera.json";
    {
        std::ofstream badCameraFile(badCameraPath);
        // "camera" as a plain boolean instead of an object.
        badCameraFile << R"({"entities": [{"name": "scout_camera", "camera": true}]})";
    }
    bool threwForBadCamera = false;
    try {
        engine::parseSceneRecords(badCameraPath.string());
    } catch (const std::exception&) {
        threwForBadCamera = true;
    }
    expectTrue(threwForBadCamera, "parseSceneRecords throws when \"camera\" isn't an object");
    std::filesystem::remove(badCameraPath);

    // --- Malformed input: "parent" names an entity that doesn't exist ----
    // Phase 14b: this is exactly the "validate at this boundary, specific
    // message" convention every other malformed-input case above already
    // gets -- see scene_serialization.hpp's own "Parent references"
    // comment on why this check has to happen at this (parseSceneRecords)
    // layer rather than loadScene()'s.
    const std::filesystem::path unknownParentPath =
        std::filesystem::temp_directory_path() / "engine_scene_test_unknown_parent.json";
    {
        std::ofstream unknownParentFile(unknownParentPath);
        unknownParentFile << R"({
            "entities": [
                { "name": "orphan", "parent": "nobody_by_this_name" }
            ]
        })";
    }
    bool threwForUnknownParent = false;
    try {
        engine::parseSceneRecords(unknownParentPath.string());
    } catch (const std::exception&) {
        threwForUnknownParent = true;
    }
    expectTrue(threwForUnknownParent,
               "parseSceneRecords throws when \"parent\" names an entity absent from the whole file");
    std::filesystem::remove(unknownParentPath);

    std::filesystem::remove(tempPath);

    if (failures == 0) {
        std::printf("scene_serialization_test: all checks passed\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "scene_serialization_test: %d check(s) failed\n", failures);
    return EXIT_FAILURE;
}
