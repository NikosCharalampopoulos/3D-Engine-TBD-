#include "engine/framebuffer.hpp"

#include <glad/glad.h>

#include <stdexcept>
#include <string>

#include "engine/gl_debug.hpp"
#include "engine/log.hpp"

namespace engine {

Framebuffer::Framebuffer(int width, int height) : width_(width), height_(height) {
    // Color attachment: GL_RGBA16F, a 16-bit-per-channel floating-point
    // format -- unlike Texture's plain GL_RGBA (8-bit normalized, silently
    // clamped to [0,1] on write), this can hold a lit fragment's color
    // whole even when a bright point light pushes it above 1.0, which is
    // the entire point of rendering the scene here instead of straight to
    // the (8-bit, clamped) default framebuffer. `format`/`type` below
    // (GL_RGBA/GL_FLOAT) only describe the (absent -- nullptr) initial
    // upload's layout, not the storage format actually allocated, which is
    // `internalformat` (GL_RGBA16F).
    GL_CHECK(glGenTextures(1, &colorTexture_));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, colorTexture_));
    GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width_, height_, 0, GL_RGBA, GL_FLOAT, nullptr));
    // GL_LINEAR: the post-process pass samples this texture 1:1 (same
    // resolution as the window), so filtering never actually blends
    // neighboring texels in practice, but there's no reason to prefer
    // GL_NEAREST either. GL_CLAMP_TO_EDGE: the fullscreen quad's texCoords
    // never leave [0,1], so wrapping mode is moot, but clamping is the
    // conventional safe default for a target nothing ever tiles.
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));

    // Depth attachment: a renderbuffer, not a texture -- nothing needs to
    // *sample* this target's depth the way the main pass samples
    // ShadowMap's depth texture (that's the shadow map's own, separate
    // depth texture), so GL's cheaper write-only renderbuffer attachment is
    // the right tool here, not a second depth texture.
    GL_CHECK(glGenRenderbuffers(1, &depthRenderbuffer_));
    GL_CHECK(glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer_));
    GL_CHECK(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width_, height_));
    GL_CHECK(glBindRenderbuffer(GL_RENDERBUFFER, 0));

    GL_CHECK(glGenFramebuffers(1, &fbo_));
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, fbo_));
    GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture_, 0));
    GL_CHECK(
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRenderbuffer_));

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &fbo_);
        glDeleteTextures(1, &colorTexture_);
        glDeleteRenderbuffers(1, &depthRenderbuffer_);
        fbo_ = 0;
        colorTexture_ = 0;
        depthRenderbuffer_ = 0;
        LOG_ERROR("Framebuffer: HDR framebuffer incomplete (status 0x" + std::to_string(status) + ")");
        throw std::runtime_error("Framebuffer: HDR framebuffer incomplete");
    }

    LOG_INFO("Framebuffer created: " + std::to_string(width_) + "x" + std::to_string(height_) +
              " RGBA16F color + depth24 target");
}

Framebuffer::~Framebuffer() {
    if (fbo_ != 0) {
        glDeleteFramebuffers(1, &fbo_);
    }
    if (colorTexture_ != 0) {
        glDeleteTextures(1, &colorTexture_);
    }
    if (depthRenderbuffer_ != 0) {
        glDeleteRenderbuffers(1, &depthRenderbuffer_);
    }
}

Framebuffer::Framebuffer(Framebuffer&& other) noexcept
    : fbo_(other.fbo_),
      colorTexture_(other.colorTexture_),
      depthRenderbuffer_(other.depthRenderbuffer_),
      width_(other.width_),
      height_(other.height_) {
    other.fbo_ = 0;
    other.colorTexture_ = 0;
    other.depthRenderbuffer_ = 0;
    other.width_ = 0;
    other.height_ = 0;
}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept {
    if (this != &other) {
        if (fbo_ != 0) {
            glDeleteFramebuffers(1, &fbo_);
        }
        if (colorTexture_ != 0) {
            glDeleteTextures(1, &colorTexture_);
        }
        if (depthRenderbuffer_ != 0) {
            glDeleteRenderbuffers(1, &depthRenderbuffer_);
        }

        fbo_ = other.fbo_;
        colorTexture_ = other.colorTexture_;
        depthRenderbuffer_ = other.depthRenderbuffer_;
        width_ = other.width_;
        height_ = other.height_;

        other.fbo_ = 0;
        other.colorTexture_ = 0;
        other.depthRenderbuffer_ = 0;
        other.width_ = 0;
        other.height_ = 0;
    }
    return *this;
}

void Framebuffer::bindForWriting() const {
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, fbo_));
    GL_CHECK(glViewport(0, 0, width_, height_));
}

void Framebuffer::bindColorTexture(unsigned int unit) const {
    GL_CHECK(glActiveTexture(GL_TEXTURE0 + unit));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, colorTexture_));
}

}  // namespace engine
