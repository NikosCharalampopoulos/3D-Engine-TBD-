// Phase 8b: the pure-data half of scene (de)serialization -- JSON <->
// SceneEntityRecord only, no GL/ResourceManager/Shader/EntityRegistry
// dependency at all. See scene_serialization.hpp's own header comment for
// why this is split from loadScene()/saveScene() (src/scene_loader.cpp):
// this translation unit is exactly what tests/scene_serialization_test.cpp
// links against, so the round-trip test never needs a live OpenGL context
// -- it can't accidentally end up needing one, either, since nothing here
// #includes anything GL-adjacent.

#include "engine/scene_serialization.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <unordered_set>

#include "engine/log.hpp"

namespace engine {

namespace {

using json = nlohmann::json;

// Bug-review fix (post-8b): readVec3()/readQuat() below used to check only
// value.is_array() && value.size() == N before calling each element's
// .get<float>() -- so an element that's array-shaped-but-not-a-number (a
// string, bool, null, nested array/object) made .get<float>() throw
// nlohmann's own json::type_error straight out of parseSceneRecords(),
// un-LOG_ERROR'd and NOT a std::runtime_error, breaking both halves of this
// header's own documented contract ("Throws std::runtime_error (after
// LOG_ERROR'ing...) for every malformed-input case", explicitly including
// "a vector field that isn't a 3- or 4-element array of numbers" -- an
// array of non-numbers is exactly that case, just not one the original
// element-count-only check actually caught). Checking is_number() per
// element here, before any .get<float>() call, closes that gap the same
// way the sibling is_array()/size() checks already do.
bool allElementsAreNumbers(const json& array) {
    for (const json& element : array) {
        if (!element.is_number()) {
            return false;
        }
    }
    return true;
}

// Reads a 3-element JSON array of numbers as a glm::vec3, or returns
// fallback unchanged if `key` isn't present in `obj` at all -- see this
// file's header comment on why every transform field is optional
// (defaults to Transform's own identity value). Throws std::runtime_error
// if `key` IS present but isn't a 3-element array of numbers -- a present
// but malformed field is a schema error worth failing loudly on, unlike an
// absent one.
glm::vec3 readVec3(const json& obj, const char* key, const glm::vec3& fallback, const std::string& context) {
    if (!obj.contains(key)) {
        return fallback;
    }
    const json& value = obj.at(key);
    if (!value.is_array() || value.size() != 3 || !allElementsAreNumbers(value)) {
        throw std::runtime_error(context + ": \"" + key + "\" must be a 3-element array of numbers");
    }
    return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
}

// Same as readVec3, but for the 4-element [w, x, y, z] quaternion array --
// see scene_serialization.hpp's own comment on why rotation is stored as a
// quaternion, not Euler angles.
glm::quat readQuat(const json& obj, const char* key, const glm::quat& fallback, const std::string& context) {
    if (!obj.contains(key)) {
        return fallback;
    }
    const json& value = obj.at(key);
    if (!value.is_array() || value.size() != 4 || !allElementsAreNumbers(value)) {
        throw std::runtime_error(context + ": \"" + key + "\" must be a 4-element [w, x, y, z] array of numbers");
    }
    return glm::quat(value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>());
}

json writeVec3(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }

json writeQuat(const glm::quat& q) { return json::array({q.w, q.x, q.y, q.z}); }

// Phase 8e: reads a single optional boolean field, or returns `fallback`
// unchanged if `key` isn't present at all -- same "absent means default"
// convention as readVec3/readQuat above, just for RigidBody::useGravity's
// "gravity" field. Throws if `key` IS present but isn't a bool, for the
// same "a present-but-malformed field is a schema error" reason those two
// already document.
bool readBool(const json& obj, const char* key, bool fallback, const std::string& context) {
    if (!obj.contains(key)) {
        return fallback;
    }
    const json& value = obj.at(key);
    if (!value.is_boolean()) {
        throw std::runtime_error(context + ": \"" + key + "\" must be a boolean");
    }
    return value.get<bool>();
}

// Phase 8e: same as readBool, but for a single optional numeric field --
// used by Collider's "halfExtent".
float readFloat(const json& obj, const char* key, float fallback, const std::string& context) {
    if (!obj.contains(key)) {
        return fallback;
    }
    const json& value = obj.at(key);
    if (!value.is_number()) {
        throw std::runtime_error(context + ": \"" + key + "\" must be a number");
    }
    return value.get<float>();
}

}  // namespace

std::vector<SceneEntityRecord> parseSceneRecords(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("parseSceneRecords: could not open scene file \"" + path + "\"");
        throw std::runtime_error("parseSceneRecords: could not open scene file \"" + path + "\"");
    }

    json root;
    try {
        file >> root;
    } catch (const json::exception& e) {
        // Bug-review fix (post-8b): this used to catch only
        // json::parse_error, but nlohmann's own parser throws other
        // json::exception subclasses directly out of `file >> root` too --
        // e.g. json::out_of_range ("number overflow parsing '1e400'") for a
        // numeric literal too large to represent, which is syntactically
        // valid JSON but not a parse_error in nlohmann's own taxonomy. Both
        // are "this file's contents aren't valid JSON [this parser can
        // represent]" from this function's caller's point of view, so both
        // get the same std::runtime_error-after-LOG_ERROR treatment this
        // header's own malformed-input contract promises, using
        // json::exception (the common base of parse_error/out_of_range/
        // type_error/...) rather than parse_error alone. what() already
        // names the specific byte offset or reason either way, so it's
        // relayed verbatim exactly as parse_error's was.
        LOG_ERROR("parseSceneRecords: \"" + path + "\" is not valid JSON: " + e.what());
        throw std::runtime_error("parseSceneRecords: \"" + path + "\" is not valid JSON: " + e.what());
    }

    if (!root.is_object() || !root.contains("entities") || !root.at("entities").is_array()) {
        LOG_ERROR("parseSceneRecords: \"" + path + "\" has no top-level \"entities\" array");
        throw std::runtime_error("parseSceneRecords: \"" + path + "\" has no top-level \"entities\" array");
    }

    std::vector<SceneEntityRecord> records;
    const json& entities = root.at("entities");
    records.reserve(entities.size());

    for (std::size_t i = 0; i < entities.size(); ++i) {
        const json& entityJson = entities[i];
        const std::string context = "parseSceneRecords: \"" + path + "\" entities[" + std::to_string(i) + "]";
        if (!entityJson.is_object()) {
            throw std::runtime_error(context + " must be an object");
        }
        if (!entityJson.contains("name") || !entityJson.at("name").is_string()) {
            throw std::runtime_error(context + " is missing a string \"name\" field");
        }

        SceneEntityRecord record;
        record.name = entityJson.at("name").get<std::string>();

        // "transform" itself is optional (an entity with no Transform at
        // all, matching a future component-only entity) -- absent means
        // "leave every field at Transform's own identity default", exactly
        // like an absent individual position/rotation/scale field within a
        // present "transform" block does (see readVec3/readQuat above).
        if (entityJson.contains("transform")) {
            const json& transformJson = entityJson.at("transform");
            if (!transformJson.is_object()) {
                throw std::runtime_error(context + ": \"transform\" must be an object");
            }
            const std::string transformContext = context + ".transform";
            record.position = readVec3(transformJson, "position", record.position, transformContext);
            record.rotation = readQuat(transformJson, "rotation", record.rotation, transformContext);
            record.scale = readVec3(transformJson, "scale", record.scale, transformContext);
        }

        // "model" is optional -- see ecs.hpp's ModelComponent / this
        // header's schema comment: an entity can have a Transform with no
        // Model.
        if (entityJson.contains("model")) {
            const json& modelJson = entityJson.at("model");
            if (!modelJson.is_object() || !modelJson.contains("path") || !modelJson.at("path").is_string()) {
                throw std::runtime_error(context + ": \"model\" must be an object with a string \"path\" field");
            }
            record.modelPath = modelJson.at("path").get<std::string>();
        }

        // Phase 8e: "rigidBody"/"collider" are both optional, independent
        // blocks -- see this file's header's "Schema" comment. An empty
        // "rigidBody": {} is valid (every one of its own fields is itself
        // optional, defaulting to RigidBody's own defaults), so presence of
        // the key alone (not any particular field inside it) is what sets
        // hasRigidBody/hasCollider.
        if (entityJson.contains("rigidBody")) {
            const json& rigidBodyJson = entityJson.at("rigidBody");
            if (!rigidBodyJson.is_object()) {
                throw std::runtime_error(context + ": \"rigidBody\" must be an object");
            }
            record.hasRigidBody = true;
            const std::string rigidBodyContext = context + ".rigidBody";
            record.rigidBodyGravity = readBool(rigidBodyJson, "gravity", record.rigidBodyGravity, rigidBodyContext);
            record.rigidBodyVelocity =
                readVec3(rigidBodyJson, "velocity", record.rigidBodyVelocity, rigidBodyContext);
        }

        if (entityJson.contains("collider")) {
            const json& colliderJson = entityJson.at("collider");
            if (!colliderJson.is_object()) {
                throw std::runtime_error(context + ": \"collider\" must be an object");
            }
            record.hasCollider = true;
            record.colliderHalfExtent =
                readFloat(colliderJson, "halfExtent", record.colliderHalfExtent, context + ".collider");
        }

        // Phase 15e: "pointLight"/"directionalLight"/"camera" are all
        // independent, optional blocks -- same "presence alone sets has*,
        // each field inside is itself optional" shape "rigidBody"/"collider"
        // above already establish (see this file's header's "Schema"
        // comment).
        if (entityJson.contains("pointLight")) {
            const json& pointLightJson = entityJson.at("pointLight");
            if (!pointLightJson.is_object()) {
                throw std::runtime_error(context + ": \"pointLight\" must be an object");
            }
            record.hasPointLight = true;
            const std::string pointLightContext = context + ".pointLight";
            record.pointLightColor = readVec3(pointLightJson, "color", record.pointLightColor, pointLightContext);
            record.pointLightConstant =
                readFloat(pointLightJson, "constant", record.pointLightConstant, pointLightContext);
            record.pointLightLinear = readFloat(pointLightJson, "linear", record.pointLightLinear, pointLightContext);
            record.pointLightQuadratic =
                readFloat(pointLightJson, "quadratic", record.pointLightQuadratic, pointLightContext);
        }

        if (entityJson.contains("directionalLight")) {
            const json& directionalLightJson = entityJson.at("directionalLight");
            if (!directionalLightJson.is_object()) {
                throw std::runtime_error(context + ": \"directionalLight\" must be an object");
            }
            record.hasDirectionalLight = true;
            const std::string directionalLightContext = context + ".directionalLight";
            record.directionalLightDirection =
                readVec3(directionalLightJson, "direction", record.directionalLightDirection, directionalLightContext);
            record.directionalLightColor =
                readVec3(directionalLightJson, "color", record.directionalLightColor, directionalLightContext);
            // "active" -- see this file's header's own "Active directional
            // light" comment. Absent means false (an ordinary, inactive
            // light), the same "absent means the unremarkable default"
            // convention every other optional field in this schema follows.
            record.directionalLightActive =
                readBool(directionalLightJson, "active", record.directionalLightActive, directionalLightContext);
        }

        if (entityJson.contains("camera")) {
            const json& cameraJson = entityJson.at("camera");
            if (!cameraJson.is_object()) {
                throw std::runtime_error(context + ": \"camera\" must be an object");
            }
            record.hasCameraComponent = true;
            const std::string cameraContext = context + ".camera";
            record.cameraFovYDeg = readFloat(cameraJson, "fovYDeg", record.cameraFovYDeg, cameraContext);
            record.cameraNearPlane = readFloat(cameraJson, "nearPlane", record.cameraNearPlane, cameraContext);
            record.cameraFarPlane = readFloat(cameraJson, "farPlane", record.cameraFarPlane, cameraContext);
        }

        // Phase 14b: "parent" is a plain string naming another entity in
        // this same file -- see this file's header's own "Parent
        // references" comment for why a name, not a raw EntityId. Whether
        // that name actually resolves to another entity in this file is
        // checked below, in a second pass over every record, once every
        // entity's name is known -- not here, since the named parent may
        // not have been parsed yet (a child is allowed to appear before its
        // parent in "entities").
        if (entityJson.contains("parent")) {
            const json& parentJson = entityJson.at("parent");
            if (!parentJson.is_string()) {
                throw std::runtime_error(context + ": \"parent\" must be a string entity name");
            }
            record.parentName = parentJson.get<std::string>();
        }

        records.push_back(std::move(record));
    }

    // Phase 14b: second pass, now that every record's "name" is known --
    // every non-empty parentName must match some OTHER record's name
    // somewhere in this same file (see this file's header's own "Parent
    // references" comment). A name that matches nothing is a schema error
    // worth failing loudly on here, the same "validate at this boundary,
    // specific message" treatment every other malformed-input case in this
    // function already gets, rather than silently loading as an
    // (incorrectly) root-level entity or deferring the failure to
    // loadScene() where the offending JSON entity index is no longer
    // directly at hand.
    std::unordered_set<std::string> allNames;
    allNames.reserve(records.size());
    for (const SceneEntityRecord& record : records) {
        allNames.insert(record.name);
    }
    for (std::size_t i = 0; i < records.size(); ++i) {
        const SceneEntityRecord& record = records[i];
        if (!record.parentName.empty() && allNames.find(record.parentName) == allNames.end()) {
            const std::string context = "parseSceneRecords: \"" + path + "\" entities[" + std::to_string(i) + "]";
            LOG_ERROR(context + ": \"parent\" references unknown entity name \"" + record.parentName + "\"");
            throw std::runtime_error(context + ": \"parent\" references unknown entity name \"" + record.parentName +
                                      "\"");
        }
    }

    return records;
}

void writeSceneRecords(const std::vector<SceneEntityRecord>& records, const std::string& path) {
    json root;
    json entities = json::array();

    for (const SceneEntityRecord& record : records) {
        json entityJson;
        entityJson["name"] = record.name;
        entityJson["transform"] = {
            {"position", writeVec3(record.position)},
            {"rotation", writeQuat(record.rotation)},
            {"scale", writeVec3(record.scale)},
        };
        if (!record.modelPath.empty()) {
            entityJson["model"] = {{"path", record.modelPath}};
        }
        // Phase 8e: "rigidBody"/"collider" are written independently of
        // each other and of "model" -- see this file's header's "Schema"
        // comment on why all three are opt-in per entity.
        if (record.hasRigidBody) {
            entityJson["rigidBody"] = {
                {"gravity", record.rigidBodyGravity},
                {"velocity", writeVec3(record.rigidBodyVelocity)},
            };
        }
        if (record.hasCollider) {
            entityJson["collider"] = {{"halfExtent", record.colliderHalfExtent}};
        }
        // Phase 15e: "pointLight"/"directionalLight"/"camera" are written
        // independently of each other and of every block above, same
        // opt-in-per-entity treatment. Every field inside a present block is
        // written unconditionally (true or false, non-default or not) --
        // matching "rigidBody"'s own "gravity" field just above, not
        // omitted-when-default the way the outer has*-gated block itself is.
        if (record.hasPointLight) {
            entityJson["pointLight"] = {
                {"color", writeVec3(record.pointLightColor)},
                {"constant", record.pointLightConstant},
                {"linear", record.pointLightLinear},
                {"quadratic", record.pointLightQuadratic},
            };
        }
        if (record.hasDirectionalLight) {
            entityJson["directionalLight"] = {
                {"direction", writeVec3(record.directionalLightDirection)},
                {"color", writeVec3(record.directionalLightColor)},
                {"active", record.directionalLightActive},
            };
        }
        if (record.hasCameraComponent) {
            entityJson["camera"] = {
                {"fovYDeg", record.cameraFovYDeg},
                {"nearPlane", record.cameraNearPlane},
                {"farPlane", record.cameraFarPlane},
            };
        }
        // Phase 14b: "parent" is written independently of every other
        // block, same opt-in-per-entity treatment -- see this file's
        // header's "Schema" comment.
        if (!record.parentName.empty()) {
            entityJson["parent"] = record.parentName;
        }
        entities.push_back(std::move(entityJson));
    }
    root["entities"] = std::move(entities);

    std::ofstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("writeSceneRecords: could not open \"" + path + "\" for writing");
        throw std::runtime_error("writeSceneRecords: could not open \"" + path + "\" for writing");
    }
    // Pretty-printed (2-space indent) so the checked-in
    // assets/scenes/default.json is human-reviewable/diffable in a PR --
    // see scene_serialization.hpp's own "why JSON" comment.
    file << root.dump(2) << '\n';
}

}  // namespace engine
