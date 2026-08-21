#include "engine/mesh.hpp"

#include <glad/glad.h>

#include <cassert>
#include <cstddef>

#include "engine/gl_debug.hpp"

namespace engine {

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices)
    : vertexCount_(vertices.size()), indexCount_(indices.size()) {
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

    // Attribute 0: position only, this phase. Stride spans the whole
    // interleaved Vertex (not just vec3) so normal/texCoord already sit in
    // the buffer at their final offsets, ready for a later phase to add
    // attributes 1/2 without re-uploading or restructuring anything.
    GL_CHECK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                    reinterpret_cast<void*>(offsetof(Vertex, position))));
    GL_CHECK(glEnableVertexAttribArray(0));

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
    // normal and a standard 0..1 texCoord quad. Winding correctness has no
    // visible effect this phase since GL_CULL_FACE is never enabled here.
    const std::vector<Vertex> vertices = {
        // +Z (front)
        {{-h, -h, h}, {0, 0, 1}, {0, 0}}, {{h, -h, h}, {0, 0, 1}, {1, 0}},
        {{h, h, h}, {0, 0, 1}, {1, 1}},   {{-h, h, h}, {0, 0, 1}, {0, 1}},
        // -Z (back)
        {{h, -h, -h}, {0, 0, -1}, {0, 0}}, {{-h, -h, -h}, {0, 0, -1}, {1, 0}},
        {{-h, h, -h}, {0, 0, -1}, {1, 1}}, {{h, h, -h}, {0, 0, -1}, {0, 1}},
        // +X (right)
        {{h, -h, h}, {1, 0, 0}, {0, 0}},  {{h, -h, -h}, {1, 0, 0}, {1, 0}},
        {{h, h, -h}, {1, 0, 0}, {1, 1}},  {{h, h, h}, {1, 0, 0}, {0, 1}},
        // -X (left)
        {{-h, -h, -h}, {-1, 0, 0}, {0, 0}}, {{-h, -h, h}, {-1, 0, 0}, {1, 0}},
        {{-h, h, h}, {-1, 0, 0}, {1, 1}},   {{-h, h, -h}, {-1, 0, 0}, {0, 1}},
        // +Y (top)
        {{-h, h, h}, {0, 1, 0}, {0, 0}},  {{h, h, h}, {0, 1, 0}, {1, 0}},
        {{h, h, -h}, {0, 1, 0}, {1, 1}},  {{-h, h, -h}, {0, 1, 0}, {0, 1}},
        // -Y (bottom)
        {{-h, -h, -h}, {0, -1, 0}, {0, 0}}, {{h, -h, -h}, {0, -1, 0}, {1, 0}},
        {{h, -h, h}, {0, -1, 0}, {1, 1}},   {{-h, -h, h}, {0, -1, 0}, {0, 1}},
    };

    std::vector<unsigned int> indices;
    indices.reserve(36);
    for (unsigned int face = 0; face < 6; ++face) {
        const unsigned int base = face * 4;
        indices.insert(indices.end(), {base + 0, base + 1, base + 2, base + 2, base + 3, base + 0});
    }

    return Mesh(vertices, indices);
}

}  // namespace engine
