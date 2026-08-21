#include "engine/shadow_map.hpp"

#include <glad/glad.h>

#include <stdexcept>
#include <string>

#include "engine/gl_debug.hpp"
#include "engine/log.hpp"

namespace engine {

ShadowMap::ShadowMap(int width, int height) : width_(width), height_(height) {
    GL_CHECK(glGenTextures(1, &depthTexture_));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, depthTexture_));
    // Depth-only, single mip level, no data yet (nullptr) -- populated by
    // rendering the light's-eye-view depth pass into it every frame, not by
    // an upload from CPU memory.
    GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width_, height_, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
                           nullptr));
    // GL_NEAREST (not GL_LINEAR): the fragment shader compares raw depth
    // values directly (see basic.frag's ShadowCalculation) -- averaging
    // neighboring depth samples via linear filtering before that comparison
    // would blend genuinely different surfaces' depths together, which is
    // not the same thing as (and not as correct as) proper percentage-closer
    // filtering. A fixed/slope-scaled bias on a hard-edged lookup is this
    // phase's chosen tradeoff; PCF is a plausible later improvement.
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    // CLAMP_TO_EDGE rather than REPEAT: sampling outside the light's own
    // frustum should never wrap around to an unrelated part of the depth
    // map. basic.frag's ShadowCalculation additionally guards against
    // out-of-[0,1] light-clip coordinates explicitly (treating them as "not
    // in shadow") rather than relying on clamped-edge texel values alone.
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));

    GL_CHECK(glGenFramebuffers(1, &fbo_));
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, fbo_));
    GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture_, 0));
    // No color attachment at all -- this FBO exists purely to write depth.
    // Some drivers only report GL_FRAMEBUFFER_COMPLETE for a color-less FBO
    // once both the draw and read buffers are explicitly told there's no
    // color buffer to touch (the default, GL_COLOR_ATTACHMENT0, would
    // otherwise be expected but absent).
    GL_CHECK(glDrawBuffer(GL_NONE));
    GL_CHECK(glReadBuffer(GL_NONE));

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &fbo_);
        glDeleteTextures(1, &depthTexture_);
        fbo_ = 0;
        depthTexture_ = 0;
        LOG_ERROR("ShadowMap: depth framebuffer incomplete (status 0x" + std::to_string(status) + ")");
        throw std::runtime_error("ShadowMap: depth framebuffer incomplete");
    }

    LOG_INFO("ShadowMap created: " + std::to_string(width_) + "x" + std::to_string(height_) + " depth texture");
}

ShadowMap::~ShadowMap() {
    if (fbo_ != 0) {
        glDeleteFramebuffers(1, &fbo_);
    }
    if (depthTexture_ != 0) {
        glDeleteTextures(1, &depthTexture_);
    }
}

ShadowMap::ShadowMap(ShadowMap&& other) noexcept
    : fbo_(other.fbo_), depthTexture_(other.depthTexture_), width_(other.width_), height_(other.height_) {
    other.fbo_ = 0;
    other.depthTexture_ = 0;
    other.width_ = 0;
    other.height_ = 0;
}

ShadowMap& ShadowMap::operator=(ShadowMap&& other) noexcept {
    if (this != &other) {
        if (fbo_ != 0) {
            glDeleteFramebuffers(1, &fbo_);
        }
        if (depthTexture_ != 0) {
            glDeleteTextures(1, &depthTexture_);
        }

        fbo_ = other.fbo_;
        depthTexture_ = other.depthTexture_;
        width_ = other.width_;
        height_ = other.height_;

        other.fbo_ = 0;
        other.depthTexture_ = 0;
        other.width_ = 0;
        other.height_ = 0;
    }
    return *this;
}

void ShadowMap::bindForWriting() const {
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, fbo_));
    GL_CHECK(glViewport(0, 0, width_, height_));
}

void ShadowMap::bindForReading(unsigned int unit) const {
    GL_CHECK(glActiveTexture(GL_TEXTURE0 + unit));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, depthTexture_));
}

}  // namespace engine
