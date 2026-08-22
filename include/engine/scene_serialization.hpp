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
// Phase 8e extends this with two more optional, independently-opt-in
// component blocks -- "rigidBody" and "collider" (see physics.hpp for
// RigidBody/Collider themselves) -- exactly the way this header's own
// Phase 8b "Schema" comment below said a later phase's physics body would
// be added: a new named block plus a new parse/serialize branch, not a
// schema rewrite. scene_serialization.cpp (the pure JSON<->record half)
// still doesn't depend on physics.hpp/ecs.hpp at all -- it only reads/
// writes the plain bool/float/vec3 fields SceneEntityRecord now carries for
// them; only scene_loader.cpp's loadScene()/saveScene() (the
// EntityRegistry-facing half) turns those fields into/out of real
// RigidBody/Collider components.
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
//       "model": { "path": "assets/models/foo.obj" },  // optional block
//       "rigidBody": {                             // optional block (Phase
//         "gravity": true,                         // 8e) -- presence adds
//         "velocity": [x, y, z]                    // a RigidBody component.
//       },                                         // Both fields optional,
//                                                   // defaulting to true /
//                                                   // [0,0,0] (RigidBody's
//                                                   // own defaults) --
//                                                   // "rigidBody": {} is a
//                                                   // valid, at-rest body.
//       "collider": { "halfExtent": 0.25 }          // optional block (Phase
//                                                   // 8e) -- presence adds
//                                                   // a Collider component;
//                                                   // "halfExtent" itself
//                                                   // optional, defaulting
//                                                   // to Collider's own
//                                                   // 0.25 default.
//     }
//   ]
// }
//
// Each entity is "an array of named component blocks" ("transform",
// "model", "rigidBody", "collider", ...) rather than a schema hardcoded to
// one fixed component shape -- so a later phase's new component type is a
// new named block + a new parse/serialize branch here, not a schema
// rewrite (Phase 8e's own "rigidBody"/"collider" blocks are exactly that:
// see physics.hpp for RigidBody/Collider). "model"/"rigidBody"/"collider"
// are all independently optional blocks (an entity can have any subset of
// Transform/Model/RigidBody/Collider, matching ecs.hpp's "components are
// opt-in per entity" design) for the same reason -- a RigidBody with no
// Collider simply never collides with the ground (see physics.hpp's
// stepPhysics()), and a Collider with no RigidBody never moves at all
// (nothing but stepPhysics() ever reads Collider, and it only visits
// entities with a RigidBody).
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

    // Phase 8e: mirrors the optional "rigidBody"/"collider" blocks above --
    // hasRigidBody/hasCollider are false when the entity's JSON had no such
    // block at all (matching modelPath's own empty-string "no model block"
    // convention, just as a bool since a RigidBody/Collider block has no
    // single "is this empty" string field to overload); the rest of each
    // pair's fields default to RigidBody/Collider's own defaults (see
    // physics.hpp) and are only meaningful when their has* flag is true.
    bool hasRigidBody = false;
    bool rigidBodyGravity = true;
    glm::vec3 rigidBodyVelocity{0.0f, 0.0f, 0.0f};
    bool hasCollider = false;
    float colliderHalfExtent = 0.25f;
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
// Phase 8e: also adds a RigidBody component (only when hasRigidBody is
// true, with velocity/gravity from rigidBodyVelocity/rigidBodyGravity) and
// a Collider component (only when hasCollider is true, with halfExtent from
// colliderHalfExtent) -- both independently, matching every other
// component here being opt-in per entity.
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
// rather than a mandatory EntityId field. Phase 8e: also writes out a
// "rigidBody"/"collider" block for any entity with a RigidBody/Collider
// component (independently -- see this header's own "Schema" comment),
// same opt-in-per-entity treatment as ModelComponent already gets.
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
