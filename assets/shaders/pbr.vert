#version 330 core

// Phase 9: the PBR pass's vertex stage. Identical vertex contract to
// basic.vert (same attribute layout, same uniform names, same varyings) --
// the metallic/roughness Cook-Torrance BRDF this pass drives (see pbr.frag)
// changes only what happens to the per-fragment normal/light vectors once
// they reach the fragment shader, not the geometry/transform pipeline that
// gets them there. Kept as a separate file (not shared with basic.vert)
// so the PBR pipeline can evolve independently later (e.g. Phase 10's IBL
// probably wants extra per-vertex data basic.vert has no reason to carry).
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec3 aTangent;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
// transpose(inverse(mat3(uModel))), computed on the CPU (see
// Application::render()) -- see basic.vert's comment on why mat3(uModel)
// directly would be a subtly-wrong shortcut under non-uniform scale.
uniform mat3 uNormalMatrix;

out vec3 vNormal;
out vec3 vFragPos;
out vec2 vTexCoord;
// World-space tangent, transformed by plain mat3(uModel) (not uNormalMatrix)
// for the same reason basic.vert does -- a tangent is an ordinary direction
// embedded in the surface, not a "must stay perpendicular" quantity.
out vec3 vTangent;
// Phase 13c: replaces the old single vFragPosLightSpace varying -- CSM's
// fragment shader picks one of several cascades' light-space matrices per
// fragment (based on view-space depth), computing
// uLightSpaceMatrices[cascadeIndex] * vec4(vFragPos, 1.0) itself using
// vFragPos (already provided above) rather than the vertex shader
// pre-computing every candidate cascade's light-space position up front --
// see basic.vert's identical comment (this shader's vertex contract is
// otherwise unchanged from it) for why that's exactly equivalent to the old
// per-vertex computation.
out float vViewSpaceDepth;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vFragPos = worldPos.xyz;
    vNormal = normalize(uNormalMatrix * aNormal);
    vTangent = normalize(mat3(uModel) * aTangent);
    vTexCoord = aTexCoord;
    vViewSpaceDepth = -(uView * worldPos).z;
    gl_Position = uProjection * uView * worldPos;
}
