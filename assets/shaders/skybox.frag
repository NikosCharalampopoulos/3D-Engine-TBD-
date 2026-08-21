#version 330 core

// Phase 7b: samples engine::Skybox's cubemap along the interpolated
// per-fragment direction vector -- see skybox.vert's comment on why the
// cube's own local position doubles as that direction.
in vec3 vDirection;

out vec4 FragColor;

uniform samplerCube uSkybox;

void main() {
    FragColor = texture(uSkybox, vDirection);
}
