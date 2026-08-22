#ifndef ENGINE_SCENE_SERIALIZATION_HPP
#define ENGINE_SCENE_SERIALIZATION_HPP

// Phase 8b: saves/loads the ECS's entity data (registry_'s Transform +
// ModelComponent + NameComponent pools -- see ecs.hpp) to/from a JSON file
// on disk, so the scene Application builds at startup can come from data
// instead of the hardcoded registry_.create()/addComponent<T>() call
// sequence application.cpp's constructor used through Phase 8a (see this
// class's own Phase 8b comment in application.hpp/.cpp for how that call
// site changes).
//
// --- Format: why JSON via a vendored single-header library -------------
// This project has no existing serialization format to reuse, so Phase 8b
// picks one. CMakeLists.txt already FetchContent's three real dependencies
// (GLFW, GLM, Assimp) as tagged git repositories, and nlohmann/json is
// exactly that same kind of dependency -- a normal, git-tagged, MIT-licensed
// library with a stable release -- NOT the GLAD/stb_image case (see
// CMakeLists.txt's own "GL loader" comment): GLAD is vendored by hand
// because it's a *code generator* with no single canonical pre-generated
// file to pull a git tag for, which has nothing to do with why a JSON
// *library* would need hand-vendoring. So nlohmann/json is FetchContent'd
// like GLFW/GLM/Assimp, not vendored like GLAD/stb_image. Concretely:
// single-header (nlohmann/json.hpp, no separate build step, no extra link
// target beyond an INTERFACE library), human-readable/diffable (so
// assets/scenes/default.json is reviewable in a PR the same way
// assets/models/scene.obj's text format already is), and expressive enough
// for nested objects/arrays without hand-rolling a parser -- a hand-rolled
// line-oriented format would work for today's single-entity scene but
// would need real design work (escaping, nesting) the moment a later phase
// wants a nested/optional field, which JSON already provides for free.
//
// --- Schema -------------------------------------------------------------
// {
//   "entities": [
//     {
//       "name": "scene",                          // human-readable only
//       "transform": {                             // all three optional,
//         "position": [x, y, z],                   // default to
//         "rotation": [w, x, y, z],                 // Transform's own
//         "scale":    [x, y, z]                     // identity defaults
//       },
//       "model": { "path": "assets/models/foo.obj" }  // optional block
//     }
//   ]
// }
//
// Each entity is "an array of named component blocks" ("transform",
// "model", ...) rather than a schema hardcoded to today's exactly-one
// (Transform, Model) shape -- so Phase 8c-8e adding a new component type
// (e.g. a physics body) is a new named block + a new parse/serialize
// branch here, not a schema rewrite. "model" is an optional block (an
// entity with a Transform but no Model is valid, matching ecs.hpp's
// "components are opt-in per entity" design) for the same reason.
//
// Rotation is stored as a quaternion, [w, x, y, z] -- the same order
// glm::quat's own constructor and Transform::rotation() use (see
// transform.hpp) -- rather than Euler angles, for the identical
// gimbal-lock reason transform.hpp itself already documents; storing
// Euler angles here would silently reintroduce that problem one file away
// from where it was originally avoided.
//
// --- Two-stage load: pure data vs. GL-touching, in two .cpp files -------
// parseSceneRecords()/writeSceneRecords() (implemented in
// scene_serialization.cpp) do ONLY JSON <-> plain-data (SceneEntityRecord)
// conversion -- that file doesn't even #include ecs.hpp/model.hpp/
// resource_manager.hpp, let alone anything GL-touching. loadScene()/
// saveScene() (implemented in scene_loader.cpp) are the thin
// EntityRegistry-facing wrappers that add the one GL-touching step in
// between (turning a model *path* into an actual GPU-resident Model via
// ResourceManager::getModel()). This split into two .cpp files sharing one
// .hpp exists so the round-trip test
// (tests/scene_serialization_test.cpp) can link against
// scene_serialization.cpp ALONE and exercise the real save/parse code path
// without needing a live OpenGL context (or even linking GLFW/Assimp/GL at
// all): it fabricates SceneEntityRecords, round-trips them through
// writeSceneRecords()/parseSceneRecords(), and compares -- exactly the
// same JSON (de)serialization code loadScene()/saveScene() call, just
// without also standing up a window+GL context the way every other
// GL-dependent piece of this engine is verified (headlessly, via
// tools/run_headless.sh -- see this phase's README section), not via a
// unit test.

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace engine {

class EntityRegistry;
class ResourceManager;
class Shader;

// One entity's plain-data record: exactly the fields the "Schema" comment
// above describes, already converted from JSON types (arrays of numbers)
// to engine types (glm::vec3/glm::quat) -- or the reverse, ready to be
// dumped back to JSON. modelPath is empty when the entity has no "model"
// block (see ecs.hpp's ModelComponent -- an entity can have a Transform
// with no Model, matching the ECS's opt-in-per-component design).
struct SceneEntityRecord {
    std::string name;
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
    std::string modelPath;
};

// Parses `path` as a scene JSON file (see this header's "Schema" comment)
// into plain-data records, doing no GL/ResourceManager work at all. Throws
// std::runtime_error (after LOG_ERROR'ing a specific, human-readable
// reason -- matching Shader/Texture/Model's own load-failure convention,
// see shader.cpp/texture.cpp/model.cpp) for every malformed-input case this
// phase is responsible for validating at this boundary: the file doesn't
// exist/can't be opened, its contents aren't valid JSON, or its JSON is
// valid but doesn't match the schema above (e.g. "entities" missing/not an
// array, an entity missing "name", a "transform"/"model" block present but
// not an object, a vector field that isn't a 3- or 4-element array of
// numbers). Does NOT check whether a referenced model *path* exists on
// disk or is a loadable model -- that's loadScene()'s job (see below),
// since resolving/validating asset paths is a GL-adjacent, ResourceManager
// concern this pure-data function has no dependency on.
std::vector<SceneEntityRecord> parseSceneRecords(const std::string& path);

// Serializes `records` to `path` as pretty-printed JSON matching the same
// schema, overwriting any existing file. Throws std::runtime_error (after
// LOG_ERROR) if `path`'s file can't be opened for writing (e.g. a missing
// parent directory or a read-only filesystem).
void writeSceneRecords(const std::vector<SceneEntityRecord>& records, const std::string& path);

// Reconstructs registry's entities from the scene file at `path`: one
// EntityId per record, with a NameComponent, a Transform, and (only when
// the record's modelPath is non-empty) a ModelComponent whose Model comes
// from resources.getModel(), loaded/linked against `shader` exactly like
// Application's own pre-Phase-8b hardcoded construction did. Registers
// entities into whatever `registry` already contains -- it does not clear
// it first -- so a caller loading more than one scene file in sequence
// gets the union, matching EntityRegistry's own "create() always allocates
// a fresh index" semantics.
//
// Throws std::runtime_error (after LOG_ERROR, naming the offending
// entity's name and path) if a record's modelPath doesn't resolve to a
// file that exists on disk, or if ResourceManager::getModel()/Model's own
// Assimp import fails for it -- both are surfaced here rather than left to
// propagate a bare, entity-less error up from deep inside Model's own
// constructor, since a multi-entity scene file needs to say *which*
// entity's asset reference was bad. Also propagates parseSceneRecords()'s
// own exceptions unchanged (missing file / invalid JSON / bad schema).
void loadScene(EntityRegistry& registry, const std::string& path, ResourceManager& resources, Shader& shader);

// Serializes every entity in `registry` that has at least a Transform
// component (Phase 8b's scene has nothing without one, but a future
// component-only entity -- e.g. a light with no Transform -- simply isn't
// representable by this schema yet, matching this phase's "extensible
// shape, not speculative handlers" scope) to `path`. An entity's "name"
// field comes from its NameComponent if it has one, or a generated
// "entity_<index>" placeholder if it doesn't (so saveScene() never fails
// or drops an entity purely for lacking a name) -- see ecs.hpp's
// NameComponent comment for why names are their own opt-in component
// rather than a mandatory EntityId field.
//
// Takes `registry` by non-const reference, not const&, even though this
// function only reads it: EntityRegistry::each<T>()/pool<T>() (see
// ecs.hpp) have no const overload -- each<T>() lazily creates T's pool on
// first use the same way addComponent<T>() does, which is why pool
// look-up isn't const there either -- and adding one purely for this one
// read-only caller would be speculative API surface on a Phase 8a file
// this phase doesn't otherwise need to change.
void saveScene(EntityRegistry& registry, const std::string& path);

}  // namespace engine

#endif  // ENGINE_SCENE_SERIALIZATION_HPP
