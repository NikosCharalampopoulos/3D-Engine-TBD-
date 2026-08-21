#version 330 core

// Phase 4: full position/normal/texCoord vertex input, now that Mesh wires
// up all three attributes (see mesh.cpp). Model/view/projection are passed
// as three separate matrices instead of Phase 2-3's single combined uMVP,
// because the fragment shader now needs a *world-space* fragment position
// and normal for lighting, not just a clip-space position -- a single
// pre-multiplied MVP can't give the fragment shader that.
//
// Phase 7a adds two things: aTangent (location 3, see mesh.hpp) so the
// fragment shader can build a per-fragment TBN matrix for normal mapping,
// and vFragPosLightSpace (the fragment position re-expressed in the
// directional light's clip space) so the fragment shader can look up the
// shadow map without redoing that matrix multiply per-fragment in the
// fragment stage for every one of potentially many lights.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec3 aTangent;

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
// Light-space "view * projection" for the directional light's shadow pass
// (see engine::ShadowMap / Application::render()) -- built once per frame
// from a fixed light-eye position + an orthographic projection sized to
// cover the test scene, since a directional light has no real position of
// its own to render a shadow map from.
uniform mat4 uLightSpaceMatrix;

out vec3 vNormal;
out vec3 vFragPos;
out vec2 vTexCoord;
// World-space tangent. Deliberately transformed by mat3(uModel) directly,
// NOT uNormalMatrix -- a tangent is an ordinary direction vector embedded in
// the surface (it should stretch/skew along with the geometry under
// non-uniform scale), unlike a normal, which must stay perpendicular to the
// surface and therefore needs the inverse-transpose. Using uNormalMatrix
// here by copy-paste habit would be exactly the same class of subtly-wrong
// shortcut uNormalMatrix itself exists to avoid for normals.
out vec3 vTangent;
out vec4 vFragPosLightSpace;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vFragPos = worldPos.xyz;
    vNormal = normalize(uNormalMatrix * aNormal);
    vTangent = normalize(mat3(uModel) * aTangent);
    vTexCoord = aTexCoord;
    vFragPosLightSpace = uLightSpaceMatrix * worldPos;
    gl_Position = uProjection * uView * worldPos;
}
