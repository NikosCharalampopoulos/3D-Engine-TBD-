#version 330 core

// Phase 7b: resolves Application's floating-point HDR color buffer
// (engine::Framebuffer, GL_RGBA16F -- can hold values above 1.0 without
// clipping) down to the default framebuffer (8-bit, implicitly clamped to
// [0,1] whenever a fragment shader writes to it).
//
// Reinhard tonemapping (color / (color + 1)) compresses that unbounded HDR
// range into [0,1) with a smooth, gradual rolloff as color grows large,
// rather than the default framebuffer's own hard clip at exactly 1.0 --
// see Application's kPointLights[0], deliberately bumped well above 1.0
// this phase specifically so this rolloff has something to visibly do (a
// plain clamp there would instead produce a flat white disc with a hard
// edge where its value crosses 1.0).
//
// Phase 7b bug-review fix: this pass used to also gamma-encode
// (pow(x, 1/2.2)) after tonemapping, on the textbook assumption that
// lighting is computed in true linear light and needs re-encoding to the
// display's gamma response. That assumption doesn't hold in *this* engine:
// texture.cpp/skybox.cpp upload every source image with a plain GL_RGB(A)
// internal format (no GL_SRGB8 -- see Texture::Texture()), so diffuse
// texture samples and the hand-picked light/tint/ambient color constants in
// application.cpp were never decoded out of gamma space in the first place.
// Every prior phase (4-7a) rendered those same un-decoded values straight to
// the (8-bit) default framebuffer with no correction at all, and *that* --
// not a true linear render -- is the "vibrant" look the gamma-encode step
// was compared against. Gamma-encoding already-gamma-space values a second
// time is a real double-correction: pow(x, 1/2.2) is concave, so it
// compresses the *ratio* between any two channels/colors every time it's
// applied (e.g. a saturated (0.9, 0.3, 0.1) sample's 9:1 R:B ratio collapses
// to roughly 2:1), which reads as exactly the flat, desaturated, washed-out
// look the project owner flagged. Removed here rather than papering over it
// with a brighter exposure alone, since no amount of pre-tonemap scaling
// fixes a compressive curve applied where nothing was ever decoded to
// invert. uExposure below is kept as a small, standard, tunable brightness
// knob (applied before the tonemap curve) restoring the punch Reinhard's
// own compression still costs even without gamma stacked on top of it.
//
// Phase 11: also additively blends in the bloom pipeline's final blurred
// bright-pass texture (uBloomBuffer -- see bloom_extract.frag/blur.frag and
// Application::render()'s bloom section) before the Reinhard curve below,
// exactly like the phase brief asks for: bloom composited pre-tonemap so it
// rolls off through the same curve as everything else instead of being a
// separately-clamped overlay. Doing that add here (in the same pass that
// already reads uHdrBuffer) rather than as a fourth full-res GL_BLEND draw
// into hdrFramebuffer_ itself is simpler and has the identical result --
// both computed as (scene + bloom) before tonemap, this just does the sum
// in one shader instead of one texture write followed by a blended second
// one.
in vec2 vTexCoord;

out vec4 FragColor;

uniform sampler2D uHdrBuffer;
uniform sampler2D uBloomBuffer;
uniform float uExposure;
// A separate multiplier from uExposure (which only scales the base scene)
// so bloom intensity can be tuned independently of overall scene exposure --
// 1.0 (the value Application actually uses) means "add the blurred
// bright-pass result at its own real brightness," the standard/expected
// bloom strength.
uniform float uBloomStrength;

void main() {
    vec3 hdrColor = texture(uHdrBuffer, vTexCoord).rgb * uExposure;
    vec3 bloomColor = texture(uBloomBuffer, vTexCoord).rgb * uBloomStrength;
    vec3 mapped = (hdrColor + bloomColor) / (hdrColor + bloomColor + vec3(1.0));
    FragColor = vec4(mapped, 1.0);
}
