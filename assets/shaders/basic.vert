#version 430 core

// Phase 13d: bumped from #version 330 core to 430 core purely to match
// basic.frag's own version bump (SSBO support, needed for clustered
// lighting's per-cluster light list -- see that file's comment); a linked
// program's stages don't strictly have to share one #version, but keeping
// them matched avoids depending on driver-specific leniency about mixed
// versions within one program. Nothing in this file's own language usage
// changes behavior between 330 and 430 core.
//
// Phase 4: full position/normal/texCoord vertex input, now that Mesh wires
// up all three attributes (see mesh.cpp). Model/view/projection are passed
// as three separate matrices instead of Phase 2-3's single combined uMVP,
// because the fragment shader now needs a *world-space* fragment position
// and normal for lighting, not just a clip-space position -- a single
// pre-multiplied MVP can't give the fragment shader that.
//
// Phase 7a adds two things: aTangent (location 3, see mesh.hpp) so the
// fragment shader can build a per-fragment TBN matrix for normal mapping,
// and (originally) vFragPosLightSpace, the fragment position re-expressed
// in the directional light's clip space.
//
// Phase 13c (Cascaded Shadow Maps) replaces that single vFragPosLightSpace
// varying with vViewSpaceDepth below: CSM's fragment shader must first pick
// *which* of several cascades' light-space matrices applies to a given
// fragment (based on that fragment's view-space depth -- see basic.frag),
// so pre-computing one single light-space position here no longer makes
// sense -- there are now several candidates, and only one is ever actually
// needed per fragment. basic.frag instead computes
// uLightSpaceMatrices[cascadeIndex] * vec4(vFragPos, 1.0) itself once it
// knows cascadeIndex, using vFragPos (already provided below) -- this is
// exactly equivalent to interpolating a vertex-computed
// uLightSpaceMatrix * worldPos across the triangle the way the old single-
// shadow-map version did (both are just an affine transform of an
// interpolated world position), just deferred until the fragment shader
// knows which matrix to apply.
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
// Phase 13c: replaces vFragPosLightSpace -- see this file's own Phase 13c
// comment above. View-space Z is negative in front of the camera (OpenGL's
// usual "camera looks down -Z" convention) and grows more negative with
// distance; negating it here makes vViewSpaceDepth a plain positive
// "distance along the view direction" value, directly comparable against
// basic.frag's uCascadeSplits[] with no sign-juggling on that end.
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
