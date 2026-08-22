#ifndef ENGINE_HDRI_LOADER_HPP
#define ENGINE_HDRI_LOADER_HPP

// Phase 13e: loads a real equirectangular HDR environment map (a Radiance
// .hdr / RGBE file, e.g. assets/textures/hdri/sky.hdr -- see
// tools/generate_hdri.py for how that file was produced offline) and
// GPU-converts it into a floating-point GL_TEXTURE_CUBE_MAP, so it can feed
// engine::Skybox (background rendering) and, through Skybox's own
// textureId(), engine::IBLProbe (diffuse/specular convolution) exactly the
// way the old 6-PNG procedural skybox always has -- see skybox.hpp's own
// Phase 13e comment for how the two paths meet at one Skybox class.
//
// A free function rather than a class: unlike Skybox/IBLProbe, nothing here
// needs to persist after the conversion finishes -- the equirectangular
// source image and the temporary capture FBO this function builds are both
// fully disposed of before it returns; only the finished cubemap texture
// name escapes (transferred to the caller, typically wrapped immediately in
// a Skybox -- see application.cpp's buildSkybox()). This mirrors
// engine::IBLProbe's own constructor, which builds and discards a temporary
// capture FBO the same way, just without needing a whole class wrapper here
// since there's no persistent state (three named maps) to hand back.

#include <string>

namespace engine {

class Shader;

// `hdrPath` is a file path to a Radiance RGBE (.hdr) equirectangular image,
// read via stb_image's stbi_loadf() (this project's existing vendored
// external/stb/stb_image.h already supports HDR loading -- see that header's
// own "HDR image support" section -- no new vendored dependency needed).
// `conversionShader` must be a linked program pairing
// assets/shaders/cubemap_capture.vert (reused as-is from Phase 10 -- see
// that file's own header comment) with
// assets/shaders/equirect_to_cubemap.frag; its `uEquirectangularMap`/
// `uView`/`uProjection` uniforms are set by this function, not the caller.
// `faceSize` is the per-face resolution of the resulting cubemap (512 is a
// reasonable default -- see this phase's own README notes for why that's
// enough: like Skybox's old procedural faces, this is a background/ambient
// source, not a surface a camera ever gets close enough to see individual
// texels of).
//
// Returns a fully-configured (GL_RGBA16F storage, GL_LINEAR filtering,
// GL_CLAMP_TO_EDGE wrap on every axis, mipmap-free -- see this function's
// own .cpp comment for why no mip chain is needed) GL_TEXTURE_CUBE_MAP name
// that the caller now owns (deletes via glDeleteTextures when done -- e.g.
// by handing it to Skybox's unsigned-int-taking constructor, which takes
// over that ownership the same way its 6-PNG constructor already owns the
// texture name it allocates itself).
//
// Throws std::runtime_error (after logging the real reason via LOG_ERROR,
// mirroring every other GL-resource-owning constructor/loader in this
// engine) if the HDR file can't be loaded/decoded, or if the temporary
// capture FBO this function builds isn't GL_FRAMEBUFFER_COMPLETE. Cleans up
// every GL resource it already created before throwing, same as
// IBLProbe's constructor.
unsigned int loadHdrEquirectangularAsCubemap(const std::string& hdrPath, Shader& conversionShader,
                                              int faceSize = 512);

}  // namespace engine

#endif  // ENGINE_HDRI_LOADER_HPP
