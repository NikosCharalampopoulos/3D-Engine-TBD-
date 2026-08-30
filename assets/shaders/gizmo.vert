#version 430 core

// Phase 18e: the translate gizmo's own vertex stage -- draws
// mesh.hpp's makeGizmoArrow() (a shaft + cone tip pointing along local +X)
// at the selected entity's world position, one draw call per axis with a
// different uModel (rotating the same shared +X-pointing mesh to point
// along the axis it represents) and uColor. Only aPos (location 0) is read
// -- like shadow.vert/selection_mask.vert, this pass needs no lighting at
// all, just placement (see gizmo.frag's own comment on why flat/unlit is the
// right choice for a manipulation-tool overlay, not a lit scene object).
layout(location = 0) in vec3 aPos;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
}
