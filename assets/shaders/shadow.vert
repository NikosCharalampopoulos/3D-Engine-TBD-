#version 330 core

// Phase 7a: the shadow-map depth pass's vertex stage. Renders the scene from
// the directional light's point of view into a depth-only framebuffer (see
// engine::ShadowMap) -- only clip-space position matters, so only aPos
// (location 0) is read even though the same interleaved Mesh buffer also
// carries normal/texCoord/tangent (locations 1-3) for the main pass.
layout(location = 0) in vec3 aPos;

uniform mat4 uModel;
// Light-space "view * projection", built once per frame in
// Application::render() from a fixed light position + orthographic
// projection sized to cover the test scene (see application.cpp) -- the
// same matrix the main pass uses to transform each fragment into light-clip
// space for the shadow lookup (see basic.vert/basic.frag).
uniform mat4 uLightSpaceMatrix;

void main() {
    gl_Position = uLightSpaceMatrix * uModel * vec4(aPos, 1.0);
}
