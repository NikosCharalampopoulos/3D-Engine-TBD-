#include "engine/resource_manager.hpp"

#include "engine/log.hpp"
#include "engine/model.hpp"
#include "engine/shader.hpp"
#include "engine/texture.hpp"

namespace engine {

namespace {

std::string shaderKey(const std::string& vertexPath, const std::string& fragmentPath) {
    // '|' can't appear in either path in practice (these are asset file
    // paths, not user-controlled strings), so a plain join can't collide
    // between e.g. vertexPath="a|b", fragmentPath="c" and vertexPath="a",
    // fragmentPath="b|c" the way an ambiguous separator elsewhere might.
    return vertexPath + "|" + fragmentPath;
}

std::string textureKey(const std::string& path, bool generateMipmaps) {
    return path + (generateMipmaps ? "|mip" : "|nomip");
}

std::string modelKey(const std::string& path, const Shader& shader) {
    // See resource_manager.hpp's class comment: the shader's GL program id
    // is part of the key so a Model already built against one shader is
    // never handed back for a request naming a different shader.
    return path + "|shader" + std::to_string(shader.id());
}

}  // namespace

std::shared_ptr<Shader> ResourceManager::getShader(const std::string& vertexPath, const std::string& fragmentPath) {
    const std::string key = shaderKey(vertexPath, fragmentPath);
    const auto it = shaders_.find(key);
    if (it != shaders_.end()) {
        return it->second;
    }

    auto shader = std::make_shared<Shader>(vertexPath, fragmentPath);
    shaders_.emplace(key, shader);
    LOG_INFO("ResourceManager: cached shader (" + vertexPath + ", " + fragmentPath + ")");
    return shader;
}

std::shared_ptr<Texture> ResourceManager::getTexture(const std::string& path, bool generateMipmaps) {
    const std::string key = textureKey(path, generateMipmaps);
    const auto it = textures_.find(key);
    if (it != textures_.end()) {
        return it->second;
    }

    auto texture = std::make_shared<Texture>(path, generateMipmaps);
    textures_.emplace(key, texture);
    LOG_INFO("ResourceManager: cached texture \"" + path + "\"");
    return texture;
}

std::shared_ptr<Model> ResourceManager::getModel(const std::string& path, Shader& shader) {
    const std::string key = modelKey(path, shader);
    const auto it = models_.find(key);
    if (it != models_.end()) {
        return it->second;
    }

    auto model = std::make_shared<Model>(path, shader, *this);
    models_.emplace(key, model);
    LOG_INFO("ResourceManager: cached model \"" + path + "\"");
    return model;
}

}  // namespace engine
