#ifndef ENGINE_FRAMEBUFFER_HPP
#define ENGINE_FRAMEBUFFER_HPP

// Phase 7b: a small reusable "render target" -- an FBO with one
// floating-point color attachment (GL_RGBA16F, wide enough to hold
// pre-tonemap HDR values above 1.0 without clipping -- see
// Application::render()'s bumped-above-1.0 point light) plus a depth
// renderbuffer (so whatever's rendered into it still gets real depth
// testing). Used this phase to render the whole lit scene off-screen
// before a single fullscreen tonemap/gamma pass resolves it to the window
// (see assets/shaders/postprocess.vert/.frag) -- deliberately stops there
// rather than growing into a general multi-pass render graph: a future
// bloom pass could reuse this same class for its own downsample/blur
// targets, but that pass itself isn't built here (see the phase brief).
//
// A renderbuffer (not a texture) backs the depth attachment, unlike
// ShadowMap: nothing needs to *sample* this target's depth buffer the way
// the main pass samples ShadowMap's depth texture, so a renderbuffer (GL's
// cheaper "write-only" attachment type) is the right tool, not a texture.
//
// MSAA HDR framebuffer bug fix (post-Phase-13c): Window has requested a
// multisampled *default* framebuffer since Phase 6, but every phase from 7b
// onward actually draws the real 3D scene (Blinn-Phong entities_/ground,
// the PBR sphere grid, the skybox) into *this* class's single-sample FBO
// instead -- the default framebuffer only ever receives the final flat
// tonemap/bloom-composite fullscreen quad, which has no geometric edges for
// MSAA to smooth. So the window's MSAA setting was never actually
// antialiasing any real geometry. Fixed here by giving this class an
// optional `samples` constructor parameter (0, the default, is the
// original single-sample behavior every existing call site -- brightFramebuffer_/
// pingpongFramebuffer0_/1_'s bloom targets -- keeps unchanged): a value > 1
// allocates a GL_TEXTURE_2D_MULTISAMPLE color attachment
// (glTexImage2DMultisample) and a multisample GL_RENDERBUFFER depth
// attachment (glRenderbufferStorageMultisample) instead of their
// single-sample equivalents, clamped to what this driver actually grants
// (see framebuffer.cpp) rather than assuming the requested count is
// honored, the same "hint, then verify" discipline Window's own GLFW_SAMPLES
// check already established. A multisample color texture can't be sampled
// via a plain `sampler2D` the way this class's single-sample texture is
// (that needs `sampler2DMS` + manual per-sample fetches in the shader) --
// resolveTo() below is the standard alternative: an explicit
// glBlitFramebuffer resolve into a second, single-sample Framebuffer, which
// is what Application now does once per frame (into a new
// hdrResolveFramebuffer_) right after the scene+skybox color pass finishes,
// before bloom extraction/final tonemapping read the (now-resolved,
// ordinary sampler2D-compatible) result -- see Application::render().
//
// Move-only, same rationale/pattern as ShadowMap: the FBO, color texture,
// and depth renderbuffer are scarce GL handles owned by exactly one
// Framebuffer.

#include <utility>

namespace engine {

class Framebuffer {
public:
    // width/height are the target's own resolution -- Application sizes
    // this to the window's real framebuffer size (see its constructor).
    // `samples` (default 0) requests a multisampled color+depth target
    // instead of the original single-sample one -- see this header's own
    // MSAA bug-fix comment above; the real granted sample count (after
    // clamping to this driver's limits) is logged from the constructor,
    // mirroring Window's own "log what GL_SAMPLES actually reports" pattern.
    // Throws std::runtime_error if the resulting FBO isn't
    // GL_FRAMEBUFFER_COMPLETE, mirroring ShadowMap/Window/Shader/Texture's
    // throw-on-first-failure convention.
    explicit Framebuffer(int width, int height, int samples = 0);
    ~Framebuffer();

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;
    Framebuffer(Framebuffer&& other) noexcept;
    Framebuffer& operator=(Framebuffer&& other) noexcept;

    // Binds this target's FBO and sets the viewport to its own
    // width/height (not necessarily the window's, though this phase's one
    // use always matches them). Does NOT clear it itself -- the caller
    // does that right after, matching ShadowMap::bindForWriting()'s
    // "caller owns glClear()" convention.
    void bindForWriting() const;

    // Binds this target's color texture as a regular 2D texture (for
    // sampling, not writing) on the given unit, so a post-process shader
    // can read it via a plain sampler2D -- mirrors
    // ShadowMap::bindForReading()'s naming/shape.
    //
    // Precondition: this Framebuffer must be single-sample (constructed
    // with samples <= 1). A multisample instance's color attachment is a
    // GL_TEXTURE_2D_MULTISAMPLE, not a GL_TEXTURE_2D -- it isn't
    // sampler2D-compatible and was never bound as one anywhere in this
    // engine; resolveTo() below is the multisample path's own way to reach
    // an ordinary sampler2D-compatible texture.
    void bindColorTexture(unsigned int unit) const;

    // MSAA HDR framebuffer bug fix: resolves this (typically multisample)
    // framebuffer's color buffer into `target` (a same-size, single-sample
    // Framebuffer) via glBlitFramebuffer(GL_COLOR_BUFFER_BIT, GL_NEAREST) --
    // the standard, simplest way to turn a multisample render target into
    // an ordinary sampler2D-readable one, used in place of the
    // sampler2DMS-plus-manual-per-sample-average alternative this engine
    // doesn't need. GL_NEAREST is the correct/required filter for a
    // multisample-to-single-sample resolve blit (GL rejects GL_LINEAR
    // there); works equally correctly (if pointlessly) between two
    // single-sample framebuffers of the same size, so this isn't
    // multisample-only despite the name suggesting its one real use here.
    void resolveTo(const Framebuffer& target) const;

    int width() const { return width_; }
    int height() const { return height_; }

    // The sample count this instance actually got, after Framebuffer's own
    // driver-limit clamp (see framebuffer.cpp) -- 0 for a single-sample
    // instance (samples <= 1 was requested), matching the constructor's own
    // "0 means single-sample" convention rather than reporting 1.
    int samples() const { return samples_; }

private:
    unsigned int fbo_ = 0;
    unsigned int colorTexture_ = 0;
    unsigned int depthRenderbuffer_ = 0;
    int width_ = 0;
    int height_ = 0;
    int samples_ = 0;
};

}  // namespace engine

#endif  // ENGINE_FRAMEBUFFER_HPP
