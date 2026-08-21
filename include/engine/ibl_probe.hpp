#ifndef ENGINE_IBL_PROBE_HPP
#define ENGINE_IBL_PROBE_HPP

// Phase 10: real-time image-based lighting, built on top of the existing
// skybox cubemap (see skybox.hpp) via the standard split-sum approximation
// (Karis, "Real Shading in Unreal Engine 4"). Precomputes, once at startup,
// the three pieces pbr.frag's ambient term needs instead of its old flat
// `uAmbientColor * albedo * ao * (1 - metallic)` placeholder (see that
// shader's own Phase 9 comment, which explicitly named this as Phase 10's
// job):
//
//   1. irradianceMap_: a small (kIrradianceMapSize per face) diffuse
//      irradiance cubemap -- the environment convolved against a cosine-
//      weighted hemisphere at every texel (see
//      assets/shaders/irradiance_convolution.frag). Sampled directly by
//      surface normal N in pbr.frag for the diffuse IBL term.
//   2. prefilterMap_: a specular environment cubemap with a real mip chain
//      (kPrefilterMipLevels levels, base resolution kPrefilterBaseSize),
//      each mip convolved against the GGX specular lobe at a fixed roughness
//      via importance sampling (see assets/shaders/prefilter.frag) --
//      roughness 0 at mip 0 (crisp mirror-like reflections) up to roughness
//      1 at the last mip (a nearly flat, blurred-out environment average).
//      Sampled by pbr.frag via textureLod(prefilterMap_, R, roughness *
//      MAX_REFLECTION_LOD), so a rough surface's reflection blurs smoothly as
//      its roughness increases, purely from GL's own trilinear mip
//      interpolation between adjacent precomputed mips.
//   3. brdfLUT_: a small 2D lookup texture (kBrdfLutSize x kBrdfLutSize)
//      storing the split-sum's other precomputed half -- the BRDF's own
//      integral as a function of (N.V, roughness), independent of any
//      particular environment (see assets/shaders/brdf_lut.frag) -- so it
//      only ever needs computing once per (N.V, roughness) grid cell, not
//      once per environment.
//
// All three are rendered via ordinary GL draw calls into a small, temporary
// offscreen FBO that exists only for the lifetime of the constructor (see
// ibl_probe.cpp) -- no persistent render-target class is needed here since
// nothing re-renders these maps after startup; the environment (skybox) this
// engine ships is static for the whole run.
//
// Move-only, same rationale as every other GL-handle-owning class in this
// engine (Shader/Mesh/Texture/Skybox/Framebuffer/ShadowMap): the three
// cubemap/2D texture names are scarce handles owned by exactly one IBLProbe.

#include <utility>

namespace engine {

class Shader;

class IBLProbe {
public:
    // Fixed per-face resolution of the diffuse irradiance cubemap -- kept
    // small deliberately (see irradiance_convolution.frag's header comment):
    // irradiance is a very low-frequency signal (every texel already
    // integrates over an entire hemisphere), so a small map loses nothing
    // visible while keeping the one-time convolution cost (per-face texel
    // count x per-texel sample count) manageable.
    static constexpr int kIrradianceMapSize = 32;
    // Base (mip 0) resolution of the prefiltered specular cubemap; each
    // subsequent mip halves this (128, 64, 32, 16, 8 for
    // kPrefilterMipLevels == 5).
    static constexpr int kPrefilterBaseSize = 128;
    // Mip count of the prefiltered specular cubemap -- also the number of
    // discrete roughness values actually convolved (0, 0.25, 0.5, 0.75, 1.0,
    // evenly spaced across [0, 1] by mip index / (kPrefilterMipLevels - 1)).
    // pbr.frag's MAX_REFLECTION_LOD must stay in sync with this (see that
    // shader's own comment).
    static constexpr int kPrefilterMipLevels = 5;
    // Resolution of the 2D (N.V, roughness) BRDF integration LUT.
    static constexpr int kBrdfLutSize = 128;

    // `environmentCubemap` is an existing, already-uploaded GL_TEXTURE_CUBE_MAP
    // texture name (see Skybox::textureId()) -- IBLProbe never takes
    // ownership of it, only samples it while convolving its own three maps.
    // `irradianceShader`/`prefilterShader`/`brdfShader` are the three
    // precompute programs (assets/shaders/cubemap_capture.vert +
    // irradiance_convolution.frag / prefilter.frag, and
    // assets/shaders/postprocess.vert + brdf_lut.frag respectively) --
    // passed in (rather than constructed internally) so Application can
    // route them through its own ResourceManager like every other shader,
    // and so nothing here needs to know an asset path or resolveAssetPath()
    // itself.
    //
    // Throws std::runtime_error if any of the three intermediate capture
    // FBOs this constructor builds isn't GL_FRAMEBUFFER_COMPLETE, mirroring
    // every other GL-resource-owning class's throw-on-first-failure
    // convention.
    IBLProbe(unsigned int environmentCubemap, Shader& irradianceShader, Shader& prefilterShader,
             Shader& brdfShader);
    ~IBLProbe();

    IBLProbe(const IBLProbe&) = delete;
    IBLProbe& operator=(const IBLProbe&) = delete;
    IBLProbe(IBLProbe&& other) noexcept;
    IBLProbe& operator=(IBLProbe&& other) noexcept;

    // Binds the three precomputed maps for sampling on the given texture
    // units (irradiance as GL_TEXTURE_CUBE_MAP, prefilter likewise, brdf as
    // a plain GL_TEXTURE_2D) -- callers still point their shader's own
    // sampler uniforms (uIrradianceMap/uPrefilterMap/uBrdfLUT) at these same
    // units themselves (mirrors Texture::bind()/ShadowMap::bindForReading()'s
    // "binding a unit vs. pointing a uniform at it are separate steps" style
    // rather than baking uniform names in here).
    void bindForSampling(unsigned int irradianceUnit, unsigned int prefilterUnit,
                          unsigned int brdfUnit) const;

    unsigned int irradianceMap() const { return irradianceMap_; }
    unsigned int prefilterMap() const { return prefilterMap_; }
    unsigned int brdfLUT() const { return brdfLUT_; }

private:
    unsigned int irradianceMap_ = 0;
    unsigned int prefilterMap_ = 0;
    unsigned int brdfLUT_ = 0;
};

}  // namespace engine

#endif  // ENGINE_IBL_PROBE_HPP
