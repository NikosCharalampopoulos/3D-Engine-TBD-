// Phase 8b: the EntityRegistry/GL-adjacent half of scene (de)serialization
// -- loadScene()/saveScene() turn SceneEntityRecords (see
// scene_serialization.cpp) into/out of a live EntityRegistry, which is the
// one place this feature touches ResourceManager/Model (a real GPU-resident
// asset) rather than plain data. Kept in its own translation unit,
// separate from scene_serialization.cpp's pure JSON<->record logic, so
// tests/scene_serialization_test.cpp can link against the pure half alone
// -- see scene_serialization.hpp's own header comment for the full
// rationale.
//
// Phase 15e: saveScene() finally gets a real caller (application.cpp's
// saveCurrentScene(), wired to Ctrl+S / File > Save Scene /
// ENGINE_DEBUG_SAVE_SCENE), and both functions here gain PointLight/
// DirectionalLight/CameraComponent support -- see scene_serialization.hpp's
// own Phase 15e comment for the schema/design, and this file's own new
// blocks below for the mechanics.

#include "engine/scene_serialization.hpp"

#include <filesystem>
#include <unordered_map>
#include <utility>

#include "engine/camera_component.hpp"
#include "engine/ecs.hpp"
#include "engine/light.hpp"
#include "engine/log.hpp"
#include "engine/material_override.hpp"
#include "engine/model.hpp"
#include "engine/paths.hpp"
#include "engine/physics.hpp"
#include "engine/resource_manager.hpp"
#include "engine/transform.hpp"
#include "engine/transform_hierarchy.hpp"

namespace engine {

EntityId restoreEntityFromRecord(EntityRegistry& registry, const SceneEntityRecord& record, ResourceManager& resources,
                                  Shader& shader) {
    const EntityId id = registry.create();
    registry.addComponent<NameComponent>(id, NameComponent{record.name});

    Transform& transform = registry.addComponent<Transform>(id);
    transform.setPosition(record.position);
    transform.setRotation(record.rotation);
    transform.setScale(record.scale);

    if (record.hasRigidBody) {
        registry.addComponent<RigidBody>(id, RigidBody{record.rigidBodyVelocity, record.rigidBodyGravity});
    }
    if (record.hasCollider) {
        registry.addComponent<Collider>(id, Collider{record.colliderHalfExtent});
    }

    if (record.hasPointLight) {
        registry.addComponent<PointLight>(
            id, PointLight{record.pointLightColor, record.pointLightConstant, record.pointLightLinear,
                            record.pointLightQuadratic});
    }
    if (record.hasDirectionalLight) {
        registry.addComponent<DirectionalLight>(
            id, DirectionalLight{record.directionalLightDirection, record.directionalLightColor});
    }
    if (record.hasCameraComponent) {
        registry.addComponent<CameraComponent>(
            id, CameraComponent{record.cameraFovYDeg, record.cameraNearPlane, record.cameraFarPlane});
    }

    if (record.hasMaterialOverride) {
        const std::string resolvedTexturePath = resolveAssetPath(record.materialOverrideDiffuseTexturePath);
        try {
            MaterialOverride& materialOverride = registry.addComponent<MaterialOverride>(id, MaterialOverride{});
            materialOverride.diffuseTexture = resources.getTexture(resolvedTexturePath);
            materialOverride.diffuseTexturePath = record.materialOverrideDiffuseTexturePath;
        } catch (const std::exception& e) {
            LOG_ERROR("restoreEntityFromRecord: entity \"" + record.name +
                       "\" failed to load materialOverride texture \"" + record.materialOverrideDiffuseTexturePath +
                       "\": " + e.what());
            throw std::runtime_error("restoreEntityFromRecord: entity \"" + record.name +
                                      "\" failed to load materialOverride texture \"" +
                                      record.materialOverrideDiffuseTexturePath + "\": " + e.what());
        }
    }

    if (record.modelPath.empty()) {
        return id;
    }

    // modelPath is stored/authored relative -- see loadScene()'s own comment
    // below for the full reasoning, identical here.
    const std::string resolvedPath = resolveAssetPath(record.modelPath);
    if (!std::filesystem::exists(resolvedPath)) {
        LOG_ERROR("restoreEntityFromRecord: entity \"" + record.name +
                   "\" references a model asset that doesn't exist: \"" + record.modelPath + "\" (resolved to \"" +
                   resolvedPath + "\")");
        throw std::runtime_error("restoreEntityFromRecord: entity \"" + record.name +
                                  "\" references a missing model asset \"" + record.modelPath + "\"");
    }

    try {
        std::shared_ptr<Model> model = resources.getModel(resolvedPath, shader);
        registry.addComponent<ModelComponent>(id, ModelComponent{std::move(model), record.modelPath});
    } catch (const std::exception& e) {
        LOG_ERROR("restoreEntityFromRecord: entity \"" + record.name + "\" failed to load model \"" +
                   record.modelPath + "\": " + e.what());
        throw std::runtime_error("restoreEntityFromRecord: entity \"" + record.name + "\" failed to load model \"" +
                                  record.modelPath + "\": " + e.what());
    }

    return id;
}

SceneEntityRecord captureEntityRecord(EntityRegistry& registry, EntityId id, EntityId activeDirectionalLight) {
    SceneEntityRecord record;

    const NameComponent* nameComponent = registry.getComponent<NameComponent>(id);
    record.name = nameComponent != nullptr ? nameComponent->name : ("entity_" + std::to_string(id.index()));

    const Transform* transform = registry.getComponent<Transform>(id);
    if (transform != nullptr) {
        record.position = transform->position();
        record.rotation = transform->rotation();
        record.scale = transform->scale();
    }

    const ModelComponent* modelComponent = registry.getComponent<ModelComponent>(id);
    if (modelComponent != nullptr) {
        record.modelPath = modelComponent->path;
    }

    const RigidBody* rigidBody = registry.getComponent<RigidBody>(id);
    if (rigidBody != nullptr) {
        record.hasRigidBody = true;
        record.rigidBodyGravity = rigidBody->useGravity;
        record.rigidBodyVelocity = rigidBody->velocity;
    }
    const Collider* collider = registry.getComponent<Collider>(id);
    if (collider != nullptr) {
        record.hasCollider = true;
        record.colliderHalfExtent = collider->halfExtent;
    }

    const PointLight* pointLight = registry.getComponent<PointLight>(id);
    if (pointLight != nullptr) {
        record.hasPointLight = true;
        record.pointLightColor = pointLight->color;
        record.pointLightConstant = pointLight->constant;
        record.pointLightLinear = pointLight->linear;
        record.pointLightQuadratic = pointLight->quadratic;
    }
    const DirectionalLight* directionalLight = registry.getComponent<DirectionalLight>(id);
    if (directionalLight != nullptr) {
        record.hasDirectionalLight = true;
        record.directionalLightDirection = directionalLight->direction;
        record.directionalLightColor = directionalLight->color;
        record.directionalLightActive = (id == activeDirectionalLight);
    }
    const CameraComponent* cameraComponent = registry.getComponent<CameraComponent>(id);
    if (cameraComponent != nullptr) {
        record.hasCameraComponent = true;
        record.cameraFovYDeg = cameraComponent->fovYDeg;
        record.cameraNearPlane = cameraComponent->nearPlane;
        record.cameraFarPlane = cameraComponent->farPlane;
    }

    const MaterialOverride* materialOverride = registry.getComponent<MaterialOverride>(id);
    if (materialOverride != nullptr && materialOverride->diffuseTexture != nullptr) {
        record.hasMaterialOverride = true;
        record.materialOverrideDiffuseTexturePath = materialOverride->diffuseTexturePath;
    }

    const Parent* parent = registry.getComponent<Parent>(id);
    if (parent != nullptr && parent->id.valid() && registry.getComponent<Transform>(parent->id) != nullptr) {
        const NameComponent* parentName = registry.getComponent<NameComponent>(parent->id);
        record.parentName = parentName != nullptr ? parentName->name : ("entity_" + std::to_string(parent->id.index()));
    }

    return record;
}

void loadScene(EntityRegistry& registry, const std::string& path, ResourceManager& resources, Shader& shader,
                EntityId* activeDirectionalLightOut) {
    // Phase 15e: reset up front, unconditionally, before parseSceneRecords()
    // even runs -- see this function's own scene_serialization.hpp comment
    // for the full contract. Doing this before the (possibly-throwing)
    // parse below means a caller whose loadScene() call ends up throwing
    // still observes a well-defined "no active light" value here rather
    // than whatever this pointee happened to hold before the call, exactly
    // the same "leave output state well-defined even on the failure path"
    // discipline this codebase already applies elsewhere (e.g.
    // ENGINE_DEBUG_SELECT's own "unmatched name leaves selectedEntity_
    // unset" handling, application.cpp).
    if (activeDirectionalLightOut != nullptr) {
        *activeDirectionalLightOut = EntityId();
    }

    // Propagates parseSceneRecords()'s own exceptions (missing file /
    // invalid JSON / bad schema, including an unresolvable "parent" name)
    // unchanged -- this function has nothing to add to those cases, they're
    // already fully diagnosed at that layer.
    const std::vector<SceneEntityRecord> records = parseSceneRecords(path);

    // Phase 14b: every record's own name -> the EntityId THIS load just
    // created for it, populated in the same loop that creates each entity
    // below, so the second pass further down (which adds Parent components)
    // can resolve a "parent" name to an EntityId regardless of whether that
    // name appears earlier or later in `records` -- see
    // scene_serialization.hpp's own "Parent references" comment for why a
    // child is allowed to list a parent that hasn't been created yet at the
    // point the child itself is created. If more than one record shares a
    // name, the last one created wins this map -- the same ambiguity
    // "name" already has everywhere else in this schema (it's a
    // human-readable label, not a uniqueness-enforced key; see
    // ecs.hpp's own NameComponent comment).
    std::unordered_map<std::string, EntityId> idByName;
    idByName.reserve(records.size());

    // Phase 18h: the actual per-record entity-building work now lives in
    // restoreEntityFromRecord() (scene_serialization.hpp), shared with
    // Application's own undo/redo recreation path -- see that function's
    // own header comment. Everything below this call is what's left that's
    // specific to a WHOLE-FILE load: idByName bookkeeping (for the
    // second-pass Parent resolution further down) and the
    // "last-record-wins" active-directional-light tracking, neither of
    // which restoreEntityFromRecord() itself can do for a single record in
    // isolation.
    for (const SceneEntityRecord& record : records) {
        const EntityId id = restoreEntityFromRecord(registry, record, resources, shader);
        idByName[record.name] = id;

        // Phase 15e: see scene_serialization.hpp's own "Active directional
        // light" comment for why the LAST record (in file order) with
        // directionalLightActive == true wins here, silently overwriting
        // whatever this pointee held from an earlier record in this same
        // loop -- the identical "last one wins" tolerance idByName just
        // above already has for a duplicate "name".
        if (record.hasDirectionalLight && record.directionalLightActive && activeDirectionalLightOut != nullptr) {
            *activeDirectionalLightOut = id;
        }
    }

    // Phase 14b: second pass, now that every record's entity has been
    // created (idByName above is complete) -- adds a Parent component for
    // every record whose parentName is non-empty, looked up through
    // idByName rather than done inline in the loop above, so a child listed
    // before its parent in `records` still resolves correctly (see
    // scene_serialization.hpp's own "Parent references" comment). This
    // lookup cannot fail for a file that made it through parseSceneRecords()
    // successfully -- that function already rejects any parentName that
    // doesn't match some record's name (see scene_serialization.cpp) -- so
    // there's no missing-name error path to handle here; the only thing
    // this loop can produce that resolveWorldMatrix() then has to guard
    // against is a cycle (A parents B, B parents A both validly exist) or,
    // once a later phase adds entity destruction, a dangling reference --
    // both handled at read time by resolveWorldMatrix() itself (see
    // transform_hierarchy.hpp), not here.
    for (const SceneEntityRecord& record : records) {
        if (record.parentName.empty()) {
            continue;
        }
        const EntityId childId = idByName.at(record.name);
        const EntityId parentId = idByName.at(record.parentName);
        registry.addComponent<Parent>(childId, Parent{parentId});
    }
}

void saveScene(EntityRegistry& registry, const std::string& path, EntityId activeDirectionalLight) {
    std::vector<SceneEntityRecord> records;

    // Driven by Transform's pool, not ModelComponent's: every entity this
    // schema can represent has a Transform (see this function's own header
    // comment), but not every entity has a ModelComponent, so iterating
    // Transform's pool and treating ModelComponent as optional per-entity
    // (mirroring how loadScene() treats an absent "model" block) covers
    // every entity exactly once -- the reverse (driving off ModelComponent)
    // would silently skip any future Model-less entity.
    // Phase 18h: the actual per-entity record-building work now lives in
    // captureEntityRecord() (scene_serialization.hpp), shared with
    // Application's own undo/redo entity-deletion/-creation commands -- see
    // that function's own header comment. `transform` (the each<Transform>()
    // callback's own second parameter) is unused here beyond driving which
    // entities this loop visits at all -- captureEntityRecord() re-reads the
    // same Transform component by id internally, the same "this loop exists
    // to enumerate ids, not to hand-carry each entity's own component
    // references" shape restoreEntityFromRecord()'s own caller (loadScene()
    // above) already has.
    registry.each<Transform>([&](EntityId id, Transform& /*transform*/) {
        records.push_back(captureEntityRecord(registry, id, activeDirectionalLight));
    });

    writeSceneRecords(records, path);
}

}  // namespace engine
