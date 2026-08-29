#version 430 core

// Phase 18d: the selection mask pass's fragment stage -- the occlusion-
// correct half of the new 3D selection outline (replacing Phase 14d's flat
// 2D screen-space dashed-rectangle-plus-corner-brackets gizmo, which had no
// awareness of the selected entity's actual silhouette or of what else in
// the scene might be standing in front of it). Writes a flat 1.0 (this
// pass's whole "is this pixel part of the selected entity's VISIBLE
// silhouette" signal) wherever a fragment survives two independent depth
// tests, and nothing (this target was cleared to 0 first -- see
// Application::renderSelectionMask()) everywhere else:
//
//   1. This target's own ordinary GL depth test/write (selectionMaskFramebuffer_'s
//      own depth renderbuffer, GL_LESS by default, untouched by this shader)
//      resolves SELF-occlusion within the selected mesh itself -- e.g. its
//      own far side, hidden behind its own near side, the same fixed-
//      function behavior every other depth-tested pass in this engine
//      already relies on.
//   2. The explicit `discard` below resolves occlusion by EVERY OTHER object
//      in the scene: uSceneDepth is SSAO's own ssaoGBuffer_ depth texture
//      (Phase 13f) -- already the whole opaque scene's real, per-pixel depth,
//      rendered this SAME frame from this SAME camera, and already reused
//      exactly this way by SSR's own ray march (see pbr.frag's uSSRDepthMap/
//      Application::renderSSRComposite()'s own comment) -- rather than a
//      second, redundant full-scene depth pre-pass built just for this
//      pass. A selected-mesh fragment further from the camera than what
//      uSceneDepth already recorded at that same screen pixel means some
//      OTHER, opaque object is standing in front of it there, so this pixel
//      must NOT be marked selected -- exactly the "a selected object's
//      portion that's actually behind something else doesn't bleed into the
//      outline" requirement a flat 2D screen-space rectangle could never
//      satisfy, since it has no notion of depth at all.
//
// uSceneDepth is ssaoGBuffer_'s own (Phase 13f kSSAODownsampleFactor,
// currently 2x) DOWNSAMPLED depth texture, not a full-resolution one built
// fresh for this pass -- deliberately: it's already computed once per frame
// regardless of selection, from the exact same view/projection this pass
// itself uses, so reusing it is both free and the same "one already-
// computed scene depth buffer every screen-space pass shares" convention
// SSR's own reuse already established. The 2x downsample only blurs the
// occlusion decision by up to a texel or two right at an occluder's own
// silhouette edge -- invisible at this outline's own few-texel edge-
// detection width (see postprocess.frag's kSelectionOutlineRadius) -- while
// costing nothing extra to compute.
uniform sampler2D uSceneDepth;
// This pass's own render target size (selectionMaskFramebuffer_, full
// viewport resolution -- NOT uSceneDepth's downsampled one) -- needed to
// turn gl_FragCoord (this fragment's own window-space pixel coordinate) into
// the normalized [0,1] UV uSceneDepth is sampled at. Texture coordinates,
// not pixel coordinates, are resolution-independent, so this correctly
// compares a full-res selection-mask fragment against its half-res
// reference depth texel without either side needing to match resolutions.
uniform vec2 uViewportSize;
// A small bias, in the SAME non-linear [0,1] depth-buffer units gl_FragDepth/
// uSceneDepth both already use -- without it, the selected mesh's OWN
// surface (rendered here a second time, from a slightly different geometry
// pre-pass than ssaoGBuffer_'s own gbuffer.vert/.frag, see this class's own
// comment) would sometimes discard itself: two passes rasterizing the exact
// same triangles can legitimately disagree by less than a ULP of floating-
// point/depth-buffer precision, and the raw `myDepth > sceneDepth` compare
// below has no tolerance for that on its own. This is the same standard
// depth-bias idea shadow.frag's own README-documented shadow-acne fix uses,
// just applied to THIS comparison instead.
uniform float uDepthBias;

out vec4 FragColor;

void main() {
    vec2 uv = gl_FragCoord.xy / uViewportSize;
    float sceneDepth = texture(uSceneDepth, uv).r;
    if (gl_FragCoord.z > sceneDepth + uDepthBias) {
        // Something else in the scene is already closer to the camera at
        // this exact screen pixel -- discard rather than write 1.0, leaving
        // this pixel at the 0 this target was cleared to.
        discard;
    }
    FragColor = vec4(1.0);
}
