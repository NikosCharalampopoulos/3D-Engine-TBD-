#ifndef ENGINE_SKYBOX_HPP
#define ENGINE_SKYBOX_HPP

// Phase 7b: a procedural-sky background rendered as a GL_TEXTURE_CUBE_MAP
// sampled by a small dedicated program (assets/shaders/skybox.vert/.frag),
// drawn as the scene's backdrop instead of a flat glClearColor. Kept as its
// own small class (not folded into Texture) for the same reason ShadowMap
// is its own class rather than a Texture variant: a cubemap's load path (6
// separate faces, GL_TEXTURE_CUBE_MAP_* targets, no vertical flip) and draw
// path (translation-stripped view matrix, the GL_LEQUAL depth trick) are
// different enough from Texture's plain single-2D-image job that bolting
// them on would muddy Texture's clean, single-purpose API.
//
// Move-only, same rationale as every other GL-handle-owning class here
// (Shader/Mesh/Texture/ShadowMap/Framebuffer): the cubemap texture name is
// a scarce handle owned by exactly one Skybox.

#include <glm/glm.hpp>

#include <array>
#include <string>

#include "engine/mesh.hpp"

namespace engine {

class Shader;

class Skybox {
public:
    // facePaths must hold exactly 6 image paths in this order: +X (right),
    // -X (left), +Y (top), -Y (bottom), +Z (front), -Z (back) -- see
    // skybox.cpp's kCubeMapTargets, which zips this same order against
    // GL_TEXTURE_CUBE_MAP_POSITIVE_X/NEGATIVE_X/POSITIVE_Y/NEGATIVE_Y/
    // POSITIVE_Z/NEGATIVE_Z one-for-one (the same right/left/top/bottom/
    // front/back convention LearnOpenGL's cubemap tutorial uses). Passing
    // the same 6 paths in a different order is *the* classic "cubemap
    // looks like a random jumble" bug -- every face would still load fine,
    // just onto the wrong cube face.
    //
    // Throws std::runtime_error if any face image can't be loaded/decoded.
    explicit Skybox(const std::array<std::string, 6>& facePaths);
    ~Skybox();

    Skybox(const Skybox&) = delete;
    Skybox& operator=(const Skybox&) = delete;
    Skybox(Skybox&& other) noexcept;
    Skybox& operator=(Skybox&& other) noexcept;

    // Renders the sky as the scene's background. Self-contained like
    // Material::bind(): binds `shader`, uploads uView (with translation
    // stripped -- see the .cpp)/uProjection/uSkybox, and draws the cube
    // with a GL_LEQUAL depth trick (see skybox.vert) so it only shows
    // through pixels nothing else was drawn to this frame -- restored to
    // GL_LESS before returning, so callers don't need to manage
    // glDepthFunc state around this call themselves.
    //
    // `view`/`projection` are the same camera matrices the main pass used
    // this frame; call this LAST, after all opaque scene geometry, still
    // bound to whatever framebuffer the scene itself rendered into.
    void draw(Shader& shader, const glm::mat4& view, const glm::mat4& projection,
              unsigned int textureUnit = 0) const;

    // Phase 10: the raw GL cubemap texture name, exposed read-only so
    // engine::IBLProbe (see ibl_probe.hpp) can sample this same environment
    // when convolving the irradiance/prefiltered-specular maps -- IBL's
    // whole premise is deriving those from whatever's actually visible as the
    // scene's background, not a second, separately-authored environment.
    unsigned int textureId() const { return textureId_; }

private:
    unsigned int textureId_ = 0;
    // A plain unit cube (see mesh.hpp's makeCube()) -- reused as-is rather
    // than hand-rolling a second position-only VAO/VBO. Only its position
    // attribute is read by skybox.vert; the normal/texCoord/tangent
    // attributes Mesh always wires up go unused for this draw, which costs
    // nothing (see mesh.cpp's own comment on always wiring up all four).
    Mesh cubeMesh_;
};

}  // namespace engine

#endif  // ENGINE_SKYBOX_HPP
