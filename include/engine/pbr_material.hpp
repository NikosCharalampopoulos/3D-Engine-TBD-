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
// textured PBR asset, not required by Phase 9's sphere-grid demo (every
// sphere there uses a flat albedo tint and its own procedural vertex normal,
// no textures at all). When absent, bind() uploads
// uUseAlbedoMap/uUseNormalMap/uUseORMMap = 0 every call -- never left stale
// from whatever the previously-bound PBRMaterial (sharing the same GL
// program) last set them to.
//
// Phase 11 adds a third optional texture, metallicRoughnessMap_ -- a packed
// "ORM" map (R = ambient occlusion, G = roughness, B = metallic, the common
// glTF-style packing convention: one texture fetch instead of three) used by
// this phase's textured sphere materials (rusted metal, scuffed plastic --
// see application.cpp). When bound, pbr.frag samples it and uses its
// G/B channels directly for roughness/metallic instead of the scalar
// uRoughness/uMetallic uniforms, and multiplies its R channel by uAO (so the
// scalar AO knob still works as an overall multiplier even with a map
// bound, exactly like uAlbedo still tints uAlbedoMap's sampled color rather
// than being ignored outright). Bound at textureUnit + 2 -- textureUnit
// itself is always the albedo map, textureUnit + 1 the normal map (see
// bind() below), so the three never collide as long as nothing else is
// bound to textureUnit + 2 while a PBRMaterial::bind() call is live; see
// application.cpp's kShadowMapTextureUnit/kIrradianceMapTextureUnit's own
// comment for why those live at units 3+ rather than 2, specifically to
// leave this slot free.
//
// Copyable/movable (not move-only like Material): PBRMaterial holds no
// exclusive GL handle of its own -- just a non-owning Shader* and three
// shared_ptr<Texture> -- so ordinary value semantics are safe and
// considerably more convenient for something meant to be built many times
// over (Phase 9's 4x4 sphere grid is 16 of these in one std::vector).

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
                std::shared_ptr<Texture> albedoMap = nullptr, std::shared_ptr<Texture> normalMap = nullptr,
                std::shared_ptr<Texture> metallicRoughnessMap = nullptr)
        : shader_(&shader),
          albedoMap_(std::move(albedoMap)),
          normalMap_(std::move(normalMap)),
          metallicRoughnessMap_(std::move(metallicRoughnessMap)),
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
        // Packed ORM map (R = AO, G = roughness, B = metallic), bound at
        // textureUnit + 2 -- see this class's header comment on why that
        // unit is reserved for exactly this and doesn't collide with the
        // shadow map / IBL maps' own fixed units.
        if (metallicRoughnessMap_) {
            metallicRoughnessMap_->bind(textureUnit + 2);
            shader_->setInt("uORMMap", static_cast<int>(textureUnit + 2));
            shader_->setInt("uUseORMMap", 1);
        } else {
            shader_->setInt("uUseORMMap", 0);
        }
    }

    Shader& shader() const { return *shader_; }
    bool hasAlbedoMap() const { return albedoMap_ != nullptr; }
    bool hasNormalMap() const { return normalMap_ != nullptr; }
    bool hasMetallicRoughnessMap() const { return metallicRoughnessMap_ != nullptr; }

private:
    Shader* shader_;
    std::shared_ptr<Texture> albedoMap_;
    std::shared_ptr<Texture> normalMap_;
    std::shared_ptr<Texture> metallicRoughnessMap_;

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
