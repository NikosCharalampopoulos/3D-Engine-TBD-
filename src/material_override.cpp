// Phase 15f: see material_override.hpp's own header comment for the full
// design (why a per-entity override, not a clone-on-edit). This translation
// unit is deliberately tiny -- one function -- and depends on nothing beyond
// ecs.hpp, the same "pure ECS-pool lookup, no GL/rendering dependency"
// property light.hpp's own resolveActiveDirectionalLight()/
// collectPointLights() (src/light.cpp) already have, and for the identical
// reason: tests/material_override_test.cpp links this file alone, with no
// GLFW/GL/Assimp/Dear ImGui in the executable at all.

#include "engine/material_override.hpp"

#include "engine/ecs.hpp"

namespace engine {

const Texture* resolveDiffuseTextureOverride(EntityRegistry& registry, EntityId id) {
    const MaterialOverride* materialOverride = registry.getComponent<MaterialOverride>(id);
    if (materialOverride == nullptr || materialOverride->diffuseTexture == nullptr) {
        return nullptr;
    }
    return materialOverride->diffuseTexture.get();
}

}  // namespace engine
