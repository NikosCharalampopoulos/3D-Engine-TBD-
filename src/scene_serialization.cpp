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

        records.push_back(std::move(record));
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
