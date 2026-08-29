#version 430 core

// Phase 18e: the translate gizmo's own fragment stage -- a flat, unlit
// uColor, deliberately not shaded against this scene's directional/point
// lights the way basic.frag/pbr.frag are. Real DCC tools (Blender/Unity/
// Unreal) all draw manipulation-tool overlays this same flat-unlit way: a
// gizmo is UI painted into the 3D view, not a scene object that should
// darken on its own shadowed side or pick up the scene's own light color --
// its whole job is to read as one unambiguous, constant per-axis color
// (X=red/Y=green/Z=blue, this engine's new kGizmoAxisColorX/Y/Z constants,
// application.cpp) regardless of which way the camera or the light happens
// to be facing this frame.
uniform vec3 uColor;

out vec4 FragColor;

void main() {
    FragColor = vec4(uColor, 1.0);
}
