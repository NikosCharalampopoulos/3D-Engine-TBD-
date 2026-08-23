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
    //
    // Phase 13f: `depthAsTexture` (default false, preserving every existing
    // call site's original renderbuffer-depth behavior) requests a
    // GL_TEXTURE_2D depth attachment (GL_DEPTH_COMPONENT24, same internal
    // format as the renderbuffer path) instead of the write-only
    // renderbuffer this class has used since Phase 7b -- see this header's
    // own Phase 7b comment on why a renderbuffer was originally the right
    // (cheaper, write-only) choice for a target nothing needed to *sample*
    // depth from. SSAO's geometry pre-pass (see application.cpp's
    // Phase 13f render() additions) is the first thing in this engine that
    // does: it reconstructs each visible fragment's view-space position from
    // this depth texture + the inverse projection matrix, so that one
    // attachment has to be a real, samplable GL_TEXTURE_2D this time, mirroring
    // how ShadowMap's own depth attachment has always been a texture for the
    // exact same "something needs to sample it" reason (see shadow_map.cpp).
    // Throws std::runtime_error if combined with samples > 1: a multisample
    // depth texture would need sampler2DMS + manual per-sample resolution
    // logic in whatever reads it, which nothing in this engine needs (SSAO's
    // own G-buffer pre-pass is deliberately single-sample -- see that pass's
    // own comment in application.cpp) and Framebuffer has no reason to
    // support speculatively.
    //
    // Phase 13g bug fix: `mipmappedColor` (default false, preserving every
    // existing call site's original single-mip-level behavior) allocates a
    // full GL_LINEAR_MIPMAP_LINEAR chain on the color texture instead of a
    // single GL_LINEAR level, and enables generateColorMipmaps() below.
    // Needed the first time anything in this engine reads this target's
    // color texture at a *dynamically computed, per-fragment-varying* UV
    // rather than the fixed, 1:1 fullscreen-quad UV every earlier consumer
    // (tonemap, bloom) used -- see Application::renderSSRComposite()'s own
    // comment on hdrResolveFramebuffer_ for the specific bug this fixes.
    // Refused together with samples > 1 for the same "nothing downstream
    // needs it, don't build it speculatively" reason depthAsTexture is.
    explicit Framebuffer(int width, int height, int samples = 0, bool depthAsTexture = false,
                          bool mipmappedColor = false);
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

    // Phase 14c: the raw GL color-texture handle itself, rather than bound
    // to any particular texture unit -- needed so Application can hand this
    // target's *rendered* image straight to Dear ImGui's `ImGui::Image()`
    // (which takes an opaque texture id/`ImTextureRef`, not a texture-unit
    // index -- see editor_ui.cpp's own Phase 14c comment) instead of routing
    // it through a `sampler2D` in one of this engine's own shaders the way
    // every other reader of a Framebuffer's color texture does. Every other
    // accessor here binds the texture as a side effect of "this shader is
    // about to sample it"; this one exists purely to *hand out* the id, so
    // it deliberately does nothing else (no binding, no unit argument).
    unsigned int colorTextureId() const { return colorTexture_; }

    // Phase 13g bug fix: regenerates the color texture's mipmap chain from
    // its current (just-rendered-into) base level -- must be called once
    // after each frame's draw into this target and before anything samples
    // it with a non-zero LOD (textureLod/textureGrad/an implicit-LOD
    // texture() call with large screen-space derivatives), or those reads
    // see stale mip data from whatever this target held last time it was
    // populated. Mirrors bindColorTexture()'s "precondition: constructed
    // with the matching flag" contract -- only valid if this instance was
    // constructed with mipmappedColor = true.
    void generateColorMipmaps() const;

    // Phase 13f: binds this target's depth texture as a regular 2D texture
    // (for sampling, not writing) on the given unit -- mirrors
    // bindColorTexture()/ShadowMap::bindForReading()'s shape.
    //
    // Precondition: this Framebuffer must have been constructed with
    // depthAsTexture = true (see the constructor's own comment) -- a
    // depth *renderbuffer* (every pre-Phase-13f Framebuffer, and every
    // Phase 13f one that doesn't request this) was never meant to be
    // sampled and isn't a GL texture object at all.
    void bindDepthTexture(unsigned int unit) const;

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
    // Phase 13f: populated instead of depthRenderbuffer_ when constructed
    // with depthAsTexture = true (mutually exclusive with it -- exactly one
    // of the two is ever nonzero for a given live instance). Kept as a
    // separate member (rather than reusing depthRenderbuffer_'s handle slot
    // for both) so the destructor/move ops know which GL delete call
    // (glDeleteTextures vs. glDeleteRenderbuffers) owns whichever handle is
    // actually live, without needing a third bool flag to remember which.
    unsigned int depthTexture_ = 0;
    int width_ = 0;
    int height_ = 0;
    int samples_ = 0;
    // Phase 13g bug fix: recorded (rather than re-derived from the texture's
    // own GL_TEXTURE_MIN_FILTER) purely so generateColorMipmaps() has
    // nothing to precondition-check against but this constructor's own
    // recorded intent -- mirrors depthTexture_ acting as its own "was this
    // requested" flag above.
    bool mipmappedColor_ = false;
};

}  // namespace engine

#endif  // ENGINE_FRAMEBUFFER_HPP
