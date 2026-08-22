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

private:
    void drawNode(Shader& shader, const ModelNode& node, const glm::mat4& parentTransform, const Frustum* frustum,
                  CullStats* cullStats) const;
    void drawNodeDepthOnly(Shader& shader, const ModelNode& node, const glm::mat4& parentTransform) const;

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
