#version 330 core

// Phase 7b: the HDR-resolve fullscreen pass's vertex stage. aPos/aTexCoord
// come from engine::makeFullscreenQuad() (see mesh.hpp) -- already
// authored directly in NDC space, so unlike every other shader in this
// engine there's no model/view/projection here at all: this pass operates
// purely in screen space over the already fully-lit HDR image
// (Application's hdrFramebuffer_).
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aTexCoord;

out vec2 vTexCoord;

void main() {
    vTexCoord = aTexCoord;
    gl_Position = vec4(aPos.xy, 0.0, 1.0);
}
