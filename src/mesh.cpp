#include "engine/mesh.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>

#include "engine/gl_debug.hpp"

namespace engine {

namespace {

// Phase 13b: computes a mesh's local-space bounding sphere from its own
// vertex positions -- center is the axis-aligned bounding box's center (not
// a centroid, which would be pulled towards wherever vertices happen to be
// denser rather than towards the shape's actual middle), and radius is the
// farthest any vertex actually sits from that center, so every vertex is
// guaranteed to lie within the resulting sphere. This is a correct (if not
// minimal -- finding the smallest enclosing sphere is a harder problem this
// engine doesn't need) bounding sphere: for frustum culling, "definitely
// contains the whole mesh" matters far more than "as tight as possible".
BoundingSphere computeBoundingSphere(const std::vector<Vertex>& vertices) {
    if (vertices.empty()) {
        // No vertices means nothing to bound; a zero-radius sphere at the
        // origin is a harmless placeholder (this mesh has no geometry to
        // draw anyway) rather than reading vertices.front() below.
        return BoundingSphere{};
    }

    glm::vec3 minExtent = vertices.front().position;
    glm::vec3 maxExtent = vertices.front().position;
    for (const Vertex& vertex : vertices) {
        minExtent = glm::min(minExtent, vertex.position);
        maxExtent = glm::max(maxExtent, vertex.position);
    }
    const glm::vec3 center = (minExtent + maxExtent) * 0.5f;

    float radius = 0.0f;
    for (const Vertex& vertex : vertices) {
        radius = std::max(radius, glm::length(vertex.position - center));
    }
    return BoundingSphere{center, radius};
}

}  // namespace

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices)
    : vertexCount_(vertices.size()),
      indexCount_(indices.size()),
      boundingSphere_(computeBoundingSphere(vertices)) {
    GL_CHECK(glGenVertexArrays(1, &vao_));
    GL_CHECK(glGenBuffers(1, &vbo_));
    GL_CHECK(glBindVertexArray(vao_));

    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, vbo_));
    GL_CHECK(glBufferData(GL_ARRAY_BUFFER,
                           static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                           vertices.data(), GL_STATIC_DRAW));

    if (!indices.empty()) {
        GL_CHECK(glGenBuffers(1, &ebo_));
        GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_));
        GL_CHECK(glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                               static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
                               indices.data(), GL_STATIC_DRAW));
    }

    // Attribute 0: position. Attributes 1/2 (normal, texCoord) were already
    // present in the interleaved buffer since Phase 2 but unwired until
    // Phase 4, which is the first phase that actually consumes them
    // (lighting needs normals, texturing needs UVs). Stride spans the whole
    // interleaved Vertex in all three so each attribute lands at its real
    // offset within one shared buffer.
    GL_CHECK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                    reinterpret_cast<void*>(offsetof(Vertex, position))));
    GL_CHECK(glEnableVertexAttribArray(0));

    GL_CHECK(glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                    reinterpret_cast<void*>(offsetof(Vertex, normal))));
    GL_CHECK(glEnableVertexAttribArray(1));

    GL_CHECK(glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                    reinterpret_cast<void*>(offsetof(Vertex, texCoord))));
    GL_CHECK(glEnableVertexAttribArray(2));

    // Attribute 3: tangent (Phase 7a, normal mapping) -- same interleaved
    // buffer, wired up unconditionally like normal/texCoord above even
    // though not every mesh's material actually has a normal map; the cost
    // of one extra always-active vertex attribute is negligible and this
    // avoids a second Mesh code path.
    GL_CHECK(glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                    reinterpret_cast<void*>(offsetof(Vertex, tangent))));
    GL_CHECK(glEnableVertexAttribArray(3));

    // Unbind the VAO first so the EBO binding (which is part of VAO state)
    // stays captured; only then unbind the array buffer, which is global
    // state and safe to release now that attribute 0 has been configured.
    GL_CHECK(glBindVertexArray(0));
    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, 0));
}

Mesh::~Mesh() {
    if (ebo_ != 0) {
        glDeleteBuffers(1, &ebo_);
    }
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
    }
}

Mesh::Mesh(Mesh&& other) noexcept
    : vao_(other.vao_),
      vbo_(other.vbo_),
      ebo_(other.ebo_),
      vertexCount_(other.vertexCount_),
      indexCount_(other.indexCount_) {
    other.vao_ = 0;
    other.vbo_ = 0;
    other.ebo_ = 0;
    other.vertexCount_ = 0;
    other.indexCount_ = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        if (ebo_ != 0) {
            glDeleteBuffers(1, &ebo_);
        }
        if (vbo_ != 0) {
            glDeleteBuffers(1, &vbo_);
        }
        if (vao_ != 0) {
            glDeleteVertexArrays(1, &vao_);
        }

        vao_ = other.vao_;
        vbo_ = other.vbo_;
        ebo_ = other.ebo_;
        vertexCount_ = other.vertexCount_;
        indexCount_ = other.indexCount_;

        other.vao_ = 0;
        other.vbo_ = 0;
        other.ebo_ = 0;
        other.vertexCount_ = 0;
        other.indexCount_ = 0;
    }
    return *this;
}

void Mesh::bind() const {
    GL_CHECK(glBindVertexArray(vao_));
}

void Mesh::draw() const {
    if (ebo_ != 0) {
        GL_CHECK(glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount_), GL_UNSIGNED_INT, nullptr));
    } else {
        GL_CHECK(glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertexCount_)));
    }
}

void Mesh::drawRange(std::size_t indexOffset, std::size_t count) const {
    assert(ebo_ != 0 && "drawRange() requires an indexed mesh");
    const auto offsetBytes = reinterpret_cast<void*>(indexOffset * sizeof(unsigned int));
    GL_CHECK(glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT, offsetBytes));
}

Mesh makeCube(float halfExtent) {
    const float h = halfExtent;

    // Each face lists its 4 corners counter-clockwise as seen from outside
    // the cube (so the default GL front-face winding matches, in case a
    // later phase enables back-face culling) along with that face's outward
    // normal, a standard 0..1 texCoord quad, and (Phase 7a) that face's
    // tangent -- the direction texCoord.u increases in world space, derived
    // by hand from each face's own vertex order/UV layout below (e.g. +Z's
    // u goes 0->1 from vertex 0 to vertex 1, a +X displacement, so its
    // tangent is (1,0,0)). Winding correctness has no visible effect this
    // phase since GL_CULL_FACE is never enabled here.
    const std::vector<Vertex> vertices = {
        // +Z (front): tangent +X
        {{-h, -h, h}, {0, 0, 1}, {0, 0}, {1, 0, 0}}, {{h, -h, h}, {0, 0, 1}, {1, 0}, {1, 0, 0}},
        {{h, h, h}, {0, 0, 1}, {1, 1}, {1, 0, 0}},   {{-h, h, h}, {0, 0, 1}, {0, 1}, {1, 0, 0}},
        // -Z (back): tangent -X
        {{h, -h, -h}, {0, 0, -1}, {0, 0}, {-1, 0, 0}}, {{-h, -h, -h}, {0, 0, -1}, {1, 0}, {-1, 0, 0}},
        {{-h, h, -h}, {0, 0, -1}, {1, 1}, {-1, 0, 0}}, {{h, h, -h}, {0, 0, -1}, {0, 1}, {-1, 0, 0}},
        // +X (right): tangent -Z
        {{h, -h, h}, {1, 0, 0}, {0, 0}, {0, 0, -1}},  {{h, -h, -h}, {1, 0, 0}, {1, 0}, {0, 0, -1}},
        {{h, h, -h}, {1, 0, 0}, {1, 1}, {0, 0, -1}},  {{h, h, h}, {1, 0, 0}, {0, 1}, {0, 0, -1}},
        // -X (left): tangent +Z
        {{-h, -h, -h}, {-1, 0, 0}, {0, 0}, {0, 0, 1}}, {{-h, -h, h}, {-1, 0, 0}, {1, 0}, {0, 0, 1}},
        {{-h, h, h}, {-1, 0, 0}, {1, 1}, {0, 0, 1}},   {{-h, h, -h}, {-1, 0, 0}, {0, 1}, {0, 0, 1}},
        // +Y (top): tangent +X
        {{-h, h, h}, {0, 1, 0}, {0, 0}, {1, 0, 0}},  {{h, h, h}, {0, 1, 0}, {1, 0}, {1, 0, 0}},
        {{h, h, -h}, {0, 1, 0}, {1, 1}, {1, 0, 0}},  {{-h, h, -h}, {0, 1, 0}, {0, 1}, {1, 0, 0}},
        // -Y (bottom): tangent +X
        {{-h, -h, -h}, {0, -1, 0}, {0, 0}, {1, 0, 0}}, {{h, -h, -h}, {0, -1, 0}, {1, 0}, {1, 0, 0}},
        {{h, -h, h}, {0, -1, 0}, {1, 1}, {1, 0, 0}},   {{-h, -h, h}, {0, -1, 0}, {0, 1}, {1, 0, 0}},
    };

    std::vector<unsigned int> indices;
    indices.reserve(36);
    for (unsigned int face = 0; face < 6; ++face) {
        const unsigned int base = face * 4;
        indices.insert(indices.end(), {base + 0, base + 1, base + 2, base + 2, base + 3, base + 0});
    }

    return Mesh(vertices, indices);
}

Mesh makeGroundPlane(float halfExtent, float y, float uvTiling) {
    const float h = halfExtent;
    const float t = uvTiling;

    // A single upward-facing quad; texCoord.u increases along +X (matching
    // this face's tangent, (1, 0, 0)) and texCoord.v increases along +Z.
    // Winding (like makeCube()'s) doesn't matter for visibility since
    // GL_CULL_FACE is never enabled in this engine.
    const std::vector<Vertex> vertices = {
        {{-h, y, -h}, {0, 1, 0}, {0, 0}, {1, 0, 0}},
        {{h, y, -h}, {0, 1, 0}, {t, 0}, {1, 0, 0}},
        {{h, y, h}, {0, 1, 0}, {t, t}, {1, 0, 0}},
        {{-h, y, h}, {0, 1, 0}, {0, t}, {1, 0, 0}},
    };
    const std::vector<unsigned int> indices = {0, 1, 2, 2, 3, 0};

    return Mesh(vertices, indices);
}

Mesh makeFullscreenQuad() {
    // Authored directly in NDC (not model/world space, unlike every other
    // mesh this file builds) -- see this function's header comment.
    // normal/tangent are unused placeholders (postprocess.vert/.frag never
    // read attributes 1/3).
    const std::vector<Vertex> vertices = {
        {{-1.0f, -1.0f, 0.0f}, {0, 0, 1}, {0.0f, 0.0f}},
        {{1.0f, -1.0f, 0.0f}, {0, 0, 1}, {1.0f, 0.0f}},
        {{1.0f, 1.0f, 0.0f}, {0, 0, 1}, {1.0f, 1.0f}},
        {{-1.0f, 1.0f, 0.0f}, {0, 0, 1}, {0.0f, 1.0f}},
    };
    const std::vector<unsigned int> indices = {0, 1, 2, 2, 3, 0};

    return Mesh(vertices, indices);
}

Mesh makeUVSphere(int latSegments, int lonSegments, float radius) {
    // A 1- or 2-segment sphere isn't a meaningful mesh (it'd have degenerate
    // or zero-area quads); clamp to the smallest values that still produce a
    // real closed surface rather than letting a caller silently build a
    // broken/empty one.
    latSegments = std::max(latSegments, 3);
    lonSegments = std::max(lonSegments, 3);

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.0f * kPi;

    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<std::size_t>((latSegments + 1) * (lonSegments + 1)));

    for (int i = 0; i <= latSegments; ++i) {
        // v (and theta) sweep top pole (i = 0) to bottom pole (i =
        // latSegments) -- see this function's header comment for the
        // parameterization.
        const float v = static_cast<float>(i) / static_cast<float>(latSegments);
        const float theta = v * kPi;
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);

        for (int j = 0; j <= lonSegments; ++j) {
            const float u = static_cast<float>(j) / static_cast<float>(lonSegments);
            const float phi = u * kTwoPi;
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);

            // Already unit length (sin/cos of the same two angles combine to
            // exactly 1 up to floating-point rounding), so this doubles as
            // both the outward normal and the unit-sphere position that
            // `radius` scales below.
            const glm::vec3 unitPos(sinTheta * cosPhi, cosTheta, sinTheta * sinPhi);
            const glm::vec3 tangent(-sinPhi, 0.0f, cosPhi);

            vertices.push_back({unitPos * radius, unitPos, glm::vec2(u, v), tangent});
        }
    }

    std::vector<unsigned int> indices;
    indices.reserve(static_cast<std::size_t>(latSegments) * static_cast<std::size_t>(lonSegments) * 6);
    for (int i = 0; i < latSegments; ++i) {
        for (int j = 0; j < lonSegments; ++j) {
            const unsigned int row1 = static_cast<unsigned int>(i * (lonSegments + 1));
            const unsigned int row2 = static_cast<unsigned int>((i + 1) * (lonSegments + 1));
            const unsigned int a = row1 + static_cast<unsigned int>(j);
            const unsigned int b = row1 + static_cast<unsigned int>(j) + 1;
            const unsigned int c = row2 + static_cast<unsigned int>(j) + 1;
            const unsigned int d = row2 + static_cast<unsigned int>(j);

            indices.insert(indices.end(), {a, b, c, c, d, a});
        }
    }

    return Mesh(vertices, indices);
}

}  // namespace engine
