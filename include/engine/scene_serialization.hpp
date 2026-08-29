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
// Phase 15e finally gives saveScene() a real caller (see application.cpp's
// own Phase 15e comment -- Ctrl+S / File > Save Scene / ENGINE_DEBUG_SAVE_SCENE
// all call it now, where before this phase nothing did), and extends this
// same schema with three more optional, independently-opt-in blocks the
// exact same way Phase 8e did: "pointLight", "directionalLight", and
// "camera" (see light.hpp/camera_component.hpp for the PointLight/
// DirectionalLight/CameraComponent components themselves), so a scene saved
// after Phase 15a-15c's Point Light / Directional Light / Camera entities
// were created actually keeps them on the next load, instead of silently
// dropping their own component data and reloading them as bare, inert
// Transform+NameComponent entities. Same two-file split as everything
// above: this header/scene_serialization.cpp still don't depend on
// light.hpp/camera_component.hpp at all (their own field DEFAULTS are
// simply duplicated as SceneEntityRecord's own literal defaults below, the
// identical "no #include, just matching literals" choice hasRigidBody's own
// fields already made for physics.hpp's RigidBody) -- only scene_loader.cpp
// turns them into/out of real components.
//
// Phase 15f adds one more independently-opt-in block, "materialOverride"
// (see material_override.hpp for the MaterialOverride component itself),
// following the exact same pattern once more -- but for a DIFFERENT reason
// than pointLight/directionalLight/camera were added: those three were
// deferred from serialization for a phase or two after each component
// existed, because nothing yet WROTE one through the editor at the time
// (see light.hpp's own "no scene serialization for a component nothing
// writes yet" Phase 15a/15b precedent). MaterialOverride is created
// directly by a live Inspector action (the Material section's "Browse..."
// button) the SAME phase it's introduced, so leaving it out of this schema
// would make Save Scene silently discard a user's own just-made edit -- a
// real, immediately-user-visible regression, not a "nothing exercises this
// yet" gap. So it round-trips from the start, no deferral.
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
//       "collider": { "halfExtent": 0.25 },         // optional block (Phase
//                                                   // 8e) -- presence adds
//                                                   // a Collider component;
//                                                   // "halfExtent" itself
//                                                   // optional, defaulting
//                                                   // to Collider's own
//                                                   // 0.25 default.
//       "pointLight": {                             // optional block (Phase
//         "color": [r, g, b],                       // 15e) -- presence adds
//         "constant": c,                             // a PointLight
//         "linear": l,                               // component
//         "quadratic": q                             // (light.hpp). All
//       },                                          // four fields optional,
//                                                   // defaulting to
//                                                   // PointLight's own
//                                                   // struct defaults --
//                                                   // "pointLight": {} is a
//                                                   // valid, plain-white
//                                                   // light.
//       "directionalLight": {                       // optional block (Phase
//         "direction": [x, y, z],                   // 15e) -- presence adds
//         "color": [r, g, b],                       // a DirectionalLight
//         "active": true                             // component
//       },                                          // (light.hpp). All
//                                                   // three fields
//                                                   // optional, defaulting
//                                                   // to DirectionalLight's
//                                                   // own struct defaults /
//                                                   // false. "active" marks
//                                                   // whether THIS entity
//                                                   // should become
//                                                   // Application's
//                                                   // activeDirectionalLight_
//                                                   // again on load -- see
//                                                   // "Active directional
//                                                   // light" below.
//       "camera": {                                 // optional block (Phase
//         "fovYDeg": f,                              // 15e) -- presence adds
//         "nearPlane": n,                            // a CameraComponent
//         "farPlane": fp                             // (camera_component.
//       },                                          // hpp). All three
//                                                   // fields optional,
//                                                   // defaulting to
//                                                   // CameraComponent's own
//                                                   // struct defaults.
//       "materialOverride": {                       // optional block (Phase
//         "diffuseTexture":                          // 15f) -- presence adds
//           "assets/textures/foo.png"                 // a MaterialOverride
//       },                                          // component
//                                                   // (material_override.
//                                                   // hpp). Unlike every
//                                                   // other block above,
//                                                   // "diffuseTexture" is
//                                                   // REQUIRED, not
//                                                   // optional, when this
//                                                   // block is present --
//                                                   // there is no
//                                                   // meaningful "empty
//                                                   // override" default the
//                                                   // way "rigidBody": {} or
//                                                   // "pointLight": {} have
//                                                   // (see "Schema" note
//                                                   // below).
//       "parent": "some_other_entity_name"          // optional (Phase 14b)
//                                                   // -- presence adds a
//                                                   // Parent component
//                                                   // referencing another
//                                                   // entity BY NAME (see
//                                                   // "Parent references"
//                                                   // below), not by a raw
//                                                   // EntityId index.
//     }
//   ]
// }
//
// Each entity is "an array of named component blocks" ("transform",
// "model", "rigidBody", "collider", ...) rather than a schema hardcoded to
// one fixed component shape -- so a later phase's new component type is a
// new named block + a new parse/serialize branch here, not a schema
// rewrite (Phase 8e's own "rigidBody"/"collider" blocks are exactly that:
// see physics.hpp for RigidBody/Collider; Phase 15e's own "pointLight"/
// "directionalLight"/"camera" blocks below, and Phase 15f's own
// "materialOverride" block further below, are the identical pattern applied
// again). "model"/"rigidBody"/"collider"/"pointLight"/"directionalLight"/
// "camera"/"materialOverride" are all independently optional blocks (an
// entity can have any subset of Transform/Model/RigidBody/Collider/
// PointLight/DirectionalLight/CameraComponent/MaterialOverride, matching
// ecs.hpp's "components are opt-in per entity" design) for the same reason
// -- a RigidBody with no Collider simply never collides with the ground
// (see physics.hpp's stepPhysics()), a Collider with no RigidBody never
// moves at all (nothing but stepPhysics() ever reads Collider, and it only
// visits entities with a RigidBody), a MaterialOverride with no Model is
// simply never read by anything (Application::render()'s own ModelComponent
// draw loop is the only consumer -- see material_override.hpp), and there
// is no rule at all stopping an entity from combining, say, a Model AND a
// PointLight (a glowing lamp mesh) even though nothing in this engine's
// Create menu happens to build one that way today.
//
// "materialOverride"'s own "diffuseTexture" field is the one exception to
// every OTHER block's "every field inside is itself optional, defaulting to
// the component's own struct default" rule (see e.g. "rigidBody": {} being
// a valid, at-rest body above): MaterialOverride has no meaningful
// "default, unset" texture the way PointLight has a meaningful default
// color/attenuation -- a materialOverride block that named no texture at
// all would just be a no-op component nothing could usefully resolve (see
// material_override.hpp's own MaterialOverride comment on why
// diffuseTexture being null already means "no override"), so
// parseSceneRecords() requires "diffuseTexture" to be present and a string
// whenever "materialOverride" itself is present -- an absent block still
// means "no override at all," but a present, empty block is a schema error
// here, unlike every has*-gated block above it.
//
// Rotation is stored as a quaternion, [w, x, y, z] -- the same order
// glm::quat's own constructor and Transform::rotation() use (see
// transform.hpp) -- rather than Euler angles, for the identical
// gimbal-lock reason transform.hpp itself already documents; storing
// Euler angles here would silently reintroduce that problem one file away
// from where it was originally avoided.
//
// --- Parent references: by name, not by raw EntityId (Phase 14b) --------
// An entity's own EntityId is a fresh, monotonically-increasing index
// EntityRegistry::create() hands out at LOAD time (see ecs.hpp) -- nothing
// about it survives a save/load round-trip meaningfully, the same reason
// NameComponent exists at all (see ecs.hpp's own NameComponent comment).
// So "parent" is authored as another entity's "name" string within the
// same file, not a numeric id. Two consequences that shape where the two
// halves of this feature live:
//
//   1. A child can be listed BEFORE its parent in the "entities" array
//      (assets/scenes/default.json's own "parented_demo_cube" demonstrates
//      exactly this, listed before "falling_cube") -- parseSceneRecords()
//      below only knows every entity's name once it has finished parsing
//      the whole file, so it validates "parent" references (every non-empty
//      SceneEntityRecord::parentName must match some OTHER record's "name"
//      in the same file) in a second pass AFTER the main per-entity parsing
//      loop, not while still parsing the entity that names it. A "parent"
//      naming an entity that doesn't exist anywhere in the file throws
//      std::runtime_error (after LOG_ERROR) at this parseSceneRecords()
//      layer, matching this header's own "validate at the boundary,
//      specific message" convention for every other malformed-input case
//      -- see scene_serialization.cpp.
//   2. Actually turning a validated parentName back into a live EntityId
//      (and adding the resulting Parent component -- see
//      transform_hierarchy.hpp) is loadScene()'s job, not
//      parseSceneRecords()'s: it needs a name -> EntityId map built from
//      the entities THIS load actually created, which only exists once
//      EntityRegistry::create() has run for every record (see
//      scene_loader.cpp). This is exactly this header's own pre-existing
//      "pure data (scene_serialization.cpp) vs. EntityRegistry-facing
//      (scene_loader.cpp)" split already documented below -- parentName
//      resolution is pure data validation (does the name exist at all?),
//      Parent-component construction is EntityRegistry-facing (what
//      EntityId does that name resolve to?), so each half lives in the
//      file already responsible for that kind of work.
//
// A cycle of "parent" names (A parents B, B parents A) passes this file's
// own existence-only validation (both names genuinely exist in the file)
// -- it's resolveWorldMatrix() (transform_hierarchy.hpp), not this schema
// layer, that guards against a cycle actually being walked; see that
// header's own comment for why cycle-safety belongs there instead of here.
//
// --- Active directional light: an in-band marker, not a separate field (Phase 15e) --
// application.hpp's activeDirectionalLight_ names which ONE DirectionalLight
// entity (if any) actually drives this engine's single uLightDirection/
// uLightColor uniform pair and shadow frustum this frame (see light.hpp's
// own resolveActiveDirectionalLight() comment) -- a save that dropped this
// distinction would reload every DirectionalLight entity as equally inert,
// silently reverting the whole scene to kLightDirection/kLightColor's fixed
// fallback even though a real sun was active before saving. Rather than a
// new top-level "activeDirectionalLight": "some_entity_name" field (which
// would need its own second-pass name resolution exactly like "parent"
// does, for a value that only ever makes sense attached to an entity that
// already HAS a "directionalLight" block), this schema puts the marker
// in-band: "directionalLight"'s own "active" boolean field (see the
// "Schema" comment above). That keeps the active/inactive distinction
// exactly where the light it describes already is, with no risk of a
// dangling reference to an entity that has no DirectionalLight at all --
// an entity whose "directionalLight" block never mentions "active" simply
// defaults to false (an ordinary, inactive light), matching this whole
// schema's "absent means the unremarkable default" convention (readBool's
// own fallback-if-absent contract, scene_serialization.cpp).
//
// Symmetric with "parent"'s own by-name approach (this same reasoning as
// above: an EntityId doesn't survive a save/load round-trip meaningfully),
// but resolved the OPPOSITE direction around the pure-data/EntityRegistry-
// facing split: parseSceneRecords() only validates that a "parent" name
// exists somewhere in the file, leaving the name -> EntityId resolution to
// loadScene() (EntityRegistry-facing); "active" carries no name to resolve
// at all -- it's parsed as a plain per-record bool by parseSceneRecords()
// (pure data, no EntityRegistry needed), and it's loadScene() that turns
// "which record(s) said active: true" into "which freshly-created EntityId
// becomes activeDirectionalLight_" (see scene_loader.cpp's own comment) --
// once again pure validation vs. EntityId-producing work landing in the
// file already responsible for each kind, this time with no second file
// needed to know the ANSWER, only the free EntityId that answer is now
// expressed in. Symmetric in the other direction, too: saveScene() takes
// the CURRENTLY active EntityId as a parameter (see its own comment below)
// and marks that one entity's own record "active": true while writing --
// there is no separate lookup step the way loadScene()'s Parent-component
// pass needs, since saveScene() already visits every entity's own
// component data directly.
//
// More than one record setting "active": true is accepted, not rejected --
// unlike an unresolvable "parent" name, this isn't a structural schema
// violation (every referenced concept -- namely, "this record itself" --
// unambiguously exists), just an authored file with more than one candidate.
// loadScene() resolves it exactly the way idByName's own "if more than one
// record shares a name, the last one created wins" comment already handles
// duplicate names elsewhere in this same function: the LAST matching record
// in file order wins, silently -- a human hand-editing this JSON (or a
// theoretical future multi-scene merge) gets a deterministic, if perhaps
// surprising, single winner rather than a hard failure over something that
// was never actually ambiguous at the EntityId level.
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
// Forward-declared only for loadScene()'s/saveScene()'s own signatures below
// (an EntityId* out-parameter and an EntityId in-parameter, respectively,
// for the "Active directional light" round-trip -- see this header's own
// comment above) -- the same "just the declaration, this header stays
// ecs.hpp-free" shape light.hpp's own EntityId forward-declaration already
// establishes for the identical reason (see that header's own comment): a
// pointer parameter, and a by-value parameter that's only ever DEFINED or
// CALLED from a translation unit that already has ecs.hpp fully included
// (scene_loader.cpp/application.cpp), need no complete EntityId type here.
class EntityId;

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

    // Phase 15e: mirrors the optional "pointLight" block above -- hasPointLight
    // is false when the entity's JSON had no such block at all, matching
    // hasRigidBody/hasCollider's own convention just above. The four field
    // defaults below are duplicated verbatim from PointLight{}'s own struct
    // defaults (light.hpp) -- see this header's own top comment for why this
    // file deliberately doesn't #include light.hpp itself to get them.
    bool hasPointLight = false;
    glm::vec3 pointLightColor{1.0f, 1.0f, 1.0f};
    float pointLightConstant = 1.0f;
    float pointLightLinear = 0.7f;
    float pointLightQuadratic = 1.8f;

    // Phase 15e: mirrors the optional "directionalLight" block above -- same
    // has*/duplicated-defaults shape as hasPointLight just above, defaults
    // copied verbatim from DirectionalLight{}'s own struct defaults
    // (light.hpp). directionalLightActive mirrors "active" -- see this
    // header's own "Active directional light" comment for what it means and
    // how loadScene()/saveScene() actually use it; false (matching
    // DirectionalLight's OWN struct having no such concept at all -- it's a
    // scene-file-only marker, not a component field) is the correct default
    // for "no active marker present," the same "absent means the
    // unremarkable default" convention every other optional field here
    // already follows.
    bool hasDirectionalLight = false;
    glm::vec3 directionalLightDirection{0.3f, -0.9f, 0.15f};
    glm::vec3 directionalLightColor{0.55f, 0.7f, 1.0f};
    bool directionalLightActive = false;

    // Phase 15e: mirrors the optional "camera" block above -- same has*
    // shape once more, defaults copied verbatim from CameraComponent{}'s own
    // struct defaults (camera_component.hpp).
    bool hasCameraComponent = false;
    float cameraFovYDeg = 60.0f;
    float cameraNearPlane = 0.1f;
    float cameraFarPlane = 100.0f;

    // Phase 15f: mirrors the optional "materialOverride" block above --
    // hasMaterialOverride is false when the entity's JSON had no such block
    // at all, same has*-flag convention as hasRigidBody/hasPointLight/etc.
    // Unlike those, there is no matching "default value" for
    // materialOverrideDiffuseTexturePath to fall back to when the block IS
    // present -- see this header's own "Schema" comment for why
    // "diffuseTexture" is a REQUIRED field of a present "materialOverride"
    // block, not an optional one defaulting to some baseline texture.
    // Stores the same relative, reloadable string form ModelComponent::path/
    // MaterialOverride::diffuseTexturePath already use (e.g.
    // "assets/textures/foo.png"), matching modelPath's own convention just
    // above -- resolving it to an absolute path and an actual GPU-resident
    // Texture is scene_loader.cpp's job (ResourceManager-facing), same split
    // as modelPath itself.
    bool hasMaterialOverride = false;
    std::string materialOverrideDiffuseTexturePath;

    // Phase 14b: mirrors the optional "parent" field above -- empty when
    // the entity's JSON had no "parent" field at all (matching modelPath's
    // own empty-string "no such block" convention), otherwise the OTHER
    // entity's "name" this one is parented to. See this header's own
    // "Parent references" comment above for why this is a name, resolved
    // to an EntityId only later (by loadScene(), not here).
    std::string parentName;
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
// own exceptions unchanged (missing file / invalid JSON / bad schema,
// including an unresolvable "parent" name -- see this header's own
// "Parent references" comment).
//
// Phase 14b: after every record's entity has been created (a first pass,
// exactly as before), a second pass adds a Parent component (see
// transform_hierarchy.hpp) for every record whose parentName is non-empty,
// resolved through a name -> EntityId map built from THIS load's own
// entities. parseSceneRecords() already guarantees every non-empty
// parentName matches some record's name somewhere in the same file, so
// this map lookup cannot fail for a file that parsed successfully -- see
// this header's own "Parent references" comment for why that validation
// lives at the parse layer while the actual EntityId resolution has to
// happen here instead.
//
// Phase 15e: also adds a PointLight component (only when hasPointLight is
// true, with color/attenuation from the matching pointLight* fields), a
// DirectionalLight component (only when hasDirectionalLight is true, with
// direction/color from the matching directionalLight* fields), and a
// CameraComponent (only when hasCameraComponent is true, with fov/near/far
// from the matching camera* fields) -- all three independently, same
// opt-in-per-entity treatment RigidBody/Collider already get.
//
// Phase 15f: also adds a MaterialOverride component (only when
// hasMaterialOverride is true) whose diffuseTexture is loaded via
// resources.getTexture() from materialOverrideDiffuseTexturePath, resolved
// against the executable's own directory first -- the identical
// resolveAssetPath()-then-load treatment modelPath itself already gets just
// below, for the identical "stay portable across machines/checkouts"
// reason. A texture path that fails to resolve/load is surfaced the same
// way a bad modelPath is: re-thrown with the offending entity's name/path
// attached, since a multi-entity scene file needs to say *which* entity's
// asset reference was bad.
// `activeDirectionalLightOut`, when non-null, is unconditionally reset to a
// default-constructed (invalid) EntityId at the top of this call, then set
// to the freshly-created EntityId of whichever record had
// directionalLightActive == true (the LAST such record in file order wins,
// if more than one sets it -- see this header's own "Active directional
// light" comment for why that's an accepted, deterministic outcome rather
// than a schema error) -- left at its default invalid value if no record
// set it at all. Pass nullptr (the default) when the caller has no use for
// this, e.g. a hypothetical future caller loading a scene file purely for
// inspection.
void loadScene(EntityRegistry& registry, const std::string& path, ResourceManager& resources, Shader& shader,
               EntityId* activeDirectionalLightOut = nullptr);

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
// Phase 14b: also writes a "parent" field for any entity with a Parent
// component, resolved back to that OTHER entity's own name (its
// NameComponent if it has one, or the same generated "entity_<index>"
// placeholder this function already falls back to for a name-less entity)
// -- see this header's own "Parent references" comment for why "parent" is
// authored/round-tripped by name, never by raw EntityId.
//
// Phase 15e: also writes a "pointLight"/"directionalLight"/"camera" block
// for any entity with a PointLight/DirectionalLight/CameraComponent (each
// independently -- see this header's own "Schema" comment), same
// opt-in-per-entity treatment ModelComponent/RigidBody/Collider already get.
// `activeDirectionalLight` names the entity (if any) this Application
// currently considers active (its own activeDirectionalLight_, see
// application.hpp) -- passed a default-constructed (invalid) EntityId when
// there is none. Whichever entity this equals gets its own
// "directionalLight" block's "active" field written true; every OTHER
// entity with a DirectionalLight gets "active": false -- see this header's
// own "Active directional light" comment for why this is an EntityId
// parameter here (saveScene() already visits every entity directly, so
// there's no name-lookup pass needed the way loadScene()'s Parent-component
// pass has) rather than, say, a second EntityId-to-name lookup step.
//
// Phase 15f: also writes a "materialOverride" block for any entity with a
// MaterialOverride component whose diffuseTexture is non-null (matching
// resolveDiffuseTextureOverride()'s own material_override.hpp "null means
// no override" contract -- an entity that once had an override cleared, if
// a future phase adds a way to do that by nulling the field rather than
// removeComponent<MaterialOverride>()'ing it outright, is correctly NOT
// written here either), same opt-in-per-entity treatment as every other
// block. The block's own "diffuseTexture" field is
// MaterialOverride::diffuseTexturePath verbatim -- already the portable,
// relative form this schema needs (see MaterialOverride's own
// material_override.hpp comment for why that string, not Texture::path(),
// is what's kept for exactly this purpose).
//
// Takes `registry` by non-const reference, not const&, even though this
// function only reads it: EntityRegistry::each<T>()/pool<T>() (see
// ecs.hpp) have no const overload -- each<T>() lazily creates T's pool on
// first use the same way addComponent<T>() does, which is why pool
// look-up isn't const there either -- and adding one purely for this one
// read-only caller would be speculative API surface on a Phase 8a file
// this phase doesn't otherwise need to change.
void saveScene(EntityRegistry& registry, const std::string& path, EntityId activeDirectionalLight);

// Phase 18h: the per-entity halves of loadScene()/saveScene() above,
// factored out so Application's own undo/redo entity-deletion/-creation
// commands (undo_stack.hpp, application.cpp) can snapshot/restore ONE
// entity the identical, already-exercised way saveScene()/loadScene()
// snapshot/restore EVERY entity, instead of a second, parallel
// per-component copy mechanism -- see undo_stack.hpp's own header comment
// for the full "why reuse, not duplicate" reasoning.
//
// captureEntityRecord() is exactly saveScene()'s own per-entity record-
// building logic (Transform, ModelComponent, RigidBody, Collider,
// PointLight, DirectionalLight, CameraComponent, MaterialOverride, and the
// Parent-to-name lookup), now shared by both saveScene()'s own
// registry.each<Transform>() loop and Application::deleteEntity()/
// spawnEntityFromCreateMenu() (application.cpp). `activeDirectionalLight`
// mirrors saveScene()'s own same-named parameter exactly -- pass the
// caller's current "which entity is active" EntityId (or a
// default-constructed invalid one if none) so a captured DirectionalLight
// record's own "active" field comes out correct.
SceneEntityRecord captureEntityRecord(EntityRegistry& registry, EntityId id, EntityId activeDirectionalLight);

// restoreEntityFromRecord() is exactly loadScene()'s own per-record entity-
// building logic (the first-pass loop body: NameComponent, Transform,
// RigidBody, Collider, PointLight, DirectionalLight, CameraComponent,
// MaterialOverride), now shared by both loadScene()'s own per-record loop
// and Application's own undo/redo recreation path. Returns the freshly
// created EntityId.
//
// Deliberately does NOT add a Parent component even when record.parentName
// is non-empty, and does NOT report back whether this record's
// DirectionalLight should become "the" active one -- both of those need a
// caller-specific name/id resolution loadScene() and Application's own
// undo/redo path each do differently (loadScene()'s own idByName, built
// across a WHOLE file's worth of records in file order, so a child can
// legally be restored before its parent -- see this header's own "Parent
// references" comment; Application's own findEntityByName() against
// whatever the CURRENT live registry already contains, since it is only
// ever restoring ONE already-independently-existing entity, never a whole
// file's interdependent set). Both callers add a Parent component
// themselves, immediately after this call returns, once THEY have resolved
// record.parentName the way each needs to -- the same two-pass shape
// loadScene() below already uses for its own bulk case.
//
// Propagates the identical model/materialOverride-load exceptions
// loadScene()'s own per-record loop already could throw (see that
// function's own comment) -- unchanged behavior, just now shared code.
EntityId restoreEntityFromRecord(EntityRegistry& registry, const SceneEntityRecord& record, ResourceManager& resources,
                                  Shader& shader);

}  // namespace engine

#endif  // ENGINE_SCENE_SERIALIZATION_HPP
