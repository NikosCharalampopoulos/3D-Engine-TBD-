#ifndef ENGINE_MATERIAL_HPP
#define ENGINE_MATERIAL_HPP

// Bundles what a draw call needs beyond raw geometry: which Shader program
// to use, a diffuse texture to sample, an optional normal map, and a couple
// of simple Phong-lite surface properties (tint, shininess). Deliberately
// minimal for Phase 4 -- no general multi-texture-slot system (specular
// maps, roughness/metalness, ...), no material asset file/serialization, no
// per-mesh material *table* -- just enough that Phase 5 (model loading)
// could plausibly attach one Material per Mesh, and (Phase 7a) that one
// optional texture slot -- a normal map -- could be bolted on without a
// redesign.
//
// Phase 7a's normalMap is a nullable std::shared_ptr<Texture>, deliberately
// NOT required: most of this engine's materials (the hand-authored
// scene.obj's Kd-only materials) have no normal map and should keep
// rendering exactly as before, lit from the vertex normal directly. bind()
// uploads a uUseNormalMap flag every call (0 or 1) precisely so the shared
// program's uniform state can't leak a previous material's "yes, sample the
// normal map" flag onto one that has none.
//
// Holds the diffuse texture as a std::shared_ptr<Texture> rather than by
// value: since Phase 6, textures are loaded through ResourceManager's cache
// (see resource_manager.hpp) and can legitimately be shared by several
// Materials at once (e.g. Model's per-mesh materials that all fall back to
// the same default checker texture) -- Material references the cached
// Texture rather than owning an independent copy of it. The Shader is still
// held only by pointer, since a shader program is typically shared across
// many materials/objects at once and is owned elsewhere (ResourceManager,
// via Application); Material does not manage the Shader's lifetime and
// never outlives it.
//
// Still move-only, matching every other GL-resource-adjacent class in this
// engine (Shader/Texture/Mesh/Model) -- shared_ptr's own copyability would
// make Material copyable too if that delete were removed, but nothing here
// currently needs to copy a Material, so the safer/more consistent default
// (move-only, opt into copying later if something actually needs it) is
// kept rather than opening that door speculatively.

#include <glm/glm.hpp>

#include <memory>
#include <utility>

#include "engine/shader.hpp"
#include "engine/texture.hpp"

namespace engine {

class Material {
public:
    Material(Shader& shader, std::shared_ptr<Texture> diffuseTexture, const glm::vec3& tint = glm::vec3(1.0f),
              float shininess = 32.0f, std::shared_ptr<Texture> normalMap = nullptr)
        : shader_(&shader),
          diffuseTexture_(std::move(diffuseTexture)),
          normalMap_(std::move(normalMap)),
          tint(tint),
          shininess(shininess) {}

    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;
    Material(Material&&) noexcept = default;
    Material& operator=(Material&&) noexcept = default;

    // Binds the shader program, binds the diffuse texture to `textureUnit`,
    // and uploads the matching sampler uniform plus this material's
    // tint/shininess. Deliberately does NOT touch model/view/projection or
    // lighting uniforms -- those are scene- and per-draw-level state that
    // the caller (Application::render(), for now) sets separately right
    // after calling this: this sets what's fixed per-material, the caller
    // sets what varies frame-to-frame.
    // Normal map (if any) is bound at textureUnit + 1 -- diffuse always
    // takes textureUnit itself, so the two never collide as long as callers
    // don't also put something else on textureUnit + 1 (nothing in this
    // engine does; every call site uses the default textureUnit = 0).
    void bind(unsigned int textureUnit = 0) const {
        shader_->use();
        diffuseTexture_->bind(textureUnit);
        shader_->setInt("uDiffuseTexture", static_cast<int>(textureUnit));
        if (normalMap_) {
            normalMap_->bind(textureUnit + 1);
            shader_->setInt("uNormalMap", static_cast<int>(textureUnit + 1));
            shader_->setInt("uUseNormalMap", 1);
        } else {
            // Uploaded every call (not just when a normal map is absent)
            // regardless of what the previously-bound Material set this
            // uniform to -- it lives on the shared GL program object and
            // would otherwise leak the last normal-mapped material's "yes"
            // flag onto this one.
            shader_->setInt("uUseNormalMap", 0);
        }
        shader_->setVec3("uTint", tint);
        shader_->setFloat("uShininess", shininess);
    }

    Shader& shader() const { return *shader_; }
    const Texture& diffuseTexture() const { return *diffuseTexture_; }
    bool hasNormalMap() const { return normalMap_ != nullptr; }

private:
    Shader* shader_;
    std::shared_ptr<Texture> diffuseTexture_;
    std::shared_ptr<Texture> normalMap_;

public:
    // Public and mutable, deliberately -- these are the only two material
    // properties this phase has, and tweaking a cube's tint/shininess at
    // runtime shouldn't need setter ceremony for two POD fields. Declared
    // after the private members (matching the constructor's initializer
    // order: shader_, then diffuseTexture_, then normalMap_, then these) so
    // member-init order and declaration order agree -- -Wreorder is part of
    // -Wextra.
    //
    // Phase 14e note: this mutability is exactly why the new Inspector
    // panel's Material section (editor_ui.cpp) deliberately does NOT expose
    // `tint`/`shininess` as live-editable ImGui widgets, even though a
    // DragFloat3 bound directly to `tint` would be a one-line change. The
    // Material a selected entity's ModelComponent points at is not that
    // entity's own private copy -- model.hpp's own header comment explains
    // Model instances are cached and SHARED via ResourceManager whenever
    // several entities load the same asset path (e.g. both
    // "parented_demo_cube" and "falling_cube" load
    // assets/models/falling_cube.obj today, see assets/scenes/default.json),
    // so mutating `tint` through one entity's Inspector selection would
    // silently repaint every OTHER entity sharing that same cached Model too
    // -- a real, surprising footgun for whoever adds live material editing
    // later, not a hypothetical one. This phase's own scope call: display
    // Model::primaryMaterial()'s tint/shininess/texture read-only (see
    // model.hpp's Phase 14e comment) rather than ship that footgun, or
    // silently editable-in-name-only fields. A future phase that wants real
    // per-entity material editing needs an actual per-entity material
    // override/clone step first (e.g. Model gaining a "give me my own
    // unshared copy of this material" operation) -- there isn't one today.
    glm::vec3 tint;
    float shininess;
};

}  // namespace engine

#endif  // ENGINE_MATERIAL_HPP
