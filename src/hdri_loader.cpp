#include "engine/hdri_loader.hpp"

#include <glad/glad.h>

#include <stb_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>

#include "engine/gl_debug.hpp"
#include "engine/log.hpp"
#include "engine/mesh.hpp"
#include "engine/shader.hpp"

namespace engine {

namespace {

// Same face-target order every other cubemap-building class in this engine
// uses (Skybox::kCubeMapTargets, IBLProbe's own kCubeMapTargets) --
// deliberately re-duplicated here rather than shared across translation
// units, matching this codebase's established "each small GL wrapper stays
// self-contained" style (see skybox.cpp's own glFormatForChannels()
// comment, which gives the same rationale for its own duplication).
constexpr std::array<GLenum, 6> kCubeMapTargets = {
    GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X, GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
    GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
};

// Identical to IBLProbe's own cubeCaptureViews() (see ibl_probe.cpp's
// comment for the full derivation of the up-vector choices) -- duplicated
// for the same "self-contained TU" reason as kCubeMapTargets above.
std::array<glm::mat4, 6> cubeCaptureViews() {
    constexpr glm::vec3 origin(0.0f);
    return {
        glm::lookAt(origin, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(origin, glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(origin, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        glm::lookAt(origin, glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)),
        glm::lookAt(origin, glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(origin, glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
    };
}

std::string toHex(GLenum value) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%04X", value);
    return std::string(buf);
}

void deleteIfNonZero(unsigned int texture) {
    if (texture != 0) {
        glDeleteTextures(1, &texture);
    }
}

}  // namespace

unsigned int loadHdrEquirectangularAsCubemap(const std::string& hdrPath, Shader& conversionShader, int faceSize) {
    // Same "GL samples (0,0) at the bottom-left, image files store rows
    // top-to-bottom" flip engine::Texture always applies (see texture.cpp's
    // own comment) -- required here too so this loader's row/column ->
    // direction convention lines up with tools/generate_hdri.py's own
    // authoring convention and equirect_to_cubemap.frag's UV formula (see
    // that shader's header comment for the full derivation). Explicitly set
    // (not assumed already true) since, unlike engine::Texture, this
    // function can run before any Texture has been constructed this process
    // (Skybox/IBLProbe are built early in Application's own member-
    // initializer list, ahead of groundMaterial_'s textures) -- there is no
    // guarantee stb_image's global flip flag has already been set to this
    // engine's steady-state convention by the time this runs.
    stbi_set_flip_vertically_on_load(true);

    int width = 0;
    int height = 0;
    int channels = 0;
    // Radiance .hdr is always a 3-channel (RGB) format (see stb_image.h's
    // own stbi__hdr_load(), which hardcodes *comp = 3) -- desired_channels =
    // 3 just makes that explicit rather than relying on the file's own
    // reported count.
    float* pixels = stbi_loadf(hdrPath.c_str(), &width, &height, &channels, 3);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        LOG_ERROR("loadHdrEquirectangularAsCubemap: failed to load \"" + hdrPath +
                   "\": " + (reason != nullptr ? std::string(reason) : std::string("unknown error")));
        throw std::runtime_error("loadHdrEquirectangularAsCubemap: failed to load HDR image: " + hdrPath);
    }

    LOG_INFO("HDRI loaded: " + hdrPath + " (" + std::to_string(width) + "x" + std::to_string(height) +
              ", HDR float RGB)");

    // The equirectangular source, uploaded as an ordinary floating-point 2D
    // texture (GL_RGBA16F -- see ibl_probe.cpp's own comment on why this
    // project's hand-pruned vendored glad build has no GL_RGB16F token to
    // reach for instead; the unused alpha channel is simply never read
    // below). GL_REPEAT on S (longitude wraps all the way around -- see
    // equirect_to_cubemap.frag's u formula) but GL_CLAMP_TO_EDGE on T
    // (latitude does not wrap; sampling exactly at/just past a pole should
    // hold the polar row's own color, not wrap to the opposite pole).
    unsigned int equirectTexture = 0;
    GL_CHECK(glGenTextures(1, &equirectTexture));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, equirectTexture));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    // Tightly-packed float data has no alignment-padding concerns the way
    // 8-bit rows can (see Texture::Texture()'s own GL_UNPACK_ALIGNMENT
    // comment) -- 4-byte floats are always 4-byte aligned regardless of
    // width/channel count, so the default alignment (4) is already correct
    // here; no glPixelStorei() call needed.
    GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGB, GL_FLOAT, pixels));
    stbi_image_free(pixels);

    // The destination cubemap -- GL_RGBA16F, no mip chain (see this
    // function's own header comment: like the old procedural skybox, this
    // is a background/IBL source sampled at essentially one fixed
    // "distance", never minified enough for mipmapping to matter).
    unsigned int cubemap = 0;
    GL_CHECK(glGenTextures(1, &cubemap));
    GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap));
    for (GLenum target : kCubeMapTargets) {
        GL_CHECK(glTexImage2D(target, 0, GL_RGBA16F, faceSize, faceSize, 0, GL_RGBA, GL_FLOAT, nullptr));
    }
    GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE));

    // One temporary FBO (same "built, used, torn down within this one call"
    // pattern as IBLProbe's constructor's own captureFbo) rendering each of
    // the 6 faces in turn.
    unsigned int captureFbo = 0;
    GL_CHECK(glGenFramebuffers(1, &captureFbo));
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, captureFbo));

    // Depth testing plays no role in this offline conversion pass -- same
    // "unconditionally disabled here, unconditionally restored after" choice
    // IBLProbe's constructor makes, for the same reason (see that
    // constructor's own comment on why GL_DEPTH_TEST is this engine's one
    // real invariant to restore rather than a value worth querying).
    GL_CHECK(glDisable(GL_DEPTH_TEST));

    Mesh cube = makeCube(1.0f);
    const std::array<glm::mat4, 6> captureViews = cubeCaptureViews();
    const glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

    try {
        conversionShader.use();
        GL_CHECK(glActiveTexture(GL_TEXTURE0));
        GL_CHECK(glBindTexture(GL_TEXTURE_2D, equirectTexture));
        conversionShader.setInt("uEquirectangularMap", 0);
        conversionShader.setMat4("uProjection", captureProjection);

        GL_CHECK(glViewport(0, 0, faceSize, faceSize));
        cube.bind();
        for (int face = 0; face < 6; ++face) {
            conversionShader.setMat4("uView", captureViews[static_cast<std::size_t>(face)]);
            GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, kCubeMapTargets[face], cubemap, 0));
            const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                LOG_ERROR("loadHdrEquirectangularAsCubemap: capture FBO incomplete (status " + toHex(status) +
                          ", face " + std::to_string(face) + ")");
                throw std::runtime_error("loadHdrEquirectangularAsCubemap: capture FBO incomplete");
            }
            GL_CHECK(glClear(GL_COLOR_BUFFER_BIT));
            cube.draw();
        }
    } catch (...) {
        deleteIfNonZero(equirectTexture);
        deleteIfNonZero(cubemap);
        glDeleteFramebuffers(1, &captureFbo);
        GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
        GL_CHECK(glEnable(GL_DEPTH_TEST));
        throw;
    }

    deleteIfNonZero(equirectTexture);
    glDeleteFramebuffers(1, &captureFbo);
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    GL_CHECK(glEnable(GL_DEPTH_TEST));
    GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));

    LOG_INFO("HDRI converted to cubemap: " + std::to_string(faceSize) + "x" + std::to_string(faceSize) +
              " per face, GL_RGBA16F");

    return cubemap;
}

}  // namespace engine
