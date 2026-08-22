#version 330 core

// Phase 11: bloom's bright-pass extraction. Paired with postprocess.vert
// (an ordinary fullscreen quad, no model/view/projection -- see that file's
// own header comment) exactly like brdf_lut.frag reuses it for Phase 10's
// BRDF LUT precompute. Samples hdrFramebuffer_'s already fully-lit HDR
// color buffer (the same texture the final resolve pass reads) and keeps
// only pixels whose luminance exceeds uThreshold, zeroing everything else --
// the standard "which pixels are bright enough to glow" mask a separable
// Gaussian blur (assets/shaders/blur.frag) then softens into a bloom
// texture, composited back in postprocess.frag before tonemapping.
//
// Luminance uses the standard Rec. 709 (BT.709) coefficients
// (0.2126, 0.7152, 0.0722) -- the conventional linear-light luma weights
// (green weighted heaviest, matching human luminance sensitivity), not an
// arbitrary/equal-weighted average.
in vec2 vTexCoord;

out vec4 FragColor;

uniform sampler2D uHdrBuffer;
uniform float uThreshold;

void main() {
    vec3 color = texture(uHdrBuffer, vTexCoord).rgb;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    // A hard cutoff (not a smoothstep-softened one) is deliberate: this
    // pass's whole job is to hand the blur pass a mask of "genuinely bright"
    // pixels -- the blur itself is what turns that hard mask into a soft
    // glow, so softening the threshold here too would double-soften the
    // falloff for no benefit.
    FragColor = vec4(luminance > uThreshold ? color : vec3(0.0), 1.0);
}
