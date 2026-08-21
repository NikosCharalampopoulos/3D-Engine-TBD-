#include "engine/skybox.hpp"

#include <glad/glad.h>

#include <stb_image.h>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

#include "engine/gl_debug.hpp"
#include "engine/log.hpp"
#include "engine/shader.hpp"

namespace engine {

namespace {

// Zips 1:1 against the facePaths order documented in skybox.hpp: index i's
// path is uploaded to kCubeMapTargets[i]. Getting this order wrong (e.g.
// swapping two entries) silently renders a valid-looking but wrong-face-in-
// the-wrong-place cubemap -- everything loads, nothing errors, it just
// looks like a jumbled sky -- so this table and the header comment above it
// must be kept in lockstep by hand.
constexpr std::array<GLenum, 6> kCubeMapTargets = {
    GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X, GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
    GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
};

// Mirrors texture.cpp's glFormatForChannels() -- duplicated rather than
// shared across translation units, matching this codebase's existing
// preference for each small GL wrapper class staying self-contained (e.g.
// Shader/Mesh/Texture/ShadowMap each spell out their own near-identical
// move-ctor bodies rather than sharing a base class).
GLenum glFormatForChannels(int channels, const std::string& path) {
    switch (channels) {
        case 1:
            return GL_RED;
        case 2:
            return GL_RG;
        case 3:
            return GL_RGB;
        case 4:
            return GL_RGBA;
        default:
            throw std::runtime_error("Skybox: unsupported channel count (" + std::to_string(channels) +
                                      ") in " + path);
    }
}

}  // namespace

Skybox::Skybox(const std::array<std::string, 6>& facePaths) : cubeMesh_(makeCube(1.0f)) {
    GL_CHECK(glGenTextures(1, &textureId_));
    GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, textureId_));

    // Unlike Texture's 2D image loads, cubemap faces must NOT be flipped on
    // load: GL's cubemap sampling convention already expects each face
    // stored in its own "as authored" row order relative to its
    // neighbors -- flipping here would rotate/mirror every face independent
    // of the others and break the face-boundary color matching
    // assets/textures/skybox's own faces were generated to have (see
    // README.md's Phase 7b notes). stbi_set_flip_vertically_on_load() is
    // process-global state, not per-call, so this explicitly sets it to
    // false around these 6 loads and restores it to Texture's own
    // convention (true) afterward, rather than assuming whatever the last
    // caller left it as.
    stbi_set_flip_vertically_on_load(false);

    for (std::size_t i = 0; i < facePaths.size(); ++i) {
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* pixels = stbi_load(facePaths[i].c_str(), &width, &height, &channels, 0);
        if (pixels == nullptr) {
            const char* reason = stbi_failure_reason();
            stbi_set_flip_vertically_on_load(true);
            glDeleteTextures(1, &textureId_);
            textureId_ = 0;
            LOG_ERROR("Skybox: failed to load \"" + facePaths[i] +
                       "\": " + (reason != nullptr ? std::string(reason) : std::string("unknown error")));
            throw std::runtime_error("Skybox: failed to load cubemap face: " + facePaths[i]);
        }

        GLenum format = GL_RGB;
        try {
            format = glFormatForChannels(channels, facePaths[i]);
        } catch (...) {
            stbi_image_free(pixels);
            stbi_set_flip_vertically_on_load(true);
            glDeleteTextures(1, &textureId_);
            textureId_ = 0;
            throw;
        }

        // Same GL_UNPACK_ALIGNMENT fix as Texture::Texture() -- see its
        // comment for why the default (4) corrupts any tightly-packed
        // buffer whose row length isn't already a multiple of 4 bytes.
        GL_CHECK(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
        GL_CHECK(glTexImage2D(kCubeMapTargets[i], 0, static_cast<GLint>(format), width, height, 0, format,
                               GL_UNSIGNED_BYTE, pixels));
        GL_CHECK(glPixelStorei(GL_UNPACK_ALIGNMENT, 4));
        stbi_image_free(pixels);

        LOG_INFO("Skybox face loaded: " + facePaths[i] + " (" + std::to_string(width) + "x" +
                  std::to_string(height) + ", " + std::to_string(channels) + " channel(s)) -> target " +
                  std::to_string(static_cast<int>(kCubeMapTargets[i]) - GL_TEXTURE_CUBE_MAP_POSITIVE_X));
    }

    stbi_set_flip_vertically_on_load(true);

    // GL_LINEAR (not mipmapped -- a skybox is always viewed at essentially
    // one fixed "distance", so there's no minification benefit worth the
    // extra generateMipmap call/storage). GL_CLAMP_TO_EDGE on all three
    // axes (S/T/R -- a cubemap sample also has an R wrap mode, unlike a 2D
    // texture) so sampling exactly on a face seam never wraps to an
    // unrelated face.
    GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE));

    GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));

    LOG_INFO("Skybox created (6-face cubemap)");
}

Skybox::~Skybox() {
    if (textureId_ != 0) {
        glDeleteTextures(1, &textureId_);
    }
}

Skybox::Skybox(Skybox&& other) noexcept : textureId_(other.textureId_), cubeMesh_(std::move(other.cubeMesh_)) {
    other.textureId_ = 0;
}

Skybox& Skybox::operator=(Skybox&& other) noexcept {
    if (this != &other) {
        if (textureId_ != 0) {
            glDeleteTextures(1, &textureId_);
        }
        textureId_ = other.textureId_;
        cubeMesh_ = std::move(other.cubeMesh_);
        other.textureId_ = 0;
    }
    return *this;
}

void Skybox::draw(Shader& shader, const glm::mat4& view, const glm::mat4& projection,
                   unsigned int textureUnit) const {
    // Strips translation from the view matrix (mat3 truncation, re-embedded
    // in a mat4) so the skybox cube stays centered on the camera and never
    // appears to translate as the camera moves -- only the camera's
    // rotation should change which part of the sky is visible.
    const glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(view));

    shader.use();
    shader.setMat4("uView", viewNoTranslation);
    shader.setMat4("uProjection", projection);

    GL_CHECK(glActiveTexture(GL_TEXTURE0 + textureUnit));
    GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, textureId_));
    shader.setInt("uSkybox", static_cast<int>(textureUnit));

    // Depth trick (see skybox.vert): the vertex shader forces
    // gl_Position.z == gl_Position.w, so every skybox fragment's
    // post-perspective-divide depth is exactly 1.0 (the far plane)
    // regardless of the cube's actual size/distance. GL_LEQUAL (rather than
    // this engine's usual GL_LESS default) is required for those far-plane
    // fragments to still pass the depth test against a freshly-cleared
    // depth buffer, which is *also* 1.0 everywhere nothing has been drawn
    // yet this frame -- GL_LESS would reject every skybox pixel outright,
    // since 1.0 is never strictly less than 1.0. Restored to GL_LESS
    // immediately after, so it doesn't leak into next frame's shadow/main
    // passes, both of which rely on GL_LESS's ordinary nearer-wins
    // behavior.
    GL_CHECK(glDepthFunc(GL_LEQUAL));
    cubeMesh_.bind();
    cubeMesh_.draw();
    GL_CHECK(glDepthFunc(GL_LESS));
}

}  // namespace engine
