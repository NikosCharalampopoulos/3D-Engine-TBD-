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
// Gamma correction (pow(x, 1/2.2)) follows tonemapping rather than
// replacing it: Reinhard's job is compressing HDR range, gamma's job is
// converting the result from the linear space this engine's lighting math
// is computed in to the roughly-2.2-gamma response an 8-bit display target
// expects, so midtones don't render too dark. Neither basic.frag (the main
// lit pass) nor the skybox shader gamma-corrects on its own -- this is the
// one place in the whole per-frame pipeline that does, applied uniformly
// to scene and sky alike since both are written into the same linear HDR
// buffer this pass reads.
in vec2 vTexCoord;

out vec4 FragColor;

uniform sampler2D uHdrBuffer;

void main() {
    vec3 hdrColor = texture(uHdrBuffer, vTexCoord).rgb;
    vec3 mapped = hdrColor / (hdrColor + vec3(1.0));
    mapped = pow(mapped, vec3(1.0 / 2.2));
    FragColor = vec4(mapped, 1.0);
}
