#include "engine/framebuffer.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <stdexcept>
#include <string>

#include "engine/gl_debug.hpp"
#include "engine/log.hpp"

namespace engine {

namespace {

// MSAA HDR framebuffer bug fix: clamps a requested multisample count to what
// this driver actually grants for a GL_RGBA16F multisample color texture
// paired with a multisample depth renderbuffer in the same FBO -- both
// attachments in one FBO must share one identical sample count (a GL FBO-
// completeness rule), so the real cap is the *smaller* of the two
// independent driver limits that could each restrict it:
// GL_MAX_SAMPLES (the general/renderbuffer-side cap) and
// GL_MAX_COLOR_TEXTURE_SAMPLES (the color-texture-side cap, which can be
// lower on some drivers). Logs requested vs. granted either way, the same
// "hint, then verify against the real GL state" discipline Window's own
// GLFW_SAMPLES/GL_SAMPLES check already established -- never silently
// assume the request was honored as-is.
int clampMultisampleCount(int requestedSamples) {
    GLint maxSamples = 0;
    GL_CHECK(glGetIntegerv(GL_MAX_SAMPLES, &maxSamples));
    GLint maxColorTextureSamples = 0;
    GL_CHECK(glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &maxColorTextureSamples));

    const int driverCap = std::max(1, std::min(static_cast<int>(maxSamples), static_cast<int>(maxColorTextureSamples)));
    const int granted = std::max(1, std::min(requestedSamples, driverCap));

    LOG_INFO("Framebuffer: multisample HDR color target requested " + std::to_string(requestedSamples) +
              "x, granted " + std::to_string(granted) + "x (driver GL_MAX_SAMPLES=" + std::to_string(maxSamples) +
              ", GL_MAX_COLOR_TEXTURE_SAMPLES=" + std::to_string(maxColorTextureSamples) + ")");
    return granted;
}

}  // namespace

Framebuffer::Framebuffer(int width, int height, int samples, bool depthAsTexture) : width_(width), height_(height) {
    // samples <= 1 is this class's original single-sample behavior (every
    // pre-existing call site -- brightFramebuffer_/pingpongFramebuffer0_/1_'s
    // bloom targets -- passes no argument here at all, defaulting to 0);
    // samples_ stays 0 in that case (see samples()'s own "0 means
    // single-sample" doc comment), not 1, so callers can tell "not
    // multisampled" apart from "multisampled at 1x" (a degenerate case GL
    // itself doesn't distinguish from single-sample but this class's own API
    // does, for clarity).
    const bool wantsMultisample = samples > 1;
    if (wantsMultisample) {
        samples_ = clampMultisampleCount(samples);
    }

    // Phase 13f: see this class's own constructor doc comment for why this
    // combination is refused rather than silently doing something weaker
    // (e.g. quietly falling back to a renderbuffer) -- nothing in this
    // engine asks for it, and a multisample depth texture would need
    // sampler2DMS handling nothing downstream implements.
    if (wantsMultisample && depthAsTexture) {
        LOG_ERROR("Framebuffer: depthAsTexture is not supported together with multisampling");
        throw std::runtime_error("Framebuffer: depthAsTexture is not supported together with multisampling");
    }

    if (wantsMultisample) {
        // Multisample color attachment: GL_TEXTURE_2D_MULTISAMPLE, not the
        // plain GL_TEXTURE_2D single-sample path below -- see this class's
        // header comment for why (Application::render() draws the whole lit
        // scene + skybox into this target, and MSAA only smooths *those*
        // draws' own edges if this attachment is actually multisample,
        // unlike the pre-fix bug this class's Phase 7b version had). Still
        // GL_RGBA16F, same HDR-safe rationale as the single-sample path.
        // No glTexParameteri calls here (unlike the single-sample path just
        // below): a multisample texture is only ever read via
        // texelFetch/sampler2DMS or resolved via glBlitFramebuffer (see
        // resolveTo()), never filtered/wrapped the way an ordinary sampled
        // texture is -- per the GL spec, filter/wrap parameters simply don't
        // apply to GL_TEXTURE_2D_MULTISAMPLE.
        GL_CHECK(glGenTextures(1, &colorTexture_));
        GL_CHECK(glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, colorTexture_));
        GL_CHECK(glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples_, GL_RGBA16F, width_, height_,
                                           GL_TRUE));
        GL_CHECK(glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0));
    } else {
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
    }

    // Depth attachment: a renderbuffer by default -- nothing needs to
    // *sample* this target's depth the way the main pass samples
    // ShadowMap's depth texture (that's the shadow map's own, separate
    // depth texture), so GL's cheaper write-only renderbuffer attachment is
    // the right tool here, not a second depth texture. The multisample path
    // uses glRenderbufferStorageMultisample with the exact same samples_
    // the color texture above was just allocated with -- GL requires every
    // attachment on one FBO to share one sample count.
    //
    // Phase 13f: depthAsTexture (never combined with wantsMultisample -- see
    // the constructor's own guard above) instead allocates a real
    // GL_TEXTURE_2D depth attachment, same GL_DEPTH_COMPONENT24 storage
    // format, filtering/wrap parameters mirroring ShadowMap's own depth
    // texture (GL_NEAREST -- a depth buffer's own device-space values should
    // never be blended together by linear filtering; GL_CLAMP_TO_EDGE --
    // this engine's fullscreen SSAO passes never sample outside [0,1]
    // texture space either) -- see SSAO's own comment in application.cpp for
    // why this Framebuffer needs a samplable depth this phase.
    if (depthAsTexture) {
        GL_CHECK(glGenTextures(1, &depthTexture_));
        GL_CHECK(glBindTexture(GL_TEXTURE_2D, depthTexture_));
        GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width_, height_, 0, GL_DEPTH_COMPONENT,
                               GL_FLOAT, nullptr));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
        GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
    } else {
        GL_CHECK(glGenRenderbuffers(1, &depthRenderbuffer_));
        GL_CHECK(glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer_));
        if (wantsMultisample) {
            GL_CHECK(
                glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_DEPTH_COMPONENT24, width_, height_));
        } else {
            GL_CHECK(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width_, height_));
        }
        GL_CHECK(glBindRenderbuffer(GL_RENDERBUFFER, 0));
    }

    GL_CHECK(glGenFramebuffers(1, &fbo_));
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, fbo_));
    GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                      wantsMultisample ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, colorTexture_,
                                      0));
    if (depthAsTexture) {
        GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture_, 0));
    } else {
        GL_CHECK(
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRenderbuffer_));
    }

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &fbo_);
        glDeleteTextures(1, &colorTexture_);
        glDeleteRenderbuffers(1, &depthRenderbuffer_);
        glDeleteTextures(1, &depthTexture_);
        fbo_ = 0;
        colorTexture_ = 0;
        depthRenderbuffer_ = 0;
        depthTexture_ = 0;
        LOG_ERROR("Framebuffer: HDR framebuffer incomplete (status 0x" + std::to_string(status) + ")");
        throw std::runtime_error("Framebuffer: HDR framebuffer incomplete");
    }

    LOG_INFO("Framebuffer created: " + std::to_string(width_) + "x" + std::to_string(height_) +
              (wantsMultisample ? (" RGBA16F " + std::to_string(samples_) + "x-multisample color + depth24 target")
                                 : std::string(" RGBA16F color + depth24 target")) +
              (depthAsTexture ? " (depth as sampled texture)" : ""));
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
    if (depthTexture_ != 0) {
        glDeleteTextures(1, &depthTexture_);
    }
}

Framebuffer::Framebuffer(Framebuffer&& other) noexcept
    : fbo_(other.fbo_),
      colorTexture_(other.colorTexture_),
      depthRenderbuffer_(other.depthRenderbuffer_),
      depthTexture_(other.depthTexture_),
      width_(other.width_),
      height_(other.height_),
      samples_(other.samples_) {
    other.fbo_ = 0;
    other.colorTexture_ = 0;
    other.depthRenderbuffer_ = 0;
    other.depthTexture_ = 0;
    other.width_ = 0;
    other.height_ = 0;
    other.samples_ = 0;
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
        if (depthTexture_ != 0) {
            glDeleteTextures(1, &depthTexture_);
        }

        fbo_ = other.fbo_;
        colorTexture_ = other.colorTexture_;
        depthRenderbuffer_ = other.depthRenderbuffer_;
        depthTexture_ = other.depthTexture_;
        width_ = other.width_;
        height_ = other.height_;
        samples_ = other.samples_;

        other.fbo_ = 0;
        other.colorTexture_ = 0;
        other.depthRenderbuffer_ = 0;
        other.depthTexture_ = 0;
        other.width_ = 0;
        other.height_ = 0;
        other.samples_ = 0;
    }
    return *this;
}

void Framebuffer::bindForWriting() const {
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, fbo_));
    GL_CHECK(glViewport(0, 0, width_, height_));
}

void Framebuffer::bindColorTexture(unsigned int unit) const {
    // Precondition (see this method's header comment): never called on a
    // multisample instance anywhere in this engine -- Application resolves
    // a multisample Framebuffer into a single-sample one (resolveTo(),
    // below) before anything ever needs to sample its color as a plain
    // sampler2D.
    GL_CHECK(glActiveTexture(GL_TEXTURE0 + unit));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, colorTexture_));
}

void Framebuffer::bindDepthTexture(unsigned int unit) const {
    // Precondition (see this method's header comment): only meaningful on
    // an instance constructed with depthAsTexture = true, where
    // depthTexture_ is the live handle -- depthRenderbuffer_ (every other
    // instance) is not a texture and was never meant to be sampled.
    GL_CHECK(glActiveTexture(GL_TEXTURE0 + unit));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, depthTexture_));
}

void Framebuffer::resolveTo(const Framebuffer& target) const {
    GL_CHECK(glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_));
    GL_CHECK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.fbo_));
    GL_CHECK(glBlitFramebuffer(0, 0, width_, height_, 0, 0, target.width_, target.height_, GL_COLOR_BUFFER_BIT,
                                 GL_NEAREST));
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

}  // namespace engine
