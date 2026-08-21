#include "engine/ibl_probe.hpp"

#include <glad/glad.h>

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

// Not defined in this project's hand-pruned external/glad/include/glad/
// glad.h (only the enums/entry points earlier phases actually needed -- see
// allocateEmptyCubemap()'s own comment on that same constraint for
// GL_RGBA16F/GL_RGB16F): these are the two standard, numerically-stable
// texture-parameter enum values (unchanged since OpenGL 1.2/EXT_texture_object),
// passed as plain GLenum arguments to glTexParameteri(), which this pruned
// glad build already loads -- no new GL entry point is needed to use them.
constexpr GLenum kGlTextureBaseLevel = 0x813C;
constexpr GLenum kGlTextureMaxLevel = 0x813D;

// The 6 face view matrices for rendering into a cubemap from its own center,
// paired 1:1 with Skybox's kCubeMapTargets order (+X, -X, +Y, -Y, +Z, -Z) --
// the same standard "cubemap capture views" table LearnOpenGL's IBL articles
// use, derived from a 90-degree-FOV lookAt() aimed down each face's own axis
// with an up vector chosen so the resulting image lands right-side-up on
// that face (the +Y/-Y faces need a forward-pointing up vector instead of
// the usual world-up, since world-up is parallel to the view direction on
// exactly those two faces).
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

// Same face-target order as Skybox's kCubeMapTargets -- see that table's own
// comment on why getting this order wrong silently produces a
// valid-looking-but-scrambled cubemap.
constexpr std::array<GLenum, 6> kCubeMapTargets = {
    GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X, GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
    GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
};

// Allocates an empty (uninitialized) floating-point cubemap of `size` per
// face, with the given min filter (GL_LINEAR for the irradiance map, which
// has no mip chain; GL_LINEAR_MIPMAP_LINEAR for the prefiltered map, which
// does). Uses GL_RGBA16F (the same HDR-capable format engine::Framebuffer's
// own color attachment uses) rather than a 3-channel RGB16F: this project's
// vendored external/glad/include/glad/glad.h is a hand-pruned GL 3.3 core
// subset (only the enums/entry points earlier phases actually needed), and
// GL_RGBA16F is the one floating-point sized-internal-format token it
// already defines -- adding GL_RGB16F would mean hand-patching the vendored
// generated header for a one-channel storage saving that doesn't matter at
// these small resolutions. The unused alpha channel is simply never read by
// pbr.frag's `.rgb` swizzles below.
unsigned int allocateEmptyCubemap(int size, GLint minFilter, bool withMips) {
    unsigned int texture = 0;
    GL_CHECK(glGenTextures(1, &texture));
    GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, texture));

    const int mipLevels = withMips ? IBLProbe::kPrefilterMipLevels : 1;
    for (int mip = 0; mip < mipLevels; ++mip) {
        const int mipSize = size >> mip;
        for (GLenum target : kCubeMapTargets) {
            GL_CHECK(glTexImage2D(target, mip, GL_RGBA16F, mipSize, mipSize, 0, GL_RGBA, GL_FLOAT, nullptr));
        }
    }

    GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, minFilter));
    GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE));
    // GL_TEXTURE_BASE_LEVEL/GL_TEXTURE_MAX_LEVEL explicitly bounded to the
    // levels actually uploaded above (default GL_TEXTURE_MAX_LEVEL is 1000):
    // this prefiltered map deliberately stops at kPrefilterMipLevels (5)
    // rather than a full pyramid all the way down to 1x1 (see
    // IBLProbe::kPrefilterMipLevels's own comment -- roughness is already
    // pinned to exactly 1.0 well before a smaller mip would add anything),
    // and leaving GL_TEXTURE_MAX_LEVEL at its default of 1000 makes at least
    // this project's Mesa llvmpipe driver treat every mip level beyond level
    // 0 as attachment-incomplete (GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT) when
    // rendering into it face-by-face below, since it expects (but can't
    // find) levels the texture was never meant to have.
    if (withMips) {
        GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP, kGlTextureBaseLevel, 0));
        GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP, kGlTextureMaxLevel, IBLProbe::kPrefilterMipLevels - 1));
    }

    GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));
    return texture;
}

void deleteIfNonZero(unsigned int texture) {
    if (texture != 0) {
        glDeleteTextures(1, &texture);
    }
}

// Formats a GLenum status as hex (e.g. "0x8CD6") -- matches
// gl_debug.hpp's checkGlErrors() formatting convention (std::to_string()
// alone prints decimal, which would silently mislabel this exact GLenum
// range as something other than the real, well-known hex constant a reader
// could look up).
std::string toHex(GLenum value) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%04X", value);
    return std::string(buf);
}

}  // namespace

IBLProbe::IBLProbe(unsigned int environmentCubemap, Shader& irradianceShader, Shader& prefilterShader,
                    Shader& brdfShader) {
    // These three precompute passes render a cube from the inside (or a
    // fullscreen quad) with no interest in depth testing at all -- every
    // fragment of every draw here is meant to write unconditionally.
    // Unconditionally disabled here and unconditionally re-enabled at the end
    // (rather than saved/restored via glIsEnabled(), which this project's
    // hand-pruned vendored glad build doesn't expose -- see
    // allocateEmptyCubemap()'s own comment on that same minimal-glad
    // constraint): GL_DEPTH_TEST is enabled exactly once, for the whole rest
    // of this engine's run, in Application's own constructor body (see
    // application.cpp) and never disabled again outside this class, so
    // "enabled" is this engine's one real invariant to restore, not a value
    // worth querying.
    GL_CHECK(glDisable(GL_DEPTH_TEST));

    // One temporary FBO, reused (with a differently-sized/targeted
    // attachment each time) across all three precompute passes below --
    // unlike Framebuffer/ShadowMap, nothing outside this constructor ever
    // needs to render into these targets again, so there's no reason to keep
    // this FBO (or a wrapper class around it) alive past this constructor.
    unsigned int captureFbo = 0;
    GL_CHECK(glGenFramebuffers(1, &captureFbo));
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, captureFbo));

    const std::array<glm::mat4, 6> captureViews = cubeCaptureViews();
    const glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

    // A plain unit cube (only its position attribute is read by
    // cubemap_capture.vert, same as Skybox's own cubeMesh_) and a screen-space
    // quad (for the BRDF LUT pass, paired with postprocess.vert -- see that
    // shader's own header comment on why it needs no model/view/projection at
    // all) -- built locally rather than borrowed from Application/Skybox
    // since both are cheap, and nothing here has a reference to either of
    // those classes' own mesh members.
    Mesh cube = makeCube(1.0f);
    Mesh quad = makeFullscreenQuad();

    try {
        // --- 1. Diffuse irradiance cubemap ---
        irradianceMap_ = allocateEmptyCubemap(kIrradianceMapSize, GL_LINEAR, /*withMips=*/false);

        irradianceShader.use();
        GL_CHECK(glActiveTexture(GL_TEXTURE0));
        GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, environmentCubemap));
        irradianceShader.setInt("uEnvironmentMap", 0);
        irradianceShader.setMat4("uProjection", captureProjection);

        GL_CHECK(glViewport(0, 0, kIrradianceMapSize, kIrradianceMapSize));
        cube.bind();
        for (int face = 0; face < 6; ++face) {
            irradianceShader.setMat4("uView", captureViews[static_cast<std::size_t>(face)]);
            GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, kCubeMapTargets[face],
                                             irradianceMap_, 0));
            const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                LOG_ERROR("IBLProbe: irradiance capture FBO incomplete (status " + toHex(status) +
                          ", face " + std::to_string(face) + ")");
                throw std::runtime_error("IBLProbe: irradiance capture FBO incomplete");
            }
            GL_CHECK(glClear(GL_COLOR_BUFFER_BIT));
            cube.draw();
        }

        // --- 2. Prefiltered specular cubemap (mipmapped) ---
        prefilterMap_ =
            allocateEmptyCubemap(kPrefilterBaseSize, GL_LINEAR_MIPMAP_LINEAR, /*withMips=*/true);

        prefilterShader.use();
        GL_CHECK(glActiveTexture(GL_TEXTURE0));
        GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, environmentCubemap));
        prefilterShader.setInt("uEnvironmentMap", 0);
        prefilterShader.setMat4("uProjection", captureProjection);

        cube.bind();
        for (int mip = 0; mip < kPrefilterMipLevels; ++mip) {
            const int mipSize = kPrefilterBaseSize >> mip;
            const float roughness = kPrefilterMipLevels > 1
                                         ? static_cast<float>(mip) / static_cast<float>(kPrefilterMipLevels - 1)
                                         : 0.0f;
            prefilterShader.setFloat("uRoughness", roughness);

            GL_CHECK(glViewport(0, 0, mipSize, mipSize));
            for (int face = 0; face < 6; ++face) {
                prefilterShader.setMat4("uView", captureViews[static_cast<std::size_t>(face)]);
                GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, kCubeMapTargets[face],
                                                 prefilterMap_, mip));
                const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                if (status != GL_FRAMEBUFFER_COMPLETE) {
                    LOG_ERROR("IBLProbe: prefilter capture FBO incomplete (status " + toHex(status) + ", mip " +
                              std::to_string(mip) + ", face " + std::to_string(face) + ")");
                    throw std::runtime_error("IBLProbe: prefilter capture FBO incomplete");
                }
                GL_CHECK(glClear(GL_COLOR_BUFFER_BIT));
                cube.draw();
            }
        }

        // --- 3. BRDF integration LUT (plain 2D, one fullscreen pass) ---
        // GL_RGBA16F/GL_RGBA here for the same "GL_RG16F isn't in this
        // project's pruned glad build" reason allocateEmptyCubemap() uses
        // GL_RGBA16F instead of GL_RGB16F -- brdf_lut.frag's FragColor
        // already writes its two meaningful channels as
        // vec4(scale, bias, 0.0, 1.0), and pbr.frag only ever reads `.rg`
        // back out, so the two extra stored channels are simply unused.
        GL_CHECK(glGenTextures(1, &brdfLUT_));
        GL_CHECK(glBindTexture(GL_TEXTURE_2D, brdfLUT_));
        GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, kBrdfLutSize, kBrdfLutSize, 0, GL_RGBA, GL_FLOAT,
                               nullptr));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

        GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUT_, 0));
        const GLenum brdfStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (brdfStatus != GL_FRAMEBUFFER_COMPLETE) {
            LOG_ERROR("IBLProbe: BRDF LUT capture FBO incomplete (status " + toHex(brdfStatus) + ")");
            throw std::runtime_error("IBLProbe: BRDF LUT capture FBO incomplete");
        }

        GL_CHECK(glViewport(0, 0, kBrdfLutSize, kBrdfLutSize));
        GL_CHECK(glClear(GL_COLOR_BUFFER_BIT));
        brdfShader.use();
        quad.bind();
        quad.draw();
    } catch (...) {
        deleteIfNonZero(irradianceMap_);
        deleteIfNonZero(prefilterMap_);
        deleteIfNonZero(brdfLUT_);
        glDeleteFramebuffers(1, &captureFbo);
        // Default framebuffer + depth test re-enabled even on failure --
        // matching this engine's usual "restore state a caller can rely on
        // even when a throwing constructor unwinds" convention (see e.g.
        // Shader::Shader()'s own cleanup-before-throw). render()'s own
        // top-of-frame glViewport() call (see application.cpp) fixes up the
        // viewport itself before the first real frame, so nothing here needs
        // to restore that.
        GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
        GL_CHECK(glEnable(GL_DEPTH_TEST));
        throw;
    }

    glDeleteFramebuffers(1, &captureFbo);
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    GL_CHECK(glEnable(GL_DEPTH_TEST));

    LOG_INFO("IBLProbe created: " + std::to_string(kIrradianceMapSize) + "x" +
              std::to_string(kIrradianceMapSize) + " irradiance cubemap, " +
              std::to_string(kPrefilterBaseSize) + "x" + std::to_string(kPrefilterBaseSize) + " prefiltered "
              "specular cubemap (" + std::to_string(kPrefilterMipLevels) + " mips), " +
              std::to_string(kBrdfLutSize) + "x" + std::to_string(kBrdfLutSize) + " BRDF LUT");
}

IBLProbe::~IBLProbe() {
    deleteIfNonZero(irradianceMap_);
    deleteIfNonZero(prefilterMap_);
    deleteIfNonZero(brdfLUT_);
}

IBLProbe::IBLProbe(IBLProbe&& other) noexcept
    : irradianceMap_(other.irradianceMap_), prefilterMap_(other.prefilterMap_), brdfLUT_(other.brdfLUT_) {
    other.irradianceMap_ = 0;
    other.prefilterMap_ = 0;
    other.brdfLUT_ = 0;
}

IBLProbe& IBLProbe::operator=(IBLProbe&& other) noexcept {
    if (this != &other) {
        deleteIfNonZero(irradianceMap_);
        deleteIfNonZero(prefilterMap_);
        deleteIfNonZero(brdfLUT_);

        irradianceMap_ = other.irradianceMap_;
        prefilterMap_ = other.prefilterMap_;
        brdfLUT_ = other.brdfLUT_;

        other.irradianceMap_ = 0;
        other.prefilterMap_ = 0;
        other.brdfLUT_ = 0;
    }
    return *this;
}

void IBLProbe::bindForSampling(unsigned int irradianceUnit, unsigned int prefilterUnit,
                                unsigned int brdfUnit) const {
    GL_CHECK(glActiveTexture(GL_TEXTURE0 + irradianceUnit));
    GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap_));
    GL_CHECK(glActiveTexture(GL_TEXTURE0 + prefilterUnit));
    GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap_));
    GL_CHECK(glActiveTexture(GL_TEXTURE0 + brdfUnit));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, brdfLUT_));
}

}  // namespace engine
