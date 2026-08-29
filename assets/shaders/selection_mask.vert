#version 430 core

// Phase 18d: the selection mask pass's vertex stage -- renders ONLY the
// currently-selected entity's mesh (Model::drawDepthOnly(), the same
// minimal-attribute draw path renderShadowPass()'s shadow.vert already uses)
// into selectionMaskFramebuffer_, a small off-screen target this pass's own
// fragment stage (selection_mask.frag) writes a flat 1.0 into wherever a
// fragment survives BOTH depth tests described there. Only aPos (location 0)
// is read -- like shadow.vert, nothing downstream of this pass needs a
// normal/texCoord/tangent, just "is this screen pixel part of the selected
// entity's visible silhouette."
layout(location = 0) in vec3 aPos;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
}
