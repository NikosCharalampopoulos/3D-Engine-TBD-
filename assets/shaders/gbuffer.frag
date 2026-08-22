#version 430 core

// Phase 13f: SSAO's geometry pre-pass fragment stage -- writes only the
// interpolated, re-normalized view-space normal (gbuffer.vert's
// vViewNormal) to this pass's single color attachment. Depth is written
// implicitly by the normal GL depth-test/write pipeline into this target's
// own depth *texture* (see framebuffer.hpp's depthAsTexture option) -- no
// explicit gl_FragDepth assignment needed, the same "let the fixed-function
// depth path do its job" approach every other depth-writing pass in this
// engine (shadow.frag, and the main color passes themselves) already takes.
//
// Deliberately the *geometric* normal (interpolated per-vertex, exactly like
// basic.frag/pbr.frag's own `vNormal` before either shader's optional normal
// map perturbs it) rather than a normal-mapped one: SSAO's occlusion
// estimate only needs to know the coarse local surface orientation to build
// a plausible sampling hemisphere, not the fine bump-mapped detail a
// material's own normal map adds for direct/IBL lighting -- sampling that
// finer detail here would make the occlusion kernel's hemisphere jitter with
// every normal-map texel instead of following the actual underlying
// geometry, which is closer to noise than to a real occlusion signal at this
// technique's sample counts.
in vec3 vViewNormal;

out vec4 FragColor;

void main() {
    // Re-normalize: vViewNormal was already unit-length per-vertex (see
    // gbuffer.vert), but linear interpolation across a triangle's three
    // vertices does not preserve unit length, the same reason basic.frag/
    // pbr.frag re-normalize their own interpolated vNormal before using it.
    FragColor = vec4(normalize(vViewNormal), 1.0);
}
