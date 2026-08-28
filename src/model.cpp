#include "engine/model.hpp"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "engine/log.hpp"
#include "engine/paths.hpp"
#include "engine/resource_manager.hpp"
#include "engine/texture.hpp"

namespace engine {

namespace {

// Fallback diffuse texture for any mesh whose material has no usable
// diffuse texture of its own (no texture at all, or one that failed to
// load) -- reusing the same checker texture Phase 4's cube already uses
// rather than adding a second asset or a from-memory solid-color Texture
// constructor neither Texture nor this phase otherwise needs. Resolved via
// resolveAssetPath() against the executable's own directory, not the
// process's cwd -- see paths.hpp.
const std::string kFallbackTexturePath = resolveAssetPath("assets/textures/checker.png");

// Converts one Assimp aiMatrix4x4 to a glm::mat4.
//
// Assimp documents aiMatrix4x4 as row-major with translation in the last
// COLUMN of each row (m.a4/b4/c4 are the x/y/z translation components,
// each at the end of its own row) -- the usual "v' = M * v_column" layout.
// glm::mat4 is column-major and indexed as mat[col][row], so the fix is
// NOT a raw memcpy of the 16 floats (that would silently transpose the
// whole matrix -- swapping e.g. a translation into the wrong row/column --
// a classic silent bug for exactly this conversion). Each element is
// placed at its transposed [col][row] slot explicitly instead, which is
// the standard, well-known-correct aiMatrix4x4 -> glm::mat4 conversion.
glm::mat4 convertAssimpMatrix(const aiMatrix4x4& m) {
    glm::mat4 result;
    result[0][0] = m.a1;
    result[1][0] = m.a2;
    result[2][0] = m.a3;
    result[3][0] = m.a4;

    result[0][1] = m.b1;
    result[1][1] = m.b2;
    result[2][1] = m.b3;
    result[3][1] = m.b4;

    result[0][2] = m.c1;
    result[1][2] = m.c2;
    result[2][2] = m.c3;
    result[3][2] = m.c4;

    result[0][3] = m.d1;
    result[1][3] = m.d2;
    result[2][3] = m.d3;
    result[3][3] = m.d4;
    return result;
}

// Directory portion of `path` (everything before the last '/'), used to
// resolve a material's texture path (which Assimp/OBJ/MTL give relative to
// the model file, not to the process's working directory) the same way
// Shader/Texture already resolve their own asset paths -- relative to
// wherever the caller happens to run from, just joined with the model's own
// directory first. Returns "" (meaning "no directory prefix, use the path
// as-is") if `path` has no '/' at all.
std::string directoryOf(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

}  // namespace

Model::Model(const std::string& path, Shader& shader, ResourceManager& resourceManager)
    // Constructed up front (before the constructor body runs any Assimp
    // code) so it always exists as a safe fallback for any mesh whose own
    // material index is out of range -- see meshMaterialIndex_ below. Goes
    // through resourceManager (rather than constructing its own Texture)
    // so this fallback texture is the same cached instance every other
    // no-diffuse-texture material below reuses, instead of each one
    // reloading/re-uploading the same PNG independently.
    : defaultMaterial_(shader, resourceManager.getTexture(kFallbackTexturePath), /*tint=*/glm::vec3(1.0f),
                       /*shininess=*/32.0f) {
    Assimp::Importer importer;

    // aiProcess_Triangulate: this engine's Mesh/draw() only ever issues
    // GL_TRIANGLES draw calls, so any quad/n-gon face (OBJ allows both) must
    // be split into triangles first.
    //
    // aiProcess_GenSmoothNormals: fills in normals for any mesh that doesn't
    // already have its own (this is a no-op for meshes that do -- Assimp's
    // docs specifically call out that this step is skipped per-mesh when
    // normals are already present), so processMesh() below can assume
    // HasNormals() reflects "did the source file provide these, or did this
    // flag compute them" without this engine needing its own normal-
    // generation fallback.
    //
    // aiProcess_FlipUVs is deliberately NOT included. That flag exists to
    // fix formats whose texture-coordinate origin is the top-left of the
    // image (e.g. glTF, most image-editor conventions) to match OpenGL's
    // bottom-left origin. This engine's own Texture class already flips
    // pixel rows on load (stbi_set_flip_vertically_on_load(true), see
    // texture.cpp) specifically so a texture's (0,0) texel lands at OpenGL's
    // v=0, and assets/models/scene.obj (this phase's hand-authored test
    // asset) was written with vt values already following that same
    // bottom-left-origin convention (matching makeCube()'s hand-written
    // texCoords in mesh.cpp) -- so for this asset, adding FlipUVs would
    // flip an already-correct mapping and silently ship an upside-down
    // result, exactly the mistake this comment exists to avoid. A future
    // asset actually authored with top-left-origin UVs (as glTF typically
    // is) would need this flag; it's omitted here because it's wrong for
    // what this phase actually loads, not because it was overlooked.
    // aiProcess_CalcTangentSpace (Phase 7a): computes per-vertex tangents
    // (and bitangents, unused -- see mesh.hpp) from each mesh's existing
    // normals + UV channel, needed for tangent-space normal mapping. Assimp
    // requires normals and a UV channel to already be present to compute
    // this, which GenSmoothNormals/the source file's own vt data guarantee
    // by the time this step runs -- using Assimp's own well-tested
    // implementation here rather than hand-deriving the UV-delta tangent
    // formula, since Model already loads through Assimp anyway.
    const unsigned int postProcessFlags =
        aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace;

    const aiScene* scene = importer.ReadFile(path, postProcessFlags);
    if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0u || scene->mRootNode == nullptr) {
        LOG_ERROR("Model: failed to load \"" + path + "\": " + importer.GetErrorString());
        throw std::runtime_error("Model: failed to load: " + path);
    }

    const std::string directory = directoryOf(path);

    meshes_.reserve(scene->mNumMeshes);
    meshMaterialIndex_.reserve(scene->mNumMeshes);
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[i];

        std::vector<Vertex> vertices;
        vertices.reserve(mesh->mNumVertices);
        for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
            Vertex vertex;
            vertex.position = glm::vec3(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);

            // Not every mesh has normals even after aiProcess_GenSmoothNormals
            // (a degenerate all-zero-area mesh could still leave them unset)
            // -- default to a zero vector rather than reading
            // mesh->mNormals[v] when mesh->mNormals itself may be null,
            // which would be an out-of-bounds/null-pointer read.
            vertex.normal = mesh->HasNormals()
                                 ? glm::vec3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z)
                                 : glm::vec3(0.0f);

            // aiMesh can have zero UV channels; mTextureCoords[0] is null in
            // that case, so this must be checked before indexing it, not
            // just before indexing the array *within* the channel.
            vertex.texCoord = mesh->mTextureCoords[0] != nullptr
                                   ? glm::vec2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y)
                                   : glm::vec2(0.0f);

            // aiProcess_CalcTangentSpace above computes both mTangents and
            // mBitangents together (HasTangentsAndBitangents() reflects
            // whether that step actually ran/succeeded for this mesh, e.g.
            // it's skipped for a mesh with no UVs) -- default to a zero
            // tangent otherwise, same defensive null-guard pattern as
            // normal/texCoord above rather than indexing a possibly-null
            // mTangents.
            vertex.tangent = mesh->HasTangentsAndBitangents()
                                  ? glm::vec3(mesh->mTangents[v].x, mesh->mTangents[v].y, mesh->mTangents[v].z)
                                  : glm::vec3(0.0f);

            vertices.push_back(vertex);
        }

        std::vector<unsigned int> indices;
        indices.reserve(static_cast<std::size_t>(mesh->mNumFaces) * 3);
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            // aiProcess_Triangulate guarantees every face has exactly 3
            // indices; this is still checked defensively (rather than
            // assumed) so a degenerate/point/line "face" Assimp occasionally
            // leaves in some formats can't desync Mesh's GL_TRIANGLES draw
            // call by feeding it a stray 1- or 2-index run.
            if (face.mNumIndices != 3) {
                LOG_WARN("Model: skipping non-triangular face (" + std::to_string(face.mNumIndices) +
                          " indices) in \"" + path + "\"");
                continue;
            }
            indices.push_back(face.mIndices[0]);
            indices.push_back(face.mIndices[1]);
            indices.push_back(face.mIndices[2]);
        }

        meshes_.emplace_back(vertices, indices);

        const unsigned int materialIndex = mesh->mMaterialIndex;
        meshMaterialIndex_.push_back(materialIndex < scene->mNumMaterials ? static_cast<int>(materialIndex)
                                                                            : -1);
    }

    materials_.reserve(scene->mNumMaterials);
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        const aiMaterial* material = scene->mMaterials[i];

        glm::vec3 tint(1.0f);
        aiColor3D diffuseColor(1.0f, 1.0f, 1.0f);
        if (material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor) == AI_SUCCESS) {
            tint = glm::vec3(diffuseColor.r, diffuseColor.g, diffuseColor.b);
        }

        float shininess = 32.0f;
        float assimpShininess = 0.0f;
        if (material->Get(AI_MATKEY_SHININESS, assimpShininess) == AI_SUCCESS && assimpShininess > 0.0f) {
            shininess = assimpShininess;
        }

        // Only the diffuse texture slot is used -- see material.hpp: this
        // engine's Material is deliberately a single-texture-slot bundle,
        // and "attach one Material per mesh using the diffuse texture
        // Assimp found if any, else fall back to a plain one" is exactly
        // this phase's stated bar (full multi-texture-slot material
        // correctness is future-phase work).
        bool boundTexture = false;
        if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
            aiString texPath;
            if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
                const std::string relative = texPath.C_Str();
                const std::string fullPath = directory.empty() ? relative : directory + "/" + relative;
                try {
                    materials_.emplace_back(shader, resourceManager.getTexture(fullPath), tint, shininess);
                    boundTexture = true;
                } catch (const std::exception& e) {
                    // Texture() throws on a missing/undecodable file; a
                    // material this engine "doesn't fully understand" (per
                    // this phase's bar) must not crash the whole load, so
                    // fall through to the plain-texture fallback below
                    // instead of propagating.
                    LOG_WARN("Model: diffuse texture \"" + fullPath + "\" for \"" + path +
                              "\" failed to load (" + e.what() + "); using the default texture instead");
                }
            }
        }
        if (!boundTexture) {
            // Goes through the cache too, so every material that lacks its
            // own diffuse texture (this phase's whole scene.obj, see
            // scene.mtl) shares one already-loaded Texture instead of each
            // reloading/re-uploading the same PNG.
            materials_.emplace_back(shader, resourceManager.getTexture(kFallbackTexturePath), tint, shininess);
        }
    }

    // processNode() below assumes scene meshes were loaded above in
    // 0..mNumMeshes-1 order and that Model's own meshes_ vector uses that
    // same indexing -- aiNode::mMeshes[i] indexes directly into scene-
    // >mMeshes (and therefore, by construction, into meshes_) with no
    // remapping needed.
    // Small local helper (rather than a free function using recursion
    // directly) just to keep the recursive tree-building logic scoped next
    // to its one call site.
    struct NodeBuilder {
        static ModelNode build(const aiNode* node) {
            ModelNode result;
            result.name = node->mName.C_Str();
            result.localTransform = convertAssimpMatrix(node->mTransformation);

            result.meshIndices.reserve(node->mNumMeshes);
            for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
                result.meshIndices.push_back(node->mMeshes[i]);
            }

            result.children.reserve(node->mNumChildren);
            for (unsigned int i = 0; i < node->mNumChildren; ++i) {
                result.children.push_back(build(node->mChildren[i]));
            }
            return result;
        }
    };
    root_ = NodeBuilder::build(scene->mRootNode);

    LOG_INFO("Model loaded: " + path + " (" + std::to_string(meshes_.size()) + " mesh(es), " +
              std::to_string(materials_.size()) + " material(s))");
}

void Model::draw(Shader& shader, const glm::mat4& rootTransform, const Frustum* frustum, CullStats* cullStats,
                  const Texture* diffuseTextureOverride) const {
    drawNode(shader, root_, rootTransform, frustum, cullStats, diffuseTextureOverride);
}

void Model::drawNode(Shader& shader, const ModelNode& node, const glm::mat4& parentTransform,
                      const Frustum* frustum, CullStats* cullStats, const Texture* diffuseTextureOverride) const {
    const glm::mat4 worldTransform = parentTransform * node.localTransform;

    if (!node.meshIndices.empty()) {
        // Same non-shortcut normal-matrix computation Application::render()
        // uses for the (Phase 4) single cube: transpose(inverse(mat3(...)))
        // rather than mat3(...) directly, so a node with non-uniform scale
        // in its transform still lights correctly.
        const glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(worldTransform));
        shader.setMat4("uModel", worldTransform);
        shader.setMat3("uNormalMatrix", normalMatrix);

        for (const std::size_t meshIndex : node.meshIndices) {
            // Phase 13b: skip this one mesh's draw call if it's entirely
            // outside the frustum -- tested per-mesh (not once for the whole
            // node/model), each against its own bounding sphere transformed
            // by this node's own worldTransform (see BoundingSphere::
            // transformed()), so a model with parts scattered across a large
            // area still draws only the parts actually in view.
            // uModel/uNormalMatrix above are uploaded regardless of whether
            // any mesh in this node ends up culled -- that cost is shared
            // across the whole node and negligible either way, and keeping
            // it unconditional keeps this function's control flow simple.
            if (cullStats != nullptr) {
                ++cullStats->totalDrawables;
            }
            if (frustum != nullptr) {
                const BoundingSphere worldSphere = meshes_[meshIndex].boundingSphere().transformed(worldTransform);
                if (!frustum->intersects(worldSphere.center, worldSphere.radius)) {
                    if (cullStats != nullptr) {
                        ++cullStats->culledDrawables;
                    }
                    continue;
                }
            }

            const int materialIndex = meshMaterialIndex_[meshIndex];
            const Material& material =
                (materialIndex >= 0) ? materials_[static_cast<std::size_t>(materialIndex)] : defaultMaterial_;

            // Material::bind() re-issues shader.use() (a same-program
            // glUseProgram is a cheap no-op-ish call) and sets the texture/
            // tint/shininess uniforms; it deliberately never touches
            // uModel/uNormalMatrix (see material.hpp), so calling it after
            // uploading those two above is safe -- both live as ordinary
            // uniform state on the shared program object and aren't reset
            // by re-binding the same program.
            //
            // Phase 15f, scoped by a post-15f bug-review fix:
            // `diffuseTextureOverride` (nullptr for every entity except the
            // one, if any, whose MaterialOverride resolved a real texture --
            // see this method's own model.hpp comment) is passed to
            // Material::bind() ONLY when `meshIndex` is the exact mesh
            // primaryMaterial() (model.hpp) itself reads from
            // (kPrimaryMeshIndex) -- every OTHER mesh in this same node/
            // subtree draws with `nullptr`, i.e. its own original,
            // untouched Material, regardless of what
            // `diffuseTextureOverride` is. The first-pass version of this
            // forwarded the override unconditionally to every mesh in the
            // whole node subtree, which is wrong for any multi-mesh Model:
            // this project's own assets/models/scene.obj (the default
            // scene's "scene" entity) has 3 meshes/4 materials (Box,
            // Pyramid, Table), and the Inspector's Material section only
            // ever shows the user ONE representative sample
            // (primaryMaterial()) -- assigning a texture there must not
            // silently retexture the other two meshes the user was never
            // even shown. kPrimaryMeshIndex is the single shared constant
            // both this check and primaryMaterial() read, specifically so
            // "the mesh the override applies to" and "the mesh the
            // Inspector displays" can never independently drift apart.
            const Texture* diffuseOverrideForThisMesh =
                (meshIndex == kPrimaryMeshIndex) ? diffuseTextureOverride : nullptr;
            material.bind(0, diffuseOverrideForThisMesh);
            meshes_[meshIndex].bind();
            meshes_[meshIndex].draw();
        }
    }

    for (const ModelNode& child : node.children) {
        drawNode(shader, child, worldTransform, frustum, cullStats, diffuseTextureOverride);
    }
}

void Model::drawDepthOnly(Shader& shader, const glm::mat4& rootTransform) const {
    drawNodeDepthOnly(shader, root_, rootTransform);
}

void Model::drawNodeDepthOnly(Shader& shader, const ModelNode& node, const glm::mat4& parentTransform) const {
    const glm::mat4 worldTransform = parentTransform * node.localTransform;

    if (!node.meshIndices.empty()) {
        // Only uModel is needed to place geometry in light-clip-space (see
        // shadow.vert) -- no normal matrix, no Material::bind(), since depth
        // is all this pass writes.
        shader.setMat4("uModel", worldTransform);
        for (const std::size_t meshIndex : node.meshIndices) {
            meshes_[meshIndex].bind();
            meshes_[meshIndex].draw();
        }
    }

    for (const ModelNode& child : node.children) {
        drawNodeDepthOnly(shader, child, worldTransform);
    }
}

void Model::drawNormalDepth(Shader& shader, const glm::mat4& rootTransform) const {
    drawNodeNormalDepth(shader, root_, rootTransform);
}

void Model::drawNodeNormalDepth(Shader& shader, const ModelNode& node, const glm::mat4& parentTransform) const {
    const glm::mat4 worldTransform = parentTransform * node.localTransform;

    if (!node.meshIndices.empty()) {
        // Same non-shortcut normal-matrix computation drawNode()/
        // Application::render() use elsewhere -- see drawNode()'s own
        // comment on why mat3(worldTransform) directly would be wrong under
        // non-uniform scale. No Material::bind(): SSAO's G-buffer pass (see
        // gbuffer.frag) only ever writes a view-space normal + depth, never
        // a material's texture/tint/shininess.
        const glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(worldTransform));
        shader.setMat4("uModel", worldTransform);
        shader.setMat3("uNormalMatrix", normalMatrix);
        for (const std::size_t meshIndex : node.meshIndices) {
            meshes_[meshIndex].bind();
            meshes_[meshIndex].draw();
        }
    }

    for (const ModelNode& child : node.children) {
        drawNodeNormalDepth(shader, child, worldTransform);
    }
}

void Model::collectBoundingSpheres(const ModelNode& node, const glm::mat4& parentTransform,
                                    std::vector<BoundingSphere>& out) const {
    const glm::mat4 worldTransform = parentTransform * node.localTransform;
    for (const std::size_t meshIndex : node.meshIndices) {
        out.push_back(meshes_[meshIndex].boundingSphere().transformed(worldTransform));
    }
    for (const ModelNode& child : node.children) {
        collectBoundingSpheres(child, worldTransform, out);
    }
}

BoundingSphere Model::boundingSphere(const glm::mat4& rootTransform) const {
    std::vector<BoundingSphere> spheres;
    collectBoundingSpheres(root_, rootTransform, spheres);
    if (spheres.empty()) {
        // No mesh anywhere in this model (a degenerate/empty scene file) --
        // an empty sphere at the origin of rootTransform's own space is a
        // harmless, well-defined answer (radius 0 never intersects anything
        // meaningfully), rather than dividing by zero below.
        return BoundingSphere{glm::vec3(rootTransform[3]), 0.0f};
    }

    // See this method's own header comment (model.hpp) for why an
    // unweighted centroid + "farthest mesh sphere surface from it" radius,
    // not a tightest-fit sphere.
    glm::vec3 centroid(0.0f);
    for (const BoundingSphere& sphere : spheres) {
        centroid += sphere.center;
    }
    centroid /= static_cast<float>(spheres.size());

    float radius = 0.0f;
    for (const BoundingSphere& sphere : spheres) {
        radius = std::max(radius, glm::length(sphere.center - centroid) + sphere.radius);
    }
    return BoundingSphere{centroid, radius};
}

}  // namespace engine
