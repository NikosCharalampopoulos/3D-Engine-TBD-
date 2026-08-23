// Phase 8b: the EntityRegistry/GL-adjacent half of scene (de)serialization
// -- loadScene()/saveScene() turn SceneEntityRecords (see
// scene_serialization.cpp) into/out of a live EntityRegistry, which is the
// one place this feature touches ResourceManager/Model (a real GPU-resident
// asset) rather than plain data. Kept in its own translation unit,
// separate from scene_serialization.cpp's pure JSON<->record logic, so
// tests/scene_serialization_test.cpp can link against the pure half alone
// -- see scene_serialization.hpp's own header comment for the full
// rationale.

#include "engine/scene_serialization.hpp"

#include <filesystem>
#include <unordered_map>
#include <utility>

#include "engine/ecs.hpp"
#include "engine/log.hpp"
#include "engine/model.hpp"
#include "engine/paths.hpp"
#include "engine/physics.hpp"
#include "engine/resource_manager.hpp"
#include "engine/transform.hpp"
#include "engine/transform_hierarchy.hpp"

namespace engine {

void loadScene(EntityRegistry& registry, const std::string& path, ResourceManager& resources, Shader& shader) {
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

    for (const SceneEntityRecord& record : records) {
        const EntityId id = registry.create();
        registry.addComponent<NameComponent>(id, NameComponent{record.name});
        idByName[record.name] = id;

        Transform& transform = registry.addComponent<Transform>(id);
        transform.setPosition(record.position);
        transform.setRotation(record.rotation);
        transform.setScale(record.scale);

        // Phase 8e: RigidBody/Collider are added independently of each
        // other and of the modelPath check below -- an entity can have
        // either, both, or neither, matching every other component here
        // being opt-in per entity (see scene_serialization.hpp's own
        // "Schema" comment).
        if (record.hasRigidBody) {
            registry.addComponent<RigidBody>(id, RigidBody{record.rigidBodyVelocity, record.rigidBodyGravity});
        }
        if (record.hasCollider) {
            registry.addComponent<Collider>(id, Collider{record.colliderHalfExtent});
        }

        if (record.modelPath.empty()) {
            continue;
        }

        // modelPath is stored/authored relative (e.g.
        // "assets/models/scene.obj", matching ResourceManager::getModel()'s
        // own path convention -- see resource_manager.hpp), so it's
        // resolved against the executable's directory here, the same way
        // every other asset path in this engine is (see paths.hpp) --
        // never baked into the scene file as an already-resolved absolute
        // path, which would make assets/scenes/default.json non-portable
        // across machines/checkouts.
        const std::string resolvedPath = resolveAssetPath(record.modelPath);
        if (!std::filesystem::exists(resolvedPath)) {
            LOG_ERROR("loadScene: entity \"" + record.name + "\" references a model asset that doesn't exist: \"" +
                       record.modelPath + "\" (resolved to \"" + resolvedPath + "\")");
            throw std::runtime_error("loadScene: entity \"" + record.name +
                                      "\" references a missing model asset \"" + record.modelPath + "\"");
        }

        try {
            std::shared_ptr<Model> model = resources.getModel(resolvedPath, shader);
            registry.addComponent<ModelComponent>(id, ModelComponent{std::move(model), record.modelPath});
        } catch (const std::exception& e) {
            // Re-thrown with the offending entity's name/path attached --
            // see scene_serialization.hpp's own loadScene() comment on why
            // a bare Model::load failure isn't enough context for a
            // multi-entity scene file (LOG_ERROR already happened once,
            // inside ResourceManager/Model; this is a second,
            // entity-scoped log line, not a duplicate of the same
            // information).
            LOG_ERROR("loadScene: entity \"" + record.name + "\" failed to load model \"" + record.modelPath +
                       "\": " + e.what());
            throw std::runtime_error("loadScene: entity \"" + record.name + "\" failed to load model \"" +
                                      record.modelPath + "\": " + e.what());
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

void saveScene(EntityRegistry& registry, const std::string& path) {
    std::vector<SceneEntityRecord> records;

    // Driven by Transform's pool, not ModelComponent's: every entity this
    // schema can represent has a Transform (see this function's own header
    // comment), but not every entity has a ModelComponent, so iterating
    // Transform's pool and treating ModelComponent as optional per-entity
    // (mirroring how loadScene() treats an absent "model" block) covers
    // every entity exactly once -- the reverse (driving off ModelComponent)
    // would silently skip any future Model-less entity.
    registry.each<Transform>([&](EntityId id, Transform& transform) {
        SceneEntityRecord record;
        const NameComponent* nameComponent = registry.getComponent<NameComponent>(id);
        record.name = nameComponent != nullptr ? nameComponent->name : ("entity_" + std::to_string(id.index()));
        record.position = transform.position();
        record.rotation = transform.rotation();
        record.scale = transform.scale();

        const ModelComponent* modelComponent = registry.getComponent<ModelComponent>(id);
        if (modelComponent != nullptr) {
            record.modelPath = modelComponent->path;
        }

        // Phase 8e: RigidBody/Collider round-trip the same way ModelComponent
        // does -- present only when the entity actually has that component.
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

        // Phase 14b: "parent" round-trips as the OTHER entity's own name
        // (its NameComponent if it has one, or the same generated
        // "entity_<index>" placeholder this function already uses for a
        // name-less entity above) -- see scene_serialization.hpp's own
        // "Parent references" comment for why a name, not the raw EntityId,
        // which wouldn't mean anything after a reload. Only written when
        // the parent still actually resolves to a live Transform: a
        // Parent whose id no longer names a real entity (not reachable
        // yet -- there is no entity-destroy UI before Phase 14f -- but this
        // guards against it anyway) is deliberately left OUT of the saved
        // file rather than round-tripping an unresolvable name that would
        // just fail parseSceneRecords()'s own "parent" validation on the
        // next load; the in-memory registry keeps the dangling Parent
        // component either way, only the save omits it.
        const Parent* parent = registry.getComponent<Parent>(id);
        if (parent != nullptr && parent->id.valid() && registry.getComponent<Transform>(parent->id) != nullptr) {
            const NameComponent* parentName = registry.getComponent<NameComponent>(parent->id);
            record.parentName =
                parentName != nullptr ? parentName->name : ("entity_" + std::to_string(parent->id.index()));
        }

        records.push_back(std::move(record));
    });

    writeSceneRecords(records, path);
}

}  // namespace engine
