#ifndef ENGINE_PBR_MATERIAL_HPP
#define ENGINE_PBR_MATERIAL_HPP

// Phase 9: a metallic/roughness PBR material -- a value bundle (albedo tint,
// metallic, roughness, ambient-occlusion scalar, plus two optional textures)
// and a bind() that uploads it as uniforms, matching engine::Material's own
// shape (see material.hpp) but for assets/shaders/pbr.vert/pbr.frag's
// Cook-Torrance BRDF instead of basic.vert/basic.frag's Blinn-Phong model.
//
// Deliberately NOT a replacement for Material: basic.vert/basic.frag and the
// existing table/box/pyramid/ground scene stay exactly as they were (see
// README.md's Phase 4/7a notes) -- this is a second, independent material
// type for the new PBR shader/sphere-grid content this phase adds, not a
// migration of everything that currently uses Material.
//
// Fields:
//   - albedo: the surface's base color. For a dielectric this is its diffuse
//     reflectance; for a metal it doubles as the tinted specular reflectance
//     (F0) instead -- see pbr.frag's `F0 = mix(vec3(0.04), albedo,
//     metallic)`.
//   - metallic: 0 = pure dielectric (insulator), 1 = pure metal. Physically
//     this is close to binary in most real materials (few things are
//     "40% metal"), but kept as a continuous float since that's what the
//     BRDF equations take and what lets one shader handle both ends plus any
//     hand-authored in-between value.
//   - roughness: 0 = mirror-smooth, 1 = fully rough/matte. Clamped away from
//     exactly 0 in bind() (see kMinRoughness below) -- alpha = roughness^2
//     appears in the GGX distribution's denominator, and alpha == 0 makes
//     that term singular (a divide-by-zero-shaped spike) at N.H == 1, which
//     is exactly the kind of "looks plausible until a NaN/Inf sneaks in at a
//     shiny grazing angle" bug real PBR implementations are notorious for.
//   - ao: a simple scalar ambient-occlusion multiplier (default 1.0, i.e. no
//     occlusion) applied only to the ambient term in pbr.frag -- this phase
//     has no baked/computed AO map or SSAO pass, just the uniform knob.
//
// albedoMap_/normalMap_ are optional (nullable) std::shared_ptr<Texture>,
// mirroring Material's own normalMap_ field: nice-to-haves for a future
// textured PBR asset, not required by this phase's sphere-grid demo (every
// sphere uses a flat albedo tint and its own procedural vertex normal, no
// textures at all). When absent, bind() uploads uUseAlbedoMap/uUseNormalMap
// = 0 every call -- never left stale from whatever the previously-bound
// PBRMaterial (sharing the same GL program) last set them to.
//
// Copyable/movable (not move-only like Material): PBRMaterial holds no
// exclusive GL handle of its own -- just a non-owning Shader* and two
// shared_ptr<Texture> -- so ordinary value semantics are safe and
// considerably more convenient for something meant to be built many times
// over (this phase's 4x4 sphere grid is 16 of these in one std::vector).

#include <algorithm>
#include <glm/glm.hpp>
#include <memory>
#include <utility>

#include "engine/shader.hpp"
#include "engine/texture.hpp"

namespace engine {

class PBRMaterial {
public:
    PBRMaterial(Shader& shader, const glm::vec3& albedo, float metallic, float roughness, float ao = 1.0f,
                std::shared_ptr<Texture> albedoMap = nullptr, std::shared_ptr<Texture> normalMap = nullptr)
        : shader_(&shader),
          albedoMap_(std::move(albedoMap)),
          normalMap_(std::move(normalMap)),
          albedo(albedo),
          metallic(metallic),
          roughness(roughness),
          ao(ao) {}

    // Roughness floor applied in bind(), not stored on the field itself --
    // `roughness` keeps whatever value a caller set (so e.g. printing/UI code
    // sees the real authored value), only the uniform actually uploaded to
    // the GPU is clamped.
    static constexpr float kMinRoughness = 0.045f;

    void bind(unsigned int textureUnit = 0) const {
        shader_->use();
        shader_->setVec3("uAlbedo", albedo);
        shader_->setFloat("uMetallic", std::clamp(metallic, 0.0f, 1.0f));
        shader_->setFloat("uRoughness", std::clamp(roughness, kMinRoughness, 1.0f));
        shader_->setFloat("uAO", std::clamp(ao, 0.0f, 1.0f));

        if (albedoMap_) {
            albedoMap_->bind(textureUnit);
            shader_->setInt("uAlbedoMap", static_cast<int>(textureUnit));
            shader_->setInt("uUseAlbedoMap", 1);
        } else {
            shader_->setInt("uUseAlbedoMap", 0);
        }
        // Normal map bound at textureUnit + 1, same convention as
        // Material::bind() (see material.hpp) -- diffuse/albedo always takes
        // textureUnit itself so the two never collide.
        if (normalMap_) {
            normalMap_->bind(textureUnit + 1);
            shader_->setInt("uNormalMap", static_cast<int>(textureUnit + 1));
            shader_->setInt("uUseNormalMap", 1);
        } else {
            shader_->setInt("uUseNormalMap", 0);
        }
    }

    Shader& shader() const { return *shader_; }
    bool hasAlbedoMap() const { return albedoMap_ != nullptr; }
    bool hasNormalMap() const { return normalMap_ != nullptr; }

private:
    Shader* shader_;
    std::shared_ptr<Texture> albedoMap_;
    std::shared_ptr<Texture> normalMap_;

public:
    // Public and mutable, matching Material's tint/shininess -- plain
    // per-material data callers are expected to poke directly, not something
    // that needs setter ceremony. Declared after the private members so
    // member-init order and declaration order agree (-Wreorder is part of
    // -Wextra).
    glm::vec3 albedo;
    float metallic;
    float roughness;
    float ao;
};

}  // namespace engine

#endif  // ENGINE_PBR_MATERIAL_HPP
