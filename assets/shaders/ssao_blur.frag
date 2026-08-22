#version 430 core

// Phase 13f: SSAO's blur pass -- a small box blur over ssao.frag's raw,
// per-pixel-noisy occlusion output, sized to exactly cancel the noise
// texture's own tile size (see application.cpp's kSSAONoiseDim/render()):
// since ssao.frag's per-pixel randomVec rotation repeats every
// kSSAONoiseDim pixels, averaging over exactly that same NxN neighborhood
// is what the standard technique (LearnOpenGL's SSAO article) uses to fully
// smooth that periodic noise back out rather than leaving a visible
// checkerboard/tiling artifact at the noise texture's own repeat period.
// Paired with postprocess.vert like every other fullscreen pass in this
// engine.
in vec2 vTexCoord;

out vec4 FragColor;

uniform sampler2D uSSAOMap;
// 1.0 / (this pass's own target resolution) -- same "derive from the real
// target, don't hardcode" approach blur.frag's uTexelSize already takes.
uniform vec2 uTexelSize;

// Half-width of the box blur's square window in texels -- kSSAONoiseDim / 2
// (application.cpp uploads this so the two constants can't drift apart by
// hand-editing only one of them).
uniform int uBlurRadius;

void main() {
    float result = 0.0;
    int sampleCount = 0;
    for (int x = -uBlurRadius; x < uBlurRadius; ++x) {
        for (int y = -uBlurRadius; y < uBlurRadius; ++y) {
            vec2 offset = vec2(float(x), float(y)) * uTexelSize;
            result += texture(uSSAOMap, vTexCoord + offset).r;
            ++sampleCount;
        }
    }
    result /= float(sampleCount);
    FragColor = vec4(result, result, result, 1.0);
}
