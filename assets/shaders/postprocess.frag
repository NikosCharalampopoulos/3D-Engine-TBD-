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

// Phase 13f: ENGINE_SSAO_DEBUG=1 (see application.cpp) -- when set, this
// pass shows SSAO's own raw (pre-blur) occlusion buffer directly, in place
// of the ordinarily-tonemapped scene, so a headless screenshot can verify
// the occlusion term's own noise/banding/halo characteristics in isolation
// rather than only its (deliberately subtle) blended contribution to the
// final lit image -- the same "debug visualization overrides the normal
// composite" pattern basic.frag/pbr.frag's own uClusterDebug already uses,
// just at the postprocess stage since SSAO's buffer lives at this pass's
// own resolution, not per-fragment in the main color pass.
uniform sampler2D uSSAOMap;
uniform int uSSAODebug;

// Phase 18d: the selection outline's edge-detection composite -- the second
// half of the new 3D silhouette outline (see Application::renderSelectionMask()/
// selection_mask.frag for the first half, which builds uSelectionMask).
// Reused this SAME final compositing pass rather than a separate fullscreen
// draw call: this is already the one place every frame's fully-finished,
// tonemapped image exists as a sampler2D right before it's handed to
// EditorUI's ImGui::Image(), the same "one pass, already reading everything
// this frame needs to composite" role uBloomBuffer/uSSAOMap above already
// fill for their own overlays.
//
// uSelectionMask is selectionMaskFramebuffer_'s own color texture -- 1.0
// wherever the selected entity's VISIBLE (not-occluded-by-anything-else)
// silhouette landed this frame, 0.0 everywhere else (see that pass's own
// comment). uHasSelection is 0 every frame nothing is selected -- checked
// FIRST, below, so this branch is skipped entirely rather than sampling
// uSelectionMask at all in that case, guaranteeing this pass's output is
// byte-for-byte identical to what it would be with this whole feature never
// built, exactly the "no selection => no visible change whatsoever" default
// Phase 14d's own now-removed 2D outline already established and this
// phase's own brief requires stay true.
uniform sampler2D uSelectionMask;
uniform vec2 uSelectionMaskTexelSize;
uniform int uHasSelection;
uniform vec3 uSelectionOutlineColor;

// A texel is painted as outline if IT is outside the mask (not part of the
// selected entity's own visible silhouette) but at least one texel within
// this small fixed radius of it IS inside the mask -- i.e. "this texel sits
// just outside the selection, right at its edge." A simple few-texel-radius
// max-neighbor check, not a full Sobel/gradient operator: this only ever
// needs to answer "is there a mask/no-mask transition near here," not
// estimate an edge's exact direction or strength, so the cheaper check is
// both sufficient and, at one sampler2D fetch per neighbor texel instead of
// a full 3x3 (or larger) weighted kernel, noticeably less work per pixel.
const int kSelectionOutlineRadius = 2;

float selectionOutlineFactor(vec2 uv) {
    // Already inside the selected entity's own silhouette -- Unity/Unreal-
    // style selection outlines draw a thin band tracing the SHAPE, not a
    // wash over the whole selected object, so a texel already marked
    // selected never itself becomes outline (its neighbors right outside
    // the silhouette are what pick up the band instead, one step below).
    if (texture(uSelectionMask, uv).r > 0.5) {
        return 0.0;
    }
    float neighborMax = 0.0;
    for (int dy = -kSelectionOutlineRadius; dy <= kSelectionOutlineRadius; ++dy) {
        for (int dx = -kSelectionOutlineRadius; dx <= kSelectionOutlineRadius; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            vec2 offset = vec2(float(dx), float(dy)) * uSelectionMaskTexelSize;
            neighborMax = max(neighborMax, texture(uSelectionMask, uv + offset).r);
        }
    }
    return neighborMax;
}

void main() {
    if (uSSAODebug != 0) {
        float ao = texture(uSSAOMap, vTexCoord).r;
        FragColor = vec4(ao, ao, ao, 1.0);
        return;
    }

    vec3 hdrColor = texture(uHdrBuffer, vTexCoord).rgb * uExposure;
    vec3 bloomColor = texture(uBloomBuffer, vTexCoord).rgb * uBloomStrength;
    vec3 mapped = (hdrColor + bloomColor) / (hdrColor + bloomColor + vec3(1.0));

    // Phase 18d: composited AFTER tonemapping, not blended into the HDR
    // scene color before it -- this is a flat UI-style accent (the same
    // teal Phase 17a's own theme already uses for a selected row/active
    // toolbar button), meant to read as exactly that fixed color on screen
    // regardless of the scene's own exposure/bloom that frame, not
    // something that should itself bloom or tonemap-compress the way an
    // actual light source in the scene would.
    if (uHasSelection != 0) {
        float outline = selectionOutlineFactor(vTexCoord);
        mapped = mix(mapped, uSelectionOutlineColor, outline);
    }

    FragColor = vec4(mapped, 1.0);
}
