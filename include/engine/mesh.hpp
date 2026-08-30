#ifndef ENGINE_MESH_HPP
#define ENGINE_MESH_HPP

// RAII wrapper around a VAO + VBO + (optional) EBO for one piece of static
// geometry.
//
// Vertex is interleaved position/normal/texCoord/tangent so later phases
// (lighting, texturing) can add their own vertex attribute pointers into the
// same buffer layout without restructuring it. Phases 2-3 only wired up
// `position` as an active vertex attribute (attribute location 0); Phase 4
// wires up `normal` (location 1) and `texCoord` (location 2) too, since
// that's the first phase that actually consumes them (Phong lighting needs
// normals, texture sampling needs UVs). No upload-path changes were needed
// to do this -- the interleaved data was already correct on the GPU, just
// unexposed.
//
// Phase 7a adds `tangent` (location 3): tangent-space normal mapping needs a
// per-vertex tangent to build the TBN matrix in the vertex shader. The
// bitangent is deliberately NOT a separate stored attribute -- it's derived
// in the shader as cross(normal, tangent), which is simpler than carrying a
// fourth interleaved vector and is accurate enough for this engine's meshes
// (no attempt is made to track/store tangent-space handedness separately;
// see basic.vert's comment for the consequence of that simplification).
//
// Move-only for the same reason as Shader: a VAO/VBO/EBO name triple is a
// scarce GL handle set owned by exactly one Mesh, so copying is disabled
// (would let two destructors glDelete* the same names) and move transfers
// ownership, zeroing the moved-from Mesh's handles so its destructor is a
// no-op.

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace engine {

// Phase 13b: a mesh's own bounding volume, computed once from its local-
// space vertex positions at construction time (see Mesh's constructor) and
// stored rather than recomputed every frame -- frustum culling (see
// frustum.hpp) needs this test against a bounding volume for every drawable
// every frame, and a mesh's own vertex data never changes after it's
// uploaded, so there's nothing to invalidate. A sphere (center + radius)
// rather than an AABB: simpler to transform by an arbitrary world matrix
// (see transformed() below) and cheaper to test against a frustum plane,
// and this engine's meshes (boxes, a plane, UV spheres, hand-authored
// scene geometry) don't have such extreme aspect ratios that a sphere's
// extra slack over a tight-fitting AABB would meaningfully hurt culling
// precision.
struct BoundingSphere {
    glm::vec3 center{0.0f};
    float radius = 0.0f;

    // Returns this bounding sphere re-expressed in whatever space
    // `worldTransform` maps local space into -- e.g. an entity's model
    // matrix, or a Model node's accumulated world transform. The center is
    // transformed directly (so translation, rotation, and scale all move it
    // correctly); the radius is scaled by the *largest* of the transform's
    // three axis scale factors (each axis's world-space length), not e.g.
    // their average -- under non-uniform scale that keeps the result a
    // conservative superset of the mesh's real transformed extent along
    // every axis, so frustum culling never discards something that should
    // still be visible (see frustum.hpp's Frustum::intersects()) in
    // exchange for occasionally not culling something it safely could have.
    BoundingSphere transformed(const glm::mat4& worldTransform) const {
        const glm::vec3 axisX(worldTransform[0]);
        const glm::vec3 axisY(worldTransform[1]);
        const glm::vec3 axisZ(worldTransform[2]);
        const float maxScaleSq =
            std::max({glm::dot(axisX, axisX), glm::dot(axisY, axisY), glm::dot(axisZ, axisZ)});
        const glm::vec3 worldCenter = glm::vec3(worldTransform * glm::vec4(center, 1.0f));
        return BoundingSphere{worldCenter, radius * std::sqrt(maxScaleSq)};
    }
};

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    // World/model-space tangent (the direction texCoord.u increases along
    // the surface), used by the vertex shader to build a TBN matrix for
    // normal mapping. Zero-initialized by aggregate-init call sites that
    // don't set it explicitly (any mesh loaded before this phase's tangent
    // plumbing existed); a zero tangent degrades gracefully to a degenerate
    // TBN only if that mesh's material actually has a normal map bound,
    // which none of this engine's pre-existing meshes do.
    glm::vec3 tangent{0.0f};
};

class Mesh {
public:
    // `indices` may be empty, in which case the mesh has no EBO and draw()
    // issues a non-indexed glDrawArrays over all of `vertices` instead.
    Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices = {});
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    // Binds this mesh's VAO (and its EBO, implicitly, since the EBO binding
    // is part of VAO state). Call before draw()/drawRange() or before
    // issuing raw GL draw calls against this mesh.
    void bind() const;

    // Draws the whole mesh: all indices (if indexed) or all vertices
    // (if not).
    void draw() const;

    // Draws `count` indices starting at `indexOffset` (both in index units,
    // not bytes) -- used to issue one draw call per cube face so each face
    // can get its own uniform color. Only valid for an indexed mesh.
    void drawRange(std::size_t indexOffset, std::size_t count) const;

    std::size_t vertexCount() const { return vertexCount_; }
    std::size_t indexCount() const { return indexCount_; }
    bool isIndexed() const { return ebo_ != 0; }

    // Phase 13b: this mesh's local-space bounding sphere, computed once at
    // construction time from `vertices` (see mesh.cpp) -- callers (see
    // Application::render()/Model::drawNode()) transform() it by whatever
    // world matrix applies before testing it against a Frustum.
    const BoundingSphere& boundingSphere() const { return boundingSphere_; }

private:
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    unsigned int ebo_ = 0;
    std::size_t vertexCount_ = 0;
    std::size_t indexCount_ = 0;
    BoundingSphere boundingSphere_;
};

// Builds an axis-aligned unit cube (side length 2 * halfExtent, centered on
// the origin) as 24 vertices (4 per face, each with that face's own outward
// normal and a 0..1 texCoord quad) + a 36-entry EBO (2 triangles per face).
// 4-per-face rather than 8 shared corners because normals differ by face,
// so corners can't actually be shared once normals are real per-vertex
// data. Indices are laid out as 6 contiguous faces of 6 indices each (face
// i occupies indices [i*6, i*6+6)), so Mesh::drawRange(i * 6, 6) still
// draws exactly one face if a caller wants that (Phase 2-3's Application
// used it to give each face its own flat uColor; Phase 4's textured/lit
// cube instead draws the whole mesh in one draw() call, since lighting/
// texturing don't need per-face draw calls).
Mesh makeCube(float halfExtent = 0.5f);

// Phase 7a: a flat XZ-plane quad (a "ground" surface), 2*halfExtent on a
// side, centered at (0, y, 0), with a single upward (+Y) normal and a
// tangent along +X (texCoord.u increases along +X). Built by hand (like
// makeCube()) rather than loaded via Model/Assimp specifically so
// Application has one normal-mapped demo surface it fully controls the
// placement/UV tiling of, independent of scene.obj's own (currently
// normal-map-less) per-mesh materials. uvTiling repeats the 0..1 texCoord
// range across the plane rather than stretching one 0..1 texture across the
// whole surface, so a tiled texture/normal map's own texel-scale pattern
// stays a sensible visual size regardless of how big halfExtent is.
Mesh makeGroundPlane(float halfExtent = 2.5f, float y = 0.0f, float uvTiling = 4.0f);

// Phase 7b: a quad already authored directly in NDC space (2 triangles
// covering [-1,1]x[-1,1] at z = 0), used by the HDR-resolve post-process
// pass (see engine::Framebuffer/Application::render() and
// assets/shaders/postprocess.vert) -- that pass's vertex shader just passes
// aPos.xy straight through as gl_Position.xy with no model/view/projection
// at all, since it operates purely in screen space over an already-rendered
// image, unlike every other mesh/shader pair in this engine. `normal` is
// set to an arbitrary placeholder and `tangent` left at its default zero
// value (see Vertex's own comment on that default) -- postprocess.vert/
// .frag read only position and texCoord, never those two.
Mesh makeFullscreenQuad();

// Phase 9: a standard UV (latitude/longitude) sphere of the given radius,
// centered at the origin, with `latSegments` bands from pole to pole and
// `lonSegments` slices around the equator (each clamped up to a minimum of 3
// so a degenerate 0/1/2-segment request can't produce a broken/empty mesh).
// Built as a (latSegments+1) x (lonSegments+1) vertex grid -- not the
// shared-pole-vertex minimum -- so every vertex, including the ones touching
// the poles, gets its own well-defined (not averaged-together) texCoord: the
// classic UV sphere seam/pole tradeoff, chosen here the same way most
// engines do because it keeps texCoords simple and avoids degenerate shared
// vertices with conflicting UVs.
//
// Parameterization (theta = polar angle from the +Y pole in [0, pi], phi =
// azimuthal angle in [0, 2*pi)):
//   position = radius * (sin(theta)*cos(phi), cos(theta), sin(theta)*sin(phi))
// Since this is a sphere centered on the origin, the outward normal at any
// point is just that point's own direction from the center -- normal =
// normalize(position), computed directly as the unit-sphere position before
// scaling by radius (no separate inverse-transpose normal-matrix step is
// needed at mesh-build time; Application still uploads the model's own
// normal matrix at draw time like every other mesh here).
//
// Tangent (the direction texCoord.u -- i.e. phi/(2*pi) -- increases along the
// surface) is the analytic partial derivative of position with respect to
// phi: d(position)/d(phi) = radius * sin(theta) * (-sin(phi), 0, cos(phi)),
// which normalizes to (-sin(phi), 0, cos(phi)) for any sin(theta) != 0 (i.e.
// everywhere except exactly at the two poles, where the surface's own u
// direction is genuinely undefined -- a single point has no tangent plane --
// so the same formula is used there too purely to avoid a zero/undefined
// vertex attribute; it is never geometrically meaningful at those two
// vertices specifically, matching makeCube()'s "hand-derived, not
// numerically fitted" tangent convention).
Mesh makeUVSphere(int latSegments = 32, int lonSegments = 32, float radius = 1.0f);

// Phase 18e: a simple procedural arrow -- a thin cylindrical shaft plus a
// conical tip -- pointing along local +X from the origin, unit length by
// default (shaftLength + tipLength = 1.0). Used by the translate gizmo's
// three axis handles (see gizmo.hpp/application.cpp's renderGizmo()): one
// shared instance of this mesh is reused for all three axes, each drawn with
// its own model matrix that rotates local +X to point along the world axis
// that handle represents (gizmo.hpp's gizmoAxisDirection()) and scales it by
// that frame's own gizmoAxisLength() (gizmo.hpp), so the caller controls
// both orientation and on-screen size without rebuilding this geometry.
// Deliberately solid triangles (Mesh::draw() only ever issues GL_TRIANGLES,
// see this class's own comment above), not a bare GL_LINES segment -- a
// solid arrow is both easier to see and, since it has real surface area
// rather than a single-pixel-wide line, an easier target for this project's
// own gizmo.hpp hit-testing math to reason about matching what's actually
// rendered. Not meant to be lit (see gizmo.frag's own comment on why the
// pass drawing this is flat/unlit, matching every real DCC tool's own
// manipulation-gizmo convention) -- normals are only approximate (outward-
// radial, ignoring the cone's own slant) and texCoord/tangent are unused
// placeholders (gizmo.vert never reads either), the same "unused attributes
// get an arbitrary placeholder value" precedent makeFullscreenQuad() already
// establishes.
Mesh makeGizmoArrow(float shaftLength = 0.72f, float shaftRadius = 0.018f, float tipLength = 0.28f,
                     float tipRadius = 0.06f, int segments = 10);

// Phase 18j: a simple procedural ring -- a thin flat annulus (a disk with a
// concentric hole through its middle, "washer"-shaped), lying in the local
// Y-Z plane, centered on the origin. Used by the rotate gizmo's three axis
// handles (see gizmo.hpp/application.cpp's renderGizmo()): one shared
// instance of this mesh is reused for all three axes, each drawn with its
// own model matrix that rotates local +X (this ring's own NORMAL -- it lies
// perpendicular to +X by construction) to point along the world axis that
// handle represents (gizmo.hpp's gizmoAxisDirection()), and scales it by
// that frame's own gizmoAxisLength() (gizmo.hpp) -- the exact same rotation/
// scale convention makeGizmoArrow() above already establishes, deliberately
// reused verbatim (not a second one invented) so Application::renderGizmo()
// can draw either tool's geometry through the identical per-axis model-
// matrix loop.
//
// Built as a flat ribbon of quads between two concentric circles (radius -
// thickness/2 and radius + thickness/2), the simplest "keep it simple"
// choice this project's own gizmo geometry already prefers (makeGizmoArrow()
// above isn't a fancy shape either) -- a real 3D torus (a tube extruded
// around the big circle) would need a second, nested ring of segments and
// isn't needed here: like makeGizmoArrow(), this mesh is deliberately solid
// triangles (Mesh::draw() only ever issues GL_TRIANGLES), and like every
// other mesh in this engine, GL_CULL_FACE is never enabled (see makeCube()'s
// own comment), so a single flat ribbon renders correctly from either side
// with no winding-direction concern. Not meant to be lit (gizmo.frag is
// flat/unlit, the identical convention makeGizmoArrow()'s own comment
// documents) -- normal/texCoord/tangent are unused placeholders (gizmo.vert
// only ever reads aPos), the same "unused attributes get an arbitrary
// placeholder value" precedent makeFullscreenQuad()/makeGizmoArrow() already
// establish. `radius` defaults to 1.0 (matching makeGizmoArrow()'s own unit-
// length default) so the identical gizmoAxisLength()-based uniform scale
// that sizes the translate gizmo's arrows sizes this ring's own visible
// radius too.
Mesh makeGizmoRing(float radius = 1.0f, float thickness = 0.035f, int segments = 48);

// Phase 18k: a simple procedural scale-gizmo handle -- a thin cylindrical
// shaft (identical shape/construction to makeGizmoArrow()'s own shaft above)
// plus a small solid CUBE tip in place of that arrow's cone, pointing along
// local +X from the origin, unit length by default (shaftLength + tipSize =
// 1.0, the identical unit-length convention makeGizmoArrow()/makeGizmoRing()
// both already establish) -- a cube tip rather than a cone is the standard
// DCC-tool visual distinction between a move handle and a scale handle (3ds
// Max/Maya/Blender/Unity/Unreal all draw scale handles this way), so a user
// can tell at a glance which tool is active without reading a toolbar label.
// Used by the scale gizmo's three axis handles (see gizmo.hpp/
// application.cpp's renderGizmo()): one shared instance, reused for all
// three axes exactly like gizmoArrowMesh_/gizmoRingMesh_ already are, each
// drawn with its own model matrix that rotates local +X to point along the
// world axis that handle represents (gizmo.hpp's gizmoAxisDirection()) and
// scales it by that frame's own gizmoAxisLength() (gizmo.hpp) -- the
// IDENTICAL rotation table and distance-based scale makeGizmoArrow() already
// establishes, deliberately reused verbatim (not a second one invented) so
// Application::renderGizmo() draws all three tools' geometry through the
// exact same per-axis model-matrix loop.
//
// The cube tip is centered on local +X at x = shaftLength + tipSize/2, with
// side length `tipSize` along all three local axes (a real cube, not a
// squashed box) -- built the same "4 vertices per face, each with that
// face's own outward normal" way makeCube() builds its own faces, just
// offset along +X rather than centered on the origin. Like
// makeGizmoArrow()/makeGizmoRing(), this mesh is deliberately solid
// triangles (Mesh::draw() only ever issues GL_TRIANGLES), not meant to be
// lit (gizmo.frag is flat/unlit, the identical convention both of those
// files' own comments already document) -- texCoord/tangent are unused
// placeholders, the same "unused attributes get an arbitrary placeholder
// value" precedent makeFullscreenQuad()/makeGizmoArrow()/makeGizmoRing()
// already establish. `hitTestGizmoAxes()` (gizmo.hpp) is reused UNMODIFIED
// for this handle's own picking -- see that reuse's own gizmo.hpp comment
// for why a cube-tipped handle is geometrically identical to a cone-tipped
// one for line-segment hit-testing purposes (only the drawn SHAPE differs).
Mesh makeGizmoScaleHandle(float shaftLength = 0.82f, float shaftRadius = 0.018f, float tipSize = 0.18f,
                           int segments = 10);

}  // namespace engine

#endif  // ENGINE_MESH_HPP
