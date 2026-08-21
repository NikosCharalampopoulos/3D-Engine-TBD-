#version 330 core

// Phase 2: position-only vertex input. `uMVP` is a single combined
// model-view-projection matrix -- there's no real Camera yet (that's
// Phase 3), so Application builds this matrix by hand each frame from a
// hardcoded view + perspective projection. Once Phase 3 lands, this shader
// doesn't need to change at all; only how uMVP gets computed on the CPU
// side does.
layout(location = 0) in vec3 aPos;

uniform mat4 uMVP;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
