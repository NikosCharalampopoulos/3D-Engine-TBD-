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
// Move-only, same rationale/pattern as ShadowMap: the FBO, color texture,
// and depth renderbuffer are scarce GL handles owned by exactly one
// Framebuffer.

#include <utility>

namespace engine {

class Framebuffer {
public:
    // width/height are the target's own resolution -- Application sizes
    // this to the window's real framebuffer size (see its constructor).
    // Throws std::runtime_error if the resulting FBO isn't
    // GL_FRAMEBUFFER_COMPLETE, mirroring ShadowMap/Window/Shader/Texture's
    // throw-on-first-failure convention.
    Framebuffer(int width, int height);
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
    void bindColorTexture(unsigned int unit) const;

    int width() const { return width_; }
    int height() const { return height_; }

private:
    unsigned int fbo_ = 0;
    unsigned int colorTexture_ = 0;
    unsigned int depthRenderbuffer_ = 0;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace engine

#endif  // ENGINE_FRAMEBUFFER_HPP
