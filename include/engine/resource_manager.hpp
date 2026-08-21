#ifndef ENGINE_RESOURCE_MANAGER_HPP
#define ENGINE_RESOURCE_MANAGER_HPP

// A simple per-key cache for the engine's three loaded asset types (Shader,
// Texture, Model), so construction code doesn't have to ad-hoc load each
// asset itself and so the same asset is never redundantly reloaded from
// disk/re-uploaded to the GPU just because more than one caller wants it.
// Concretely this replaces Phase 5's pattern of Application constructing
// its Shader/Model directly (and, less obviously, Model's own material
// loop reconstructing the fallback checker texture from scratch for every
// material that needed it -- four separate GL texture uploads of the exact
// same file for this phase's scene.obj alone) with one shared cache both
// paths draw from.
//
// Deliberately NOT a general asset-management system: no reference-counted
// eviction, no LRU, no hot-reload, no async loading -- just an
// unordered_map<key, shared_ptr<T>> per asset type, populated on first
// request ("get-or-load") and handed back again on every later request for
// the same key. A failed load is never cached (the exception propagates
// before anything is inserted), so a later retry with the same key tries
// the load again rather than permanently remembering the failure. Ordinary
// shared_ptr/destructor order is enough cleanup: whichever of
// ResourceManager or its cached assets' other owners (e.g. Application's
// entities_) outlives the other, the assets are destroyed exactly once,
// when the last shared_ptr to each goes away -- no explicit teardown method
// needed here.
//
// Cache keys: Texture's key includes generateMipmaps (two requests for the
// same path with different mipmap settings are genuinely different GL
// objects, not interchangeable). Model's key includes the identity (GL
// program id) of the shader it's requested against, since a Model's
// per-mesh Materials are permanently bound to whichever Shader& was passed
// to Model's constructor -- caching solely by path would silently hand back
// a Model wired to the wrong shader if a caller ever requested the same
// model path against a second shader. This engine only ever has one Shader
// in practice, so that collision can't happen yet, but the key is
// future-proofed against it rather than assuming "one shader" forever.

#include <memory>
#include <string>
#include <unordered_map>

namespace engine {

class Shader;
class Texture;
class Model;

class ResourceManager {
public:
    ResourceManager() = default;

    // Not copied or moved: nothing in this phase needs to duplicate or
    // relocate a ResourceManager, and Model instances built through
    // getModel() capture a reference to *this while a texture is being
    // resolved, so letting it be moved out from under a caller who kept a
    // pointer/reference around would be a foot-gun for no present benefit.
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    ResourceManager(ResourceManager&&) = delete;
    ResourceManager& operator=(ResourceManager&&) = delete;

    // Returns the cached Shader for this exact (vertexPath, fragmentPath)
    // pair, compiling+linking it via Shader's own constructor the first
    // time it's requested. Propagates Shader's std::runtime_error on a
    // first-time load failure.
    std::shared_ptr<Shader> getShader(const std::string& vertexPath, const std::string& fragmentPath);

    // Returns the cached Texture for this exact (path, generateMipmaps)
    // pair, loading it via Texture's own constructor the first time it's
    // requested. Propagates Texture's std::runtime_error on a first-time
    // load failure.
    std::shared_ptr<Texture> getTexture(const std::string& path, bool generateMipmaps = true);

    // Returns the cached Model for this exact (path, shader identity) pair,
    // loading it via Assimp (Model's own constructor) the first time it's
    // requested, passing *this along so Model's own texture loads (its
    // per-material diffuse maps, and its checker-texture fallback) also go
    // through this cache instead of loading independently. Propagates
    // Model's std::runtime_error on a first-time load failure.
    std::shared_ptr<Model> getModel(const std::string& path, Shader& shader);

private:
    std::unordered_map<std::string, std::shared_ptr<Shader>> shaders_;
    std::unordered_map<std::string, std::shared_ptr<Texture>> textures_;
    std::unordered_map<std::string, std::shared_ptr<Model>> models_;
};

}  // namespace engine

#endif  // ENGINE_RESOURCE_MANAGER_HPP
