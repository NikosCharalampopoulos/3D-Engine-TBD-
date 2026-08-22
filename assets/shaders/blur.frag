#version 330 core

// Phase 11: bloom's separable Gaussian blur pass -- one draw call blurs
// along EITHER the horizontal OR the vertical axis (uHorizontal selects
// which), not both at once. A full 2D Gaussian blur is separable into two
// 1D passes (blur horizontally, then blur that result vertically) with
// identical output to a real 2D kernel at a fraction of the sample cost
// (5 + 5 samples across two passes vs. 25 for an equivalent 5x5 2D kernel
// in one pass) -- the standard technique, not a shortcut/approximation.
// Application::render() runs this shader kBloomBlurPasses times, alternating
// uHorizontal and ping-ponging between two Framebuffers each call (see
// pingpongFramebuffer0_/1_ in application.hpp) since a texture can't be
// simultaneously read and written.
//
// Paired with postprocess.vert like every other screen-space pass this
// phase adds (bloom_extract.frag, and Phase 10's brdf_lut.frag before it).
in vec2 vTexCoord;

out vec4 FragColor;

uniform sampler2D uImage;
// 1.0 / (this pass's target resolution) -- derived from whichever
// Framebuffer render() is about to draw into (see kBloomDownsampleFactor),
// not hardcoded, so this stays correct if that resolution ever changes.
uniform vec2 uTexelSize;
uniform int uHorizontal;

// The standard 5-tap Gaussian kernel weights used by the classic
// LearnOpenGL bloom writeup (and, by extension, most from-scratch bloom
// implementations since): a discretized Gaussian with sigma chosen so the
// 5 taps (center + 2 on each side) capture effectively all of the curve's
// area. Declared as a plain float array rather than computed at runtime --
// this is a fixed, well-known constant kernel, not something this engine
// ever needs to vary.
const float kWeights[5] = float[](0.227027, 0.194594, 0.121621, 0.054054, 0.016216);

void main() {
    vec3 result = texture(uImage, vTexCoord).rgb * kWeights[0];
    vec2 direction = uHorizontal != 0 ? vec2(uTexelSize.x, 0.0) : vec2(0.0, uTexelSize.y);

    for (int i = 1; i < 5; ++i) {
        vec2 offset = direction * float(i);
        result += texture(uImage, vTexCoord + offset).rgb * kWeights[i];
        result += texture(uImage, vTexCoord - offset).rgb * kWeights[i];
    }

    FragColor = vec4(result, 1.0);
}
