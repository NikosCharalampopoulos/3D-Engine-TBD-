#version 330 core

// Phase 7a: the shadow-map depth pass's fragment stage. Deliberately empty
// -- the depth-only FBO this program renders into (see engine::ShadowMap)
// has no color attachment at all (glDrawBuffer(GL_NONE)), so there is
// nothing for a fragment stage to usefully write. A program still needs a
// fragment shader to link on this engine's GL 3.3 core target, so this is
// the smallest one that does: gl_FragDepth defaults to gl_FragCoord.z,
// which is exactly the depth this pass wants written.
void main() {
}
