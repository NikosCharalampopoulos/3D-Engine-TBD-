#version 330 core

// Flat per-draw-call color: Application issues one draw call per cube face
// and sets `uColor` before each, so every face renders as a distinct solid
// color -- proof that the shader+buffer pipeline (and depth testing) work,
// without needing lighting/normals wired up yet.
out vec4 FragColor;

uniform vec3 uColor;

void main() {
    FragColor = vec4(uColor, 1.0);
}
