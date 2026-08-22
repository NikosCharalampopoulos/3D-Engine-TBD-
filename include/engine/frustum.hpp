#ifndef ENGINE_FRUSTUM_HPP
#define ENGINE_FRUSTUM_HPP

// Phase 13b: the camera's view frustum, expressed as 6 planes extracted
// directly from a combined view-projection matrix -- the standard
// Gribb/Hartmann method (Gribb & Hartmann, "Fast Extraction of Viewing
// Frustum Planes from the World-View-Projection Matrix", 2001). Used by
// Application::render() to skip draw calls for geometry that's provably
// entirely outside the camera's view, rather than rasterizing it only to
// have every fragment fail the viewport/clip test.
//
// Deliberately a tiny, self-contained header-only class (like transform.hpp/
// pbr_material.hpp) -- one matrix in, 6 planes out, one bounding-sphere test
// -- rather than a general computational-geometry module. AABB-vs-frustum or
// OBB-vs-frustum tests are not provided because nothing in this engine's
// bounding-volume representation (Mesh::boundingSphere(), see mesh.hpp) needs
// them yet; add them here if a later phase's bounding volume needs them.
//
// Why a bounding sphere (not an AABB) is what this pairs with: a sphere is
// rotation-invariant, so testing a mesh's *local-space* bounding sphere
// transformed into world space (mesh.hpp's BoundingSphere::transformed())
// only ever needs the transform's translation + largest axis scale factor,
// never its rotation -- an AABB would instead need re-deriving a new
// axis-aligned box from the 8 rotated corners every frame (or accept a
// looser, over-conservative box) to stay correct under rotation. This
// engine's meshes are simple, roughly sphere-ish or box-ish primitives/
// assets where a sphere's extra slack over a tight AABB costs little culling
// precision in exchange for that simplicity.

#include <glm/glm.hpp>

#include <array>
#include <cstddef>

namespace engine {

// One frustum plane in the form dot(normal, point) + d == 0, with `normal`
// pointing *into* the frustum (towards its interior) -- so for any point
// actually inside the frustum, signedDistance(point) is >= 0, and it's more
// negative the farther outside that plane the point lies. This sign
// convention (rather than the reverse) is what lets intersects() below use a
// single "is every plane's signed distance >= -radius" test with no special-
// casing per plane.
struct FrustumPlane {
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
    float d = 0.0f;

    float signedDistance(const glm::vec3& point) const { return glm::dot(normal, point) + d; }
};

// Running total/culled counts for one frame's worth of frustum tests,
// accumulated across every drawable (Model's per-mesh draws, the ground
// plane, the PBR sphere grid) so Application::render() can log one combined
// "N/M culled" line per frame instead of each call site logging its own
// piece. Plain data, reset by the caller (Application::render() constructs a
// fresh one each frame) rather than owning any reset logic itself.
struct CullStats {
    std::size_t totalDrawables = 0;
    std::size_t culledDrawables = 0;
};

class Frustum {
public:
    Frustum() = default;
    explicit Frustum(const glm::mat4& viewProjection) { update(viewProjection); }

    // Re-extracts all 6 planes from a fresh view-projection matrix (e.g.
    // `camera.getProjectionMatrix(aspect) * camera.getViewMatrix()`,
    // recomputed once per frame in Application::render() -- see this
    // engine's Camera comment on why view/projection are never cached).
    //
    // Gribb/Hartmann derivation: for a point p with clip-space coordinates
    // clip = viewProjection * vec4(p, 1), OpenGL's normalized-device-
    // coordinate frustum is exactly -clip.w <= clip.{x,y,z} <= clip.w. Each
    // of those 6 inequalities, rearranged to a "distance >= 0 means inside"
    // form, is a fixed linear combination of viewProjection's own rows: e.g.
    // clip.x + clip.w >= 0 (the left-plane inequality) expands to
    // dot(row0 + row3, vec4(p, 1)) >= 0, i.e. the left plane's own (A, B, C,
    // D) coefficients are simply row0 + row3. The other 5 planes follow the
    // same pattern (row3 -/+ row0/row1/row2) -- this is the well-known
    // closed-form result the Gribb/Hartmann paper derives, not something
    // re-derived from scratch here, but the row extraction below is worth
    // getting right: glm::mat4 is column-major and indexed m[col][row], so
    // "row i" of the mathematical matrix is (m[0][i], m[1][i], m[2][i],
    // m[3][i]) -- reading straight across m[i] would silently grab a column
    // instead and produce a subtly-wrong (not obviously broken) frustum.
    void update(const glm::mat4& m) {
        const glm::vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
        const glm::vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
        const glm::vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
        const glm::vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);

        setPlane(planes_[0], row3 + row0);  // left:   clip.x + clip.w >= 0
        setPlane(planes_[1], row3 - row0);  // right:  clip.w - clip.x >= 0
        setPlane(planes_[2], row3 + row1);  // bottom: clip.y + clip.w >= 0
        setPlane(planes_[3], row3 - row1);  // top:    clip.w - clip.y >= 0
        setPlane(planes_[4], row3 + row2);  // near:   clip.z + clip.w >= 0
        setPlane(planes_[5], row3 - row2);  // far:    clip.w - clip.z >= 0
    }

    // True if the sphere (world-space center/radius) is inside or merely
    // intersecting the frustum; false only if it's provably entirely outside
    // at least one plane. This is the standard conservative sphere-frustum
    // test: a sphere can be wrongly kept (e.g. one that clears every
    // individual plane but actually sits just outside a frustum corner,
    // where two planes' "outside" regions overlap) but is never wrongly
    // discarded, which is exactly the safe direction to be conservative in
    // for culling -- an occasional unculled-but-invisible sphere costs a
    // wasted draw call, while a wrongly-culled visible one is a rendering
    // correctness bug.
    bool intersects(const glm::vec3& center, float radius) const {
        for (const FrustumPlane& plane : planes_) {
            if (plane.signedDistance(center) < -radius) {
                return false;
            }
        }
        return true;
    }

private:
    // `coeffs` is one plane's (A, B, C, D) in un-normalized form (a row-sum/
    // difference of viewProjection's rows, see update()'s comment) -- (A, B,
    // C) is normalized to unit length here (and D scaled by the same factor)
    // so signedDistance() above returns a true world-space distance, not an
    // arbitrarily-scaled quantity the `radius` comparison couldn't be
    // compared against directly.
    static void setPlane(FrustumPlane& plane, const glm::vec4& coeffs) {
        const glm::vec3 normal(coeffs.x, coeffs.y, coeffs.z);
        const float length = glm::length(normal);
        // A real perspective or orthographic projection combined with a real
        // (non-degenerate) view matrix never produces a near-zero-length
        // plane normal here -- guarded anyway (rather than dividing by ~0
        // and propagating NaN into every subsequent intersects() call this
        // frame) the same defensive way Camera::updateVectors() guards its
        // own cross product, cheap insurance against a call site ever
        // passing in a degenerate matrix.
        if (length < 1e-8f) {
            plane.normal = glm::vec3(0.0f, 0.0f, 1.0f);
            plane.d = 0.0f;
            return;
        }
        plane.normal = normal / length;
        plane.d = coeffs.w / length;
    }

    std::array<FrustumPlane, 6> planes_{};
};

// Phase 13c: unprojects the 8 corners of the canonical OpenGL NDC cube
// ([-1,1]^3) through the inverse of a view-projection matrix, recovering
// that frustum's 8 world-space corners. Added for CSM's per-cascade
// frustum-fitting (application.cpp's computeCascades()): each cascade needs
// the world-space corners of just its own [near_i, far_i) depth slice of
// the camera's full frustum (built by passing a projection matrix whose
// near/far are that slice's bounds -- see Camera::getProjectionMatrix's
// 3-argument overload), not the 6 planes Frustum itself extracts. A free
// function alongside Frustum rather than a Frustum method/member: Frustum's
// own intersects() test only ever needs planes, never corners, so keeping
// this separate avoids growing Frustum's own stored state (it would need to
// cache the source matrix, not just the derived planes) for a need only CSM
// has.
//
// Corner order is not meaningful to any caller (every corner is min/maxed
// over, never indexed by which face/corner it came from), so this simply
// walks x/y/z in nested-loop order rather than a "named corner" convention.
inline std::array<glm::vec3, 8> frustumCornersWorldSpace(const glm::mat4& viewProjection) {
    const glm::mat4 inverseViewProjection = glm::inverse(viewProjection);
    std::array<glm::vec3, 8> corners{};
    std::size_t index = 0;
    for (int ix = 0; ix < 2; ++ix) {
        for (int iy = 0; iy < 2; ++iy) {
            for (int iz = 0; iz < 2; ++iz) {
                const glm::vec4 ndc(2.0f * static_cast<float>(ix) - 1.0f, 2.0f * static_cast<float>(iy) - 1.0f,
                                     2.0f * static_cast<float>(iz) - 1.0f, 1.0f);
                glm::vec4 worldPos = inverseViewProjection * ndc;
                // Perspective-divide: a perspective projection's inverse
                // does not return w == 1 the way an orthographic one would.
                worldPos /= worldPos.w;
                corners[index++] = glm::vec3(worldPos);
            }
        }
    }
    return corners;
}

}  // namespace engine

#endif  // ENGINE_FRUSTUM_HPP
