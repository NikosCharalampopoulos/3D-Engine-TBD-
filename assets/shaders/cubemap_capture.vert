#version 330 core

// Phase 10: shared vertex stage for the two one-time IBL cubemap-convolution
// passes (irradiance_convolution.frag / prefilter.frag -- see
// engine::IBLProbe). Renders engine::makeCube()'s unit cube from a fixed
// 90-degree-FOV cube-face view/projection pair (IBLProbe supplies 6 such view
// matrices, one per face, the same convention Skybox's own kCubeMapTargets
// order uses), so each fragment's own cube-local position doubles as the
// world-space direction that face's texel represents -- the same vDirection
// trick skybox.vert uses. Unlike skybox.vert, this pass has no camera and no
// depth trick (gl_Position.z forced to gl_Position.w): it's an offline,
// startup-only render into a small offscreen cubemap FBO with depth testing
// disabled entirely (see IBLProbe's constructor), not a per-frame background
// draw racing the rest of the scene's depth buffer.
layout(location = 0) in vec3 aPos;

uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vDirection;

void main() {
    vDirection = aPos;
    gl_Position = uProjection * uView * vec4(aPos, 1.0);
}
