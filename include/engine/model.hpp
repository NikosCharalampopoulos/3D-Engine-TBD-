#ifndef ENGINE_MODEL_HPP
#define ENGINE_MODEL_HPP

// Loads a whole scene (multiple meshes arranged in a node hierarchy, each
// with its own local transform and an associated Material) from disk via
// Assimp, and knows how to draw it. This is deliberately still NOT a general
// scene graph (see transform.hpp's comment on that): Model owns exactly the
// tree it loaded from one file and only knows how to walk and draw that
// tree, parented under one caller-supplied outer transform. A real scene
// graph (multiple independent Models, lights, cameras, etc. all as nodes)
// is later-phase work.
//
// Ownership: Model owns every Mesh and Material it builds from the file (by
// value, in meshes_/materials_) plus a defaultMaterial_ fallback used for
// any mesh whose material Assimp gave us wasn't usable. Each Material's
// diffuse texture (Phase 6 on) is a std::shared_ptr<Texture> obtained from
// the caller-supplied ResourceManager rather than an owned Texture, so
// Model no longer independently reloads the same fallback checker texture
// once per mesh that needs it -- Model's destructor still cleanly releases
// every GL handle it does own (VAO/VBO/EBO per Mesh) via each member's own
// RAII destructor; any Texture is released whenever the last shared_ptr to
// it (here or in ResourceManager's cache) goes away.
//
// Move-only: Mesh and Material are themselves move-only (each owns scarce
// GL handles), so Model inherits that the same way Application's members
// do -- copying a Model would require copying live GL objects, which none
// of the underlying classes support.

#include <glm/glm.hpp>

#include <cstddef>
#include <string>
#include <vector>

#include "engine/frustum.hpp"
#include "engine/material.hpp"
#include "engine/mesh.hpp"
#include "engine/shader.hpp"

namespace engine {

class ResourceManager;

// One node from the imported scene's graph: Assimp's aiNode, converted to
// plain data this engine already understands (glm types, indices into
// Model's own mesh vector) so nothing outside model.cpp needs to touch an
// Assimp type. Owns its children by value in a tree (no parent pointers --
// traversal is top-down recursive and always has the accumulated parent
// transform in hand, so nodes never need to look upward).
struct ModelNode {
    std::string name;
    // This node's transform relative to its parent, exactly as Assimp
    // reported it (converted from aiMatrix4x4 to glm::mat4 -- see
    // model.cpp's convertAssimpMatrix()). Identity if the source format has
    // no concept of a per-node transform (e.g. plain OBJ).
    glm::mat4 localTransform{1.0f};
    // Indices into the owning Model's meshes_ (and meshMaterialIndex_)
    // vectors. A node can reference zero, one, or several meshes.
    std::vector<std::size_t> meshIndices;
    std::vector<ModelNode> children;
};

class Model {
public:
    // Loads `path` via Assimp::Importer with post-process flags suited to
    // this engine's Vertex/Mesh conventions (triangulation, normal
    // generation for meshes that don't already have their own -- see
    // model.cpp for the exact flag list and why aiProcess_FlipUVs is
    // deliberately NOT among them). Throws std::runtime_error (after
    // logging importer.GetErrorString() via LOG_ERROR) if the scene can't
    // be read/parsed, or has no root node -- mirroring Shader/Window/
    // Texture's "throw on first failure" convention rather than risking a
    // null-scene dereference.
    //
    // `shader` is used only to construct this model's per-mesh Materials
    // (Material stores a Shader* it never owns, see material.hpp) --  Model
    // does not take ownership of it and must not outlive it.
    //
    // `resourceManager` is used only during construction, to load (or reuse
    // an already-cached) Texture for each material's diffuse map and for
    // the shared default/fallback texture -- see resource_manager.hpp.
    // Model does not store a reference to it and does not need it to
    // outlive the constructor call.
    Model(const std::string& path, Shader& shader, ResourceManager& resourceManager);

    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;
    Model(Model&&) noexcept = default;
    Model& operator=(Model&&) noexcept = default;

    // Recursively draws every node's mesh(es), depth-first. `shader` must
    // already be the active program with view/projection/lighting uniforms
    // uploaded by the caller (see Application::render()) -- those are
    // scene-level state set once per frame, not per node/mesh. For each
    // node this uploads that node's composed world model matrix (and the
    // matching normal matrix) to `shader`, then for each of the node's
    // meshes binds its Material (texture/tint/shininess) and issues the
    // draw call.
    //
    // `rootTransform` is composed as `rootTransform * node.localTransform`,
    // recursively (child world transform = parent world transform * child's
    // own local transform) -- so a caller-supplied outer transform (e.g.
    // Application's placement of the whole model in the scene) sits above
    // the file's own node hierarchy rather than replacing it.
    //
    // Phase 13b: `frustum`, if non-null, is tested against each individual
    // mesh's own bounding sphere (see mesh.hpp's BoundingSphere), transformed
    // by that mesh's own accumulated world transform -- not one bounding
    // volume for the whole model -- so a multi-node model with parts
    // scattered across a large area still only draws the parts actually in
    // view, not all-or-nothing. A mesh entirely outside `frustum` has its
    // draw call skipped (its uModel/uNormalMatrix are still uploaded once
    // per node exactly as before -- see drawNode() -- since that cost is
    // shared across every mesh in the node and is negligible either way).
    // `cullStats`, if non-null, is incremented once per mesh considered
    // (whether culled or drawn) so a caller can log/verify culling actually
    // happened -- see Application::render(). Passing frustum == nullptr
    // (the default) draws unconditionally, exactly as before this phase.
    void draw(Shader& shader, const glm::mat4& rootTransform = glm::mat4(1.0f), const Frustum* frustum = nullptr,
              CullStats* cullStats = nullptr) const;

    // Phase 7a: draws every node's mesh geometry with `shader` (expected to
    // be the depth-only shadow shader, see shadow_map.hpp/assets/shaders/
    // shadow.vert) uploading only each node's uModel -- no normal matrix, no
    // Material::bind() (no texture/tint/shininess uniforms are used or even
    // exist on that shader), since only depth is being written. Kept as a
    // separate entry point rather than a flag on draw() so the normal draw
    // path's uniform/material contract doesn't grow a shadow-pass-only
    // special case.
    void drawDepthOnly(Shader& shader, const glm::mat4& rootTransform = glm::mat4(1.0f)) const;

    // Phase 13f: draws every node's mesh geometry with `shader` (expected to
    // be SSAO's G-buffer shader, see application.cpp's Phase 13f render()
    // additions and assets/shaders/gbuffer.vert/.frag) uploading each node's
    // uModel *and* uNormalMatrix -- unlike drawDepthOnly() above, SSAO's
    // geometry pass needs each fragment's normal (to build its per-pixel
    // occlusion sample basis), not just its depth -- but still no
    // Material::bind(), since no material property (texture, tint,
    // shininess) is ever read by that pass either. A third entry point
    // rather than a flag on drawDepthOnly() or draw(), matching this class's
    // existing "one dedicated method per pass's exact uniform contract"
    // convention.
    void drawNormalDepth(Shader& shader, const glm::mat4& rootTransform = glm::mat4(1.0f)) const;

    // Phase 14d: one bounding sphere aggregating every mesh across every
    // node of this model (not just its root node's own meshes -- scene.obj's
    // "scene" entity has three, one per node: pyramid/table/box), each
    // transformed into `rootTransform`'s space by its own accumulated node
    // transform first (the same worldTransform drawNode() composes for
    // drawing). Used by the Scene Hierarchy's selection outline
    // (Application::render(), see this file's own Phase 14d comment) to get
    // one world-space volume to project onto the screen for a selected
    // entity's whole model, without the editor UI needing to know how many
    // meshes/nodes that model actually has.
    //
    // Not the tightest possible enclosing sphere (that's a harder,
    // non-trivial computational-geometry problem -- Welzl's algorithm and
    // similar) -- this instead centers on the unweighted centroid of every
    // individual mesh sphere's own center, then sets the radius to the
    // farthest any single mesh sphere's own surface (center-to-centroid
    // distance plus that mesh's own radius) reaches from that centroid. That
    // is always a conservative superset of the model's real extent (matching
    // BoundingSphere::transformed()'s own "never cull something that should
    // still be visible" bias, mesh.hpp), which is exactly the property this
    // phase's outline projection needs -- a screen-space rectangle a little
    // larger than the object's exact silhouette reads fine visually; one
    // that's too small and clips into the object would not.
    BoundingSphere boundingSphere(const glm::mat4& rootTransform = glm::mat4(1.0f)) const;

    // Phase 14e: read-only access to one representative Material, for the
    // Inspector panel's Material section (see editor_ui.cpp's Phase 14e
    // comment) to display a selected entity's texture/tint/shininess.
    // Deliberately returns `const Material&`, not `Material&` -- see this
    // class's own header comment above: Model instances are cached and
    // SHARED across every entity that loaded the same asset path (via
    // ResourceManager, resource_manager.hpp), so a mutable reference here
    // would let editing one entity's "material" in the Inspector silently
    // repaint every other entity sharing this same cached Model. Read-only
    // display is this phase's own deliberate scope choice for exactly that
    // reason -- see material.hpp's own Phase 14e comment for the fuller
    // writeup of the tradeoff this sidesteps.
    //
    // Not "the" material of a possibly-multi-mesh model (scene.obj's "scene"
    // entity alone has three nodes, each with its own material) -- this
    // returns whichever material meshes_[0] (the first mesh Assimp reported,
    // not necessarily the root node's own mesh) actually uses, falling back
    // to defaultMaterial_ if meshMaterialIndex_ is empty (no meshes at all,
    // defensive only) or out of range (see meshMaterialIndex_'s own comment
    // below for when that can happen). A single representative sample is
    // exactly what a "what does this thing generally look like" Inspector
    // readout needs; a full per-mesh material list is real, separate scope
    // this phase's own brief doesn't ask for.
    const Material& primaryMaterial() const {
        if (meshMaterialIndex_.empty() || meshMaterialIndex_.front() < 0) {
            return defaultMaterial_;
        }
        return materials_[static_cast<std::size_t>(meshMaterialIndex_.front())];
    }

private:
    void drawNode(Shader& shader, const ModelNode& node, const glm::mat4& parentTransform, const Frustum* frustum,
                  CullStats* cullStats) const;
    void drawNodeDepthOnly(Shader& shader, const ModelNode& node, const glm::mat4& parentTransform) const;
    void drawNodeNormalDepth(Shader& shader, const ModelNode& node, const glm::mat4& parentTransform) const;
    // Phase 14d: recursive helper for the public boundingSphere() above --
    // appends one already-`worldTransform`-transformed BoundingSphere per
    // mesh referenced anywhere in `node`'s own subtree.
    void collectBoundingSpheres(const ModelNode& node, const glm::mat4& parentTransform,
                                 std::vector<BoundingSphere>& out) const;

    std::vector<Mesh> meshes_;
    std::vector<Material> materials_;
    // meshMaterialIndex_[i] indexes into materials_ for meshes_[i], or -1 if
    // that mesh's Assimp material index was out of range (defensive only --
    // Assimp guarantees a default material exists, so this should never
    // actually be -1 in practice) and defaultMaterial_ should be used
    // instead.
    std::vector<int> meshMaterialIndex_;
    Material defaultMaterial_;
    ModelNode root_;
};

}  // namespace engine

#endif  // ENGINE_MODEL_HPP
