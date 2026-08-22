#ifndef ENGINE_SSAO_HPP
#define ENGINE_SSAO_HPP

// Phase 13f: SSAO's own small, self-contained state -- the hemisphere
// sample kernel and the tileable per-pixel rotation-noise texture the
// classic Crysis/John Chapman SSAO technique needs (as documented in
// LearnOpenGL's SSAO article), generated once at construction and read-only
// for the rest of this engine's run. Everything else SSAO needs (the
// G-buffer/raw/blurred render targets, the three shader programs, the
// per-frame pass orchestration) lives directly in Application/
// application.cpp instead -- this class is deliberately just the one piece
// of "randomly generated once, then reused unchanged every frame" data,
// mirroring the "small owning class for one non-obvious, reusable piece of
// state" shape IBLProbe/ClusterLightCuller already establish rather than
// growing into a full mini render-graph of its own.

#include <glm/glm.hpp>

#include <vector>

namespace engine {

class SSAOKernel {
public:
    // kernelSize hemisphere samples (tangent-space, oriented around +Z,
    // biased to cluster more samples near the origin via the standard
    // `lerp(0.1, 1.0, i*i/count*count)` scale curve -- see the .cpp) plus a
    // noiseDim x noiseDim tileable texture of random per-pixel rotation
    // vectors (confined to the XY/tangent plane, z always 0 -- also the
    // standard technique). noiseDim must be small (this engine uses 4x4,
    // see application.cpp's kSSAONoiseDim) -- it's tiled across the whole
    // screen, not stretched to cover it.
    SSAOKernel(int kernelSize, int noiseDim);
    ~SSAOKernel();

    SSAOKernel(const SSAOKernel&) = delete;
    SSAOKernel& operator=(const SSAOKernel&) = delete;
    SSAOKernel(SSAOKernel&& other) noexcept;
    SSAOKernel& operator=(SSAOKernel&& other) noexcept;

    // The generated kernel, in construction order -- ssao.frag's
    // uSamples[SSAO_KERNEL_SIZE] array is uploaded from exactly this,
    // once, right after ssaoShader_ is constructed (see application.cpp);
    // SSAO_KERNEL_SIZE in that shader must match samples().size(), the same
    // "kept in sync by hand across the GLSL/C++ boundary" situation this
    // engine's other fixed-size shader arrays (MAX_POINT_LIGHTS,
    // CLUSTER_GRID_X/Y/Z, ...) are already in.
    const std::vector<glm::vec3>& samples() const { return samples_; }

    // Binds the noise texture as a regular 2D texture (for sampling) on the
    // given unit -- mirrors Framebuffer::bindColorTexture()'s shape.
    void bindNoiseTexture(unsigned int unit) const;

private:
    std::vector<glm::vec3> samples_;
    unsigned int noiseTexture_ = 0;
};

}  // namespace engine

#endif  // ENGINE_SSAO_HPP
