#ifndef ENGINE_MATERIAL_OVERRIDE_HPP
#define ENGINE_MATERIAL_OVERRIDE_HPP

// Phase 15f: closes the Material Inspector's own long-`BeginDisabled()`'d
// "Browse..." gap (editor_ui.cpp, Phase 14e) -- the button that lets a user
// assign a different diffuse texture to ONE selected entity's material.
//
// --- The hazard this file exists to avoid -------------------------------
// material.hpp's own Phase 14e comment (and model.hpp's Phase 14e
// primaryMaterial() comment) already explain WHY that button stayed
// disabled through Phases 14e-15e: a ModelComponent's `model` is a
// std::shared_ptr<Model> obtained from ResourceManager's get-or-load cache
// (resource_manager.hpp), so every entity that loads the same asset path
// shares the exact same Model instance -- and therefore the exact same
// Material instances, since Model owns its Materials by value in its own
// materials_ vector (model.hpp). This project's own default scene already
// has two entities sharing one Model (assets/scenes/default.json's
// "falling_cube" and "parented_demo_cube" both load
// assets/models/falling_cube.obj) -- a real, not hypothetical, hazard: if
// the Inspector mutated that shared Material's diffuseTexture_ in place for
// one entity, the OTHER entity sharing the same cached Model would silently
// repaint too.
//
// --- The fix: a per-entity override layered ON TOP of the shared cache,
// never INTO it ----------------------------------------------------------
// Two designs were available to let one entity's texture change without
// this collateral damage:
//   (a) Per-entity override (chosen): give the entity its own, genuinely
//       separate MaterialOverride component holding the replacement
//       Texture. The shared Model/Material is NEVER mutated -- Model::draw()
//       (model.hpp) still reads its own materials_ unchanged; it just
//       accepts an optional "bind THIS texture instead, for this one draw
//       call" override from its caller (Material::bind(), material.hpp),
//       and Application::render() supplies that override only for the one
//       entity that actually has a MaterialOverride component
//       (resolveDiffuseTextureOverride() below). ResourceManager's cache
//       itself is untouched -- getModel()/getTexture() still hand back the
//       exact same shared instances they always have; this override is pure
//       ECS-side, additive state that happens to steer what one draw call
//       reads.
//   (b) Clone-on-first-edit: deep-copy the entity's whole Model (or just its
//       Material) out of the cache into an entity-owned copy the moment it's
//       first edited, so it stops aliasing the shared original. Rejected:
//       Model and Material are both deliberately move-only (see their own
//       header comments -- "nothing here currently needs to copy a
//       Material, so the safer/more consistent default... is kept rather
//       than opening that door speculatively"), so this would require
//       adding a real copy/clone operation to two GL-resource-owning
//       classes neither of which has ever needed one, purely to serve this
//       one Inspector button. It would also break ModelComponent::path's
//       existing contract (ecs.hpp's own comment: "the asset path `model`
//       was loaded from... a *reloadable* reference") -- the moment an
//       entity's Model diverges from what its own path would reload,
//       ModelComponent::path stops accurately describing what that entity
//       actually renders, which Phase 15e's scene serialization (and any
//       future re-load) depends on being true. (a) sidesteps both problems:
//       no new copy/clone machinery anywhere, and ModelComponent::path keeps
//       meaning exactly what it always has -- the override is tracked as its
//       own separate, independently-serialized fact (see below), not folded
//       into what the model path claims to be.
//
// --- Scope: diffuse texture only, this phase -----------------------------
// material.hpp's own tint/shininess fields stay exactly as read-only as
// Phase 14e left them -- this phase's own brief calls that an explicit,
// deliberate choice, not an oversight: the SAME shared-cache hazard this
// whole file exists to solve applies to them too, and the Asset Browser
// (Phase 15d) only ever gives a user something to PICK for the texture case
// (a file from assets/textures/) -- there is no equivalent "browse for a
// tint" concept, so tint/shininess would need their own, differently-shaped
// UI (a plain ColorEdit3/DragFloat writing into this same override
// mechanism) that this phase's own "Browse..." brief doesn't ask for. A
// MaterialOverride gaining optional tint/shininess fields alongside
// diffuseTexture later is the natural, additive way to extend this same
// mechanism -- see this struct's own comment below.
//
// --- Scope: which MESH, for a multi-mesh Model (post-15f bug-review fix) --
// A ModelComponent's Model can own more than one mesh/Material -- this
// project's own assets/models/scene.obj (the default scene's "scene"
// entity) has 3 meshes/4 materials (Box, Pyramid, Table, each visually
// distinct). The Inspector's Material section only ever shows the user ONE
// representative sample, Model::primaryMaterial() (model.hpp) -- it never
// implies a "Browse..." pick is retexturing every mesh in the entity at
// once. The first-pass implementation of this override got that wrong: it
// forwarded MaterialOverride's texture to EVERY mesh Model::drawNode()
// (model.cpp) drew for the overridden entity, so assigning a texture to
// "scene" silently replaced the Pyramid's and Table's materials too, even
// though the Inspector never showed the user those. Fixed by scoping the
// override to apply ONLY to the exact mesh primaryMaterial() itself samples
// from (Model::kPrimaryMeshIndex, model.hpp/.cpp) -- every other mesh in the
// same entity draws with its own original, untouched Material regardless of
// this override. "What you see in the Inspector is what your override
// actually changes" now holds for every Model this engine loads, not just
// the common single-mesh case.
//
// --- Round-trips through Save Scene (Phase 15e) --------------------------
// Unlike Phase 15a/15b/15c's own PointLight/DirectionalLight/CameraComponent
// (deferred from scene serialization because, at the time each was written,
// nothing yet WROTE one through the editor), this override is created
// directly by a live Inspector action -- a user who assigns a texture and
// then presses Ctrl+S would find Save Scene silently discarding their edit
// if this were left unserialized, which is a real, user-visible regression,
// not a "nothing exercises this yet" gap. So `MaterialOverride` round-trips
// through scene_serialization.hpp's schema (`materialOverride` block) the
// same opt-in-per-entity way `rigidBody`/`pointLight`/etc. already do -- see
// that header's own comment for the schema itself.

#include <memory>
#include <string>

namespace engine {

class EntityRegistry;
// Forward-declared only for resolveDiffuseTextureOverride()'s by-value
// EntityId parameter below -- the same "just the declaration" shape
// light.hpp/scene_serialization.hpp already establish for the identical
// reason (see either header's own comment): this file never needs
// ecs.hpp's full EntityId definition, only a name for the parameter.
class EntityId;
class Texture;

// One entity's OPTIONAL diffuse-texture override -- present only on an
// entity whose Inspector "Browse..." action (or ENGINE_DEBUG_ASSIGN_TEXTURE,
// see application.cpp) has actually assigned one; absent means "draw with
// whatever this entity's own ModelComponent::model's Material already
// carries, unchanged" (the overwhelmingly common case, and every entity
// before this phase). Never installed by ResourceManager/Model themselves --
// this is purely additive ECS state Application::render() consults, the
// shared cache neither knows nor cares that it exists.
//
// diffuseTexture is the actual GPU-resident Texture to bind instead of the
// Model's own baked-in diffuse map -- itself obtained from
// ResourceManager::getTexture() (so reassigning the SAME texture path to two
// different entities still shares one GPU upload, exactly like every other
// Texture in this engine -- this override adds a second reference to an
// already-cached Texture, it doesn't grow ResourceManager a second,
// override-only loading path). Nullable in principle (a
// default-constructed MaterialOverride has a null diffuseTexture) so
// resolveDiffuseTextureOverride() below has one unambiguous way to say "no
// override, even though a MaterialOverride component happens to be
// present" -- see that function's own comment for why that distinction is
// worth keeping rather than just removeComponent<MaterialOverride>() the
// instant an override is cleared (either is valid; both are handled).
//
// diffuseTexturePath mirrors ModelComponent::path's own convention (ecs.hpp):
// the SAME relative, reloadable string form ("assets/textures/foo.png", not
// an already-resolved absolute path) scene_serialization.cpp's
// materialOverride block round-trips, kept alongside the live Texture
// pointer for the identical reason ModelComponent keeps `path` alongside
// `model` -- Texture itself has no notion of "the path it was loaded from"
// beyond its own path() accessor (texture.hpp), and that accessor returns
// whatever string Texture's OWN constructor was given (which, for every
// Texture in this engine, is already an ABSOLUTE resolveAssetPath()'d path
// -- see resource_manager.cpp's getTexture()), not the portable relative
// form a scene file needs to stay reloadable across machines/checkouts.
struct MaterialOverride {
    std::shared_ptr<Texture> diffuseTexture;
    std::string diffuseTexturePath;
};

// Resolves what Model::draw() should actually bind as `id`'s diffuse
// texture this frame: `id`'s own MaterialOverride component's
// diffuseTexture, if the entity has one AND it's non-null, else nullptr
// (meaning "no override -- draw with whatever the shared Model's own
// Material already carries, unchanged"). Every OTHER entity sharing that
// same cached Model (e.g. "parented_demo_cube" alongside an overridden
// "falling_cube") simply has no MaterialOverride component at all, so this
// returns nullptr for it and its own draw call reads the shared Material
// completely untouched -- this one function is the entire "does entity X's
// material come from its own override or the shared Model's" decision this
// phase's design has to get right, and it is pulled out here, on its own,
// specifically so it can be tested in isolation (tests/
// material_override_test.cpp) without a live GL context, a real loaded
// Texture, or a Dear ImGui frame -- the identical "small pure resolution
// function, unit-tested on its own" shape light.hpp's own
// resolveActiveDirectionalLight()/collectPointLights() already established
// for this codebase's other "which of several possible sources wins"
// questions.
const Texture* resolveDiffuseTextureOverride(EntityRegistry& registry, EntityId id);

}  // namespace engine

#endif  // ENGINE_MATERIAL_OVERRIDE_HPP
