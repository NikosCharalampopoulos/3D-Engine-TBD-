#version 430 core

// Phase 13f: SSAO's lightweight geometry pre-pass. Renders the same scene
// geometry basic.vert/pbr.vert do (entities_, the ground plane, the PBR
// sphere grid -- see application.cpp's Phase 13f render() additions and
// Model::drawNormalDepth()), but writes only a view-space normal (this
// file's one output, vViewNormal) into a single-sample off-screen target
// whose depth attachment is a real, samplable GL_TEXTURE_2D (see
// framebuffer.hpp's Phase 13f depthAsTexture option) -- ssao.frag then
// reconstructs each pixel's view-space *position* from that depth texture +
// the inverse projection matrix instead of this pass writing a second
// position render target, the "depth-reconstruction, buffer-lighter"
// approach this phase's brief calls out as preferable to a full G-buffer
// for a scene this simple: one extra color attachment (just the normal)
// plus the depth every rasterized fragment already produces for free, not
// two.
//
// Same attribute layout as basic.vert/pbr.vert (Mesh's one interleaved
// vertex format, shared by every mesh in this engine) so this pass can draw
// the exact same VAOs those passes do with no separate SSAO-only vertex
// data -- aTexCoord/aTangent are simply unread here, since neither texturing
// nor normal-mapped detail matters for an occlusion estimate (see
// gbuffer.frag's own comment on using the geometric, not normal-mapped,
// normal).
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec3 aTangent;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

out vec3 vViewNormal;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    // World-space normal first (uNormalMatrix, the standard
    // transpose-inverse -- see basic.vert's comment on why not mat3(uModel)
    // directly), then rotated into view space by mat3(uView) -- uView has no
    // translation/scale component to worry about here, only rotation, so a
    // plain mat3 truncation is exactly right for transforming a direction
    // (unlike a position, which needs the full mat4).
    vViewNormal = normalize(mat3(uView) * (uNormalMatrix * aNormal));
    gl_Position = uProjection * uView * worldPos;
}
