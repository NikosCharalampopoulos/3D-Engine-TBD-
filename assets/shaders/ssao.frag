#version 430 core

// Phase 13f: the SSAO kernel pass itself -- the classic Crysis/John Chapman
// hemisphere-sample-kernel technique (as documented in LearnOpenGL's SSAO
// article), reconstructing view-space position from a depth texture instead
// of reading it from a second G-buffer render target (see gbuffer.vert's own
// comment for why). Paired with postprocess.vert (an ordinary fullscreen
// quad, no model/view/projection -- see that file's header comment) exactly
// like every other screen-space pass in this engine (bloom_extract.frag,
// blur.frag, brdf_lut.frag).
//
// Output: a single-channel (stored in .r of this pass's RGBA16F target, see
// application.cpp reusing the existing Framebuffer class for it -- the
// other three channels are simply unused, the same harmless waste
// brightFramebuffer_/pingpongFramebuffer0_/1_ already accept for their own
// unused depth renderbuffer) raw occlusion factor: 1.0 = fully open/
// unoccluded, 0.0 = fully occluded. Noisy at this kernel size by design --
// ssao_blur.frag (a small box blur, run right after this pass every frame)
// is what smooths it, the standard two-pass SSAO shape this technique
// always uses rather than trying to get a clean result from one pass alone.
in vec2 vTexCoord;

out vec4 FragColor;

// SSAO's own G-buffer inputs (see gbuffer.vert/.frag and application.cpp's
// SSAO pre-pass): view-space normal, and the real device depth this pass
// reconstructs view-space position from.
uniform sampler2D uNormalMap;
uniform sampler2D uDepthMap;
// A small (see application.cpp's kSSAONoiseDim) tileable texture of random
// vectors in the surface tangent plane (z always 0 -- a rotation confined
// to the XY plane, per the standard technique) -- sampled with a UV tiled
// across the screen (uNoiseScale below) so each pixel's kernel gets a
// different, but spatially-repeating (not per-pixel-unique, which would
// need a much bigger texture) rotation. This is what turns what would
// otherwise be a banded/ringed artifact (every pixel's kernel oriented
// identically) into fine-grained noise instead -- exactly what the
// following blur pass is built to remove.
uniform sampler2D uNoiseMap;

#define SSAO_KERNEL_SIZE 32
uniform vec3 uSamples[SSAO_KERNEL_SIZE];

uniform mat4 uProjection;
uniform mat4 uInvProjection;
// screenSize / noise texture tile size (see application.cpp) -- how many
// times the noise texture repeats across the full screen.
uniform vec2 uNoiseScale;
// World/view-space sampling radius (this engine's PBR sphere grid /
// table-and-box scene sit at roughly a 1-unit scale, so this is tuned
// small relative to a "physical meters" scene -- see application.cpp's
// kSSAORadius comment) and a small depth bias (kSSAOBias) that keeps a
// perfectly flat surface from self-occluding purely from its own depth
// quantization noise (the classic SSAO "acne" failure mode).
uniform float uRadius;
uniform float uBias;

// Reconstructs a view-space position from this pixel's own device depth
// (already known, from gl_FragCoord/vTexCoord) or from a re-sampled depth at
// a different screen UV (every kernel sample's own projected position) --
// shared by both call sites below so the exact same unprojection math
// always applies. `uv` is in [0,1] texture space, `depth` is the raw
// [0,1] device-depth value read from uDepthMap.
vec3 reconstructViewPos(vec2 uv, float depth) {
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = uInvProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

void main() {
    float fragDepth = texture(uDepthMap, vTexCoord).r;

    // A depth of exactly 1.0 (the far plane) means this pixel is the
    // skybox/background, never covered by any real geometry -- treat it as
    // fully unoccluded rather than running the kernel against a
    // meaningless reconstructed position out at the far plane (which could
    // otherwise produce spurious self-occlusion against the background
    // itself, or against real geometry near the far clip edge).
    if (fragDepth >= 1.0) {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    vec3 fragPos = reconstructViewPos(vTexCoord, fragDepth);
    vec3 normal = normalize(texture(uNormalMap, vTexCoord).rgb);
    vec3 randomVec = normalize(texture(uNoiseMap, vTexCoord * uNoiseScale).xyz);

    // Gram-Schmidt: the standard way to build an orthonormal tangent basis
    // around `normal` biased by `randomVec`'s own per-tile rotation --
    // identical construction to basic.frag/pbr.frag's own normal-mapping TBN
    // (re-orthogonalize a rough tangent-ish vector against the normal, then
    // cross for the third axis), just built from a random vector here
    // instead of a mesh-authored tangent attribute.
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    for (int i = 0; i < SSAO_KERNEL_SIZE; ++i) {
        // Tangent-space kernel sample -> view space, offset from this
        // fragment's own reconstructed position by uRadius.
        vec3 samplePos = fragPos + (TBN * uSamples[i]) * uRadius;

        // Project the sample point back to screen space to look up what
        // geometry (if any) actually occupies that screen position.
        vec4 offset = uProjection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xy = offset.xy * 0.5 + 0.5;

        // Outside the screen entirely (a kernel sample can project outside
        // [0,1] near the frame's own edges) -- nothing meaningful to compare
        // against, so this sample contributes no occlusion rather than
        // sampling a clamped edge texel that belongs to unrelated geometry.
        if (offset.x < 0.0 || offset.x > 1.0 || offset.y < 0.0 || offset.y > 1.0) {
            continue;
        }

        float sampleDepth = texture(uDepthMap, offset.xy).r;
        // The background (far plane) can never occlude anything -- treat it
        // as "no geometry there" rather than reconstructing a spurious
        // far-plane position for the range check below.
        if (sampleDepth >= 1.0) {
            continue;
        }
        float sampleViewZ = reconstructViewPos(offset.xy, sampleDepth).z;

        // View-space Z is negative in front of the camera and grows more
        // negative with distance (this engine's usual convention -- see
        // basic.vert/pbr.vert's vViewSpaceDepth comment) -- the actual
        // geometry at this screen position occludes samplePos exactly when
        // it sits nearer the camera (a larger, less-negative Z) than
        // samplePos itself, beyond a small bias that guards against a flat
        // surface occluding itself from depth-buffer quantization alone
        // (the classic SSAO self-acne failure mode).
        float rangeCheck = smoothstep(0.0, 1.0, uRadius / max(abs(fragPos.z - sampleViewZ), 0.0001));
        occlusion += (sampleViewZ >= samplePos.z + uBias ? 1.0 : 0.0) * rangeCheck;
    }

    float ao = 1.0 - (occlusion / float(SSAO_KERNEL_SIZE));
    FragColor = vec4(ao, ao, ao, 1.0);
}
