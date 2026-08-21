#version 330 core

// Phase 4: full position/normal/texCoord vertex input, now that Mesh wires
// up all three attributes (see mesh.cpp). Model/view/projection are passed
// as three separate matrices instead of Phase 2-3's single combined uMVP,
// because the fragment shader now needs a *world-space* fragment position
// and normal for lighting, not just a clip-space position -- a single
// pre-multiplied MVP can't give the fragment shader that.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
// The normal matrix is transpose(inverse(mat3(uModel))), computed on the
// CPU side (Application::render()) and uploaded as its own uniform rather
// than recomputed as mat3(uModel) here. Using mat3(uModel) directly is a
// classic subtly-wrong shortcut: it's only correct for rotation/uniform-
// scale/translation, and silently skews normals under non-uniform scale
// (which this engine may apply to meshes in later phases). Doing the
// transpose-inverse once per draw call on the CPU is cheap and always
// correct, so there's no reason to take the shortcut even though this
// phase's cube itself never scales non-uniformly.
uniform mat3 uNormalMatrix;

out vec3 vNormal;
out vec3 vFragPos;
out vec2 vTexCoord;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vFragPos = worldPos.xyz;
    vNormal = normalize(uNormalMatrix * aNormal);
    vTexCoord = aTexCoord;
    gl_Position = uProjection * uView * worldPos;
}
