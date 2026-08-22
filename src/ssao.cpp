#include "engine/ssao.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "engine/gl_debug.hpp"
#include "engine/log.hpp"

namespace engine {

namespace {

// Fixed seed (not std::random_device) so this engine's kernel/noise texture
// -- and therefore every headless screenshot's exact pixel values -- are
// reproducible run to run, the same determinism every other piece of
// "headless verification must be able to compare byte-for-byte" state in
// this engine already relies on (e.g. ENGINE_CAMERA_DEMO's scripted
// waypoints, ENGINE_FRUSTUM_CULL_DEMO's fixed pose). A real interactive
// game would have no reason to care, but this project's verification
// harness explicitly does.
constexpr unsigned int kRandomSeed = 13521u;

}  // namespace

SSAOKernel::SSAOKernel(int kernelSize, int noiseDim) {
    std::mt19937 gen(kRandomSeed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    // The standard John Chapman hemisphere kernel: each sample starts as a
    // random point inside the unit hemisphere oriented around +Z in tangent
    // space (x/y in [-1,1], z in [0,1] -- confining z to be non-negative is
    // what keeps every sample on the +Z side, matching a surface normal
    // pointing straight along tangent-space +Z), normalized to the
    // hemisphere's surface, then re-scaled by a random [0,1] factor so
    // samples fill the hemisphere's *volume* rather than only its shell.
    samples_.reserve(static_cast<std::size_t>(kernelSize));
    for (int i = 0; i < kernelSize; ++i) {
        glm::vec3 sample(dist(gen) * 2.0f - 1.0f, dist(gen) * 2.0f - 1.0f, dist(gen));
        sample = glm::normalize(sample);
        sample *= dist(gen);

        // The classic accelerating-interpolation trick (LearnOpenGL's SSAO
        // article, John Chapman's original writeup): without it, samples
        // are uniformly distributed across the hemisphere's radius, wasting
        // most of the kernel's sampling budget far from the fragment (where
        // occlusion detail matters least) instead of clustered close to it
        // (where it matters most, e.g. a contact crease's own immediate
        // neighborhood). lerp(0.1, 1.0, t*t) biases the scale curve so most
        // samples land close to the origin, with fewer reaching all the way
        // out to the full uRadius.
        float scale = static_cast<float>(i) / static_cast<float>(kernelSize);
        scale = 0.1f + (1.0f - 0.1f) * (scale * scale);
        sample *= scale;

        samples_.push_back(sample);
    }

    // Tileable rotation-noise texture: noiseDim x noiseDim random vectors,
    // confined to the tangent plane (z = 0 -- a rotation *around* the
    // surface normal, not a tilt away from it) so ssao.frag's Gram-Schmidt
    // re-orthogonalization against the real per-pixel normal always
    // produces a valid basis regardless of this random vector's own
    // direction. Stored as floats (RGB16F, not a normalized 8-bit format):
    // these components range over [-1, 1], which an unsigned normalized
    // texture format would clamp/rescale incorrectly.
    // GL_RGBA16F (not GL_RGB16F): this engine's hand-written GL loader (see
    // external/glad) only declares the handful of enum values every other
    // GL call site here actually needs, and GL_RGBA16F is already one of
    // them (Framebuffer's own color attachment format) while GL_RGB16F isn't
    // -- reusing the 3-component format's nearest already-available
    // superset avoids adding a driver-header enum value nothing else in
    // this engine uses. The unused 4th (alpha) component is simply padded
    // to 1.0 and never read (ssao.frag only samples uNoiseMap's .xyz, and
    // .z is always 0 anyway -- see this constructor's own comment above).
    const std::size_t noiseTexelCount = static_cast<std::size_t>(noiseDim) * static_cast<std::size_t>(noiseDim);
    std::vector<glm::vec4> noiseData;
    noiseData.reserve(noiseTexelCount);
    for (std::size_t i = 0; i < noiseTexelCount; ++i) {
        noiseData.emplace_back(dist(gen) * 2.0f - 1.0f, dist(gen) * 2.0f - 1.0f, 0.0f, 1.0f);
    }

    GL_CHECK(glGenTextures(1, &noiseTexture_));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, noiseTexture_));
    GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, noiseDim, noiseDim, 0, GL_RGBA, GL_FLOAT, noiseData.data()));
    // GL_NEAREST: this is a lookup table of discrete random rotations, not
    // an image meant to be smoothly interpolated -- blending two
    // neighboring random vectors together would produce a *third*,
    // meaningless rotation, not a legitimate in-between one.
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    // GL_REPEAT (not this engine's usual GL_CLAMP_TO_EDGE): ssao.frag
    // deliberately samples this texture with UVs scaled well past [0,1]
    // (uNoiseScale = screen size / noiseDim, see application.cpp) so it
    // tiles across the whole screen -- clamping would instead smear the
    // texture's own edge texels across most of the frame.
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));

    LOG_INFO("SSAOKernel created: " + std::to_string(kernelSize) + " hemisphere samples, " +
              std::to_string(noiseDim) + "x" + std::to_string(noiseDim) + " rotation-noise texture");
}

SSAOKernel::~SSAOKernel() {
    if (noiseTexture_ != 0) {
        glDeleteTextures(1, &noiseTexture_);
    }
}

SSAOKernel::SSAOKernel(SSAOKernel&& other) noexcept
    : samples_(std::move(other.samples_)), noiseTexture_(other.noiseTexture_) {
    other.noiseTexture_ = 0;
}

SSAOKernel& SSAOKernel::operator=(SSAOKernel&& other) noexcept {
    if (this != &other) {
        if (noiseTexture_ != 0) {
            glDeleteTextures(1, &noiseTexture_);
        }
        samples_ = std::move(other.samples_);
        noiseTexture_ = other.noiseTexture_;
        other.noiseTexture_ = 0;
    }
    return *this;
}

void SSAOKernel::bindNoiseTexture(unsigned int unit) const {
    GL_CHECK(glActiveTexture(GL_TEXTURE0 + unit));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, noiseTexture_));
}

}  // namespace engine
