#ifndef ENGINE_MESH_HPP
#define ENGINE_MESH_HPP

// RAII wrapper around a VAO + VBO + (optional) EBO for one piece of static
// geometry.
//
// Vertex is interleaved position/normal/texCoord so later phases (lighting,
// texturing) can add their own vertex attribute pointers into the same
// buffer layout without restructuring it. Phases 2-3 only wired up
// `position` as an active vertex attribute (attribute location 0); Phase 4
// wires up `normal` (location 1) and `texCoord` (location 2) too, since
// that's the first phase that actually consumes them (Phong lighting needs
// normals, texture sampling needs UVs). No upload-path changes were needed
// to do this -- the interleaved data was already correct on the GPU, just
// unexposed.
//
// Move-only for the same reason as Shader: a VAO/VBO/EBO name triple is a
// scarce GL handle set owned by exactly one Mesh, so copying is disabled
// (would let two destructors glDelete* the same names) and move transfers
// ownership, zeroing the moved-from Mesh's handles so its destructor is a
// no-op.

#include <glm/glm.hpp>

#include <cstddef>
#include <vector>

namespace engine {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
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

private:
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    unsigned int ebo_ = 0;
    std::size_t vertexCount_ = 0;
    std::size_t indexCount_ = 0;
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

}  // namespace engine

#endif  // ENGINE_MESH_HPP
