#version 330 core

// Phase 7b: renders engine::Skybox as the scene's background. aPos is one
// of makeCube()'s plain unit-cube corners -- only its position attribute is
// read here; makeCube() also wires up normal/texCoord/tangent on this same
// mesh (Mesh always enables all four), but this shader simply never
// touches them.
layout(location = 0) in vec3 aPos;

// uView is the camera's real view matrix with translation stripped (mat3
// truncation, re-embedded in a mat4 -- see engine::Skybox::draw()), so the
// sky rotates with the camera but never translates with it: the cube stays
// centered on the viewer no matter where the camera actually is in world
// space, which is what makes an infinitely-distant background believable
// with a finite unit cube.
uniform mat4 uView;
uniform mat4 uProjection;

// The cube is centered on the origin, so any point on its surface, read as
// a vector from the origin, already points in exactly the direction that
// corner represents -- that's the whole cubemap sampling direction, no
// separate per-vertex direction attribute needed.
out vec3 vDirection;

void main() {
    vDirection = aPos;
    vec4 clipPos = uProjection * uView * vec4(aPos, 1.0);
    // Forces this vertex's post-perspective-divide depth to exactly 1.0
    // (the far plane) regardless of the cube's actual clip-space z, since
    // gl_Position.z / gl_Position.w == 1.0 whenever z is set equal to w.
    // Combined with glDepthFunc(GL_LEQUAL) (see Skybox::draw()), this makes
    // every skybox fragment depth-test as "as far away as possible", so it
    // only shows through pixels nothing else was drawn to this frame,
    // without the caller ever needing to disable depth testing.
    gl_Position = clipPos.xyww;
}
