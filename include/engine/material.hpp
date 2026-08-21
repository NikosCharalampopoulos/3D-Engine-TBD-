#ifndef ENGINE_MATERIAL_HPP
#define ENGINE_MATERIAL_HPP

// Bundles what a draw call needs beyond raw geometry: which Shader program
// to use, a diffuse texture to sample, and a couple of simple Phong-lite
// surface properties (tint, shininess). Deliberately minimal for Phase 4 --
// no multi-texture-slot system (normal maps, specular maps, ...), no
// material asset file/serialization, no per-mesh material *table* -- just
// enough that Phase 5 (model loading) can plausibly attach one Material per
// Mesh later without this needing to be redesigned.
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
              float shininess = 32.0f)
        : shader_(&shader), diffuseTexture_(std::move(diffuseTexture)), tint(tint), shininess(shininess) {}

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
    void bind(unsigned int textureUnit = 0) const {
        shader_->use();
        diffuseTexture_->bind(textureUnit);
        shader_->setInt("uDiffuseTexture", static_cast<int>(textureUnit));
        shader_->setVec3("uTint", tint);
        shader_->setFloat("uShininess", shininess);
    }

    Shader& shader() const { return *shader_; }
    const Texture& diffuseTexture() const { return *diffuseTexture_; }

private:
    Shader* shader_;
    std::shared_ptr<Texture> diffuseTexture_;

public:
    // Public and mutable, deliberately -- these are the only two material
    // properties this phase has, and tweaking a cube's tint/shininess at
    // runtime shouldn't need setter ceremony for two POD fields. Declared
    // after the private members (matching the constructor's initializer
    // order: shader_, then diffuseTexture_, then these) so member-init
    // order and declaration order agree -- -Wreorder is part of -Wextra.
    glm::vec3 tint;
    float shininess;
};

}  // namespace engine

#endif  // ENGINE_MATERIAL_HPP
