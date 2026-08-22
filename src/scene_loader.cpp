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
#include <utility>

#include "engine/ecs.hpp"
#include "engine/log.hpp"
#include "engine/model.hpp"
#include "engine/paths.hpp"
#include "engine/resource_manager.hpp"
#include "engine/transform.hpp"

namespace engine {

void loadScene(EntityRegistry& registry, const std::string& path, ResourceManager& resources, Shader& shader) {
    // Propagates parseSceneRecords()'s own exceptions (missing file /
    // invalid JSON / bad schema) unchanged -- this function has nothing to
    // add to those cases, they're already fully diagnosed at that layer.
    const std::vector<SceneEntityRecord> records = parseSceneRecords(path);

    for (const SceneEntityRecord& record : records) {
        const EntityId id = registry.create();
        registry.addComponent<NameComponent>(id, NameComponent{record.name});

        Transform& transform = registry.addComponent<Transform>(id);
        transform.setPosition(record.position);
        transform.setRotation(record.rotation);
        transform.setScale(record.scale);

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

        records.push_back(std::move(record));
    });

    writeSceneRecords(records, path);
}

}  // namespace engine
