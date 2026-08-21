#ifndef ENGINE_SHADOW_MAP_HPP
#define ENGINE_SHADOW_MAP_HPP

// Phase 7a: a small RAII wrapper around a depth-only framebuffer object,
// used to render the scene from the directional light's point of view (a
// "shadow map") so the main render pass can look up, per fragment, whether
// that fragment was the closest thing to the light or something else was in
// front of it (i.e. it's in shadow). Deliberately not a general "render
// target" or "render pass" abstraction -- this engine only has one shadow
// caster (the directional light) and one shadow-mapping use case, so this
// class only knows how to do exactly that, the same "just enough structure,
// no speculative generality" philosophy as Entity/ResourceManager.
//
// Move-only, same rationale as Texture/Mesh/Shader: the FBO and depth
// texture are scarce GL handles owned by exactly one ShadowMap.

#include <utility>

namespace engine {

class ShadowMap {
public:
    // width/height are the shadow map's own resolution (independent of the
    // window's framebuffer size) -- 1024x1024 is a reasonable default for
    // this engine's small test scene; a larger scene or harder shadow edges
    // would want a bigger map. Throws std::runtime_error if the resulting
    // FBO isn't GL_FRAMEBUFFER_COMPLETE, mirroring Window/Shader/Texture's
    // throw-on-first-failure convention.
    ShadowMap(int width, int height);
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;
    ShadowMap(ShadowMap&& other) noexcept;
    ShadowMap& operator=(ShadowMap&& other) noexcept;

    // Binds this shadow map's FBO and sets the viewport to its own
    // width/height (not the window's) -- call before rendering the
    // depth-only pass. Does NOT clear the depth buffer itself (the caller
    // does that right after, alongside whatever other per-pass setup it
    // wants), matching Application::render()'s existing "caller owns
    // glClear()" convention for the main framebuffer.
    void bindForWriting() const;

    // Binds this shadow map's depth texture as a regular 2D texture (for
    // sampling, not writing) on the given texture unit, so the main render
    // pass's fragment shader can read it via a plain sampler2D.
    void bindForReading(unsigned int unit) const;

    int width() const { return width_; }
    int height() const { return height_; }

private:
    unsigned int fbo_ = 0;
    unsigned int depthTexture_ = 0;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace engine

#endif  // ENGINE_SHADOW_MAP_HPP
