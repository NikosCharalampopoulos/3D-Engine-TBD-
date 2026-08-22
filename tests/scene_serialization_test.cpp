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
// Collider's own defaults would show up as a mismatch.
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

    std::filesystem::remove(tempPath);

    if (failures == 0) {
        std::printf("scene_serialization_test: all checks passed\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "scene_serialization_test: %d check(s) failed\n", failures);
    return EXIT_FAILURE;
}
