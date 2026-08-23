#!/usr/bin/env python3
"""Phase 14f: generates assets/models/sphere.obj and assets/models/plane.obj
-- the two new mesh assets the editor's Create menu's "Sphere"/"Plane" items
load through the ordinary Model(path, shader, resourceManager) constructor,
exactly like every other asset (see application.hpp's own Phase 14f Create-
menu comment for why: there is no procedural-Mesh-to-Model pathway in this
engine, and this phase deliberately doesn't invent one -- reusing the normal
asset-loading path for two more small, checked-in OBJ files is the smallest,
most consistent-with-existing-architecture way to add "Sphere"/"Plane" next
to the pre-existing "Cube" (assets/models/falling_cube.obj, Phase 8e).

falling_cube.obj/scene.obj (Phase 5/8e) were hand-typed OBJ text -- easy for
a 6-face box, not practical for a smooth-ish sphere. This script is this
project's own established alternative for a primitive too tedious to
hand-author (see tools/generate_hdri.py's own Phase 13e header comment: "no
network fetch", pure offline generation): it PRINTS plain OBJ text to stdout
deterministically, no external mesh library, no network access, matching
this project's "procedurally generate every asset that isn't practically
hand-typed" convention. Run as:

    python3 tools/generate_primitive_meshes.py sphere > assets/models/sphere.obj
    python3 tools/generate_primitive_meshes.py plane  > assets/models/plane.obj

-- checked-in output, not regenerated at build/run time (same "generated
once, then a normal checked-in asset" treatment sky.hdr gets from
generate_hdri.py).

--- Sizing, to read as roughly the same on-screen scale as the existing
"Cube" (falling_cube.obj, half-extent 0.25 -- see that file's own header
comment) once all three sit side by side in a freshly Create'd scene ---------
Sphere: radius 0.35 (diameter 0.7) -- a little larger than the cube's 0.5
edge length, since a sphere's silhouette reads visually smaller than a box of
the same bounding radius (no flat corner-to-corner reach). Plane: a single
flat 1.0 x 1.0 quad (half-extent 0.5) on the XZ plane -- bigger than the cube
footprint-wise (a ground-patch primitive is expected to read as a surface,
not a small object), but not so large it swallows the rest of a freshly
created demo scene.

--- Winding: verified, not assumed -----------------------------------------
Every face this script emits has its vertex order checked against its own
target outward normal direction (see faceIsCorrectlyWound() below) before
being written -- reversed in the output if the naive parameterization would
have produced a face wound the wrong way (visible as a black/backface-culled
-- or, since this engine's Mesh/Material path does not cull backfaces,
inside-out-lit -- surface) rather than trusting the hand-derived triangle/
quad order to already be correct by construction.
"""
import math
import sys


def make_material_text(name: str, kd) -> str:
    r, g, b = kd
    return (
        f"newmtl {name}\n"
        f"Kd {r:.2f} {g:.2f} {b:.2f}\n"
        f"Ka {r:.2f} {g:.2f} {b:.2f}\n"
        f"Ks 0.30 0.30 0.30\n"
        f"Ns 32.0\n"
    )


def face_geometric_normal(a, b, c):
    """Right-hand-rule normal of triangle (a, b, c), unnormalized (only the
    SIGN of its dot product against a target direction is ever used below,
    so magnitude doesn't matter)."""
    ux, uy, uz = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    vx, vy, vz = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    return (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def face_is_correctly_wound(positions, target_outward):
    """positions: the face's own vertex positions in emission order (3 or 4
    of them). Splits into the first triangle (0, 1, 2) -- enough to
    determine a planar/near-planar polygon's own winding -- and checks its
    geometric normal points the same general direction (positive dot
    product) as `target_outward` (this sphere/plane is centered near the
    origin, or -- for the plane -- always meant to face +Y, so "outward" is
    always known independently of the face's own vertex order)."""
    normal = face_geometric_normal(positions[0], positions[1], positions[2])
    return dot(normal, target_outward) > 0.0


def emit_face(vertex_indices, positions, target_outward, vt_indices=None, vn_indices=None):
    """Returns one `f ...` OBJ line, reversing `vertex_indices` (and the
    matching vt/vn index lists, if given -- kept aligned with the position
    order so a per-vertex texcoord/normal still lands on the correct
    physical corner after a reversal) if the naive order was wound the wrong
    way (see face_is_correctly_wound() above)."""
    order = list(range(len(vertex_indices)))
    if not face_is_correctly_wound(positions, target_outward):
        order = list(reversed(order))

    parts = []
    for i in order:
        v = vertex_indices[i]
        vt = vt_indices[i] if vt_indices is not None else None
        vn = vn_indices[i] if vn_indices is not None else None
        if vt is not None and vn is not None:
            parts.append(f"{v}/{vt}/{vn}")
        elif vn is not None:
            parts.append(f"{v}//{vn}")
        else:
            parts.append(str(v))
    return "f " + " ".join(parts)


def generate_sphere(radius=0.35, stacks=10, slices=16):
    """A standard UV sphere: `stacks` latitude rings (theta 0..pi, 0 = north
    pole) x `slices` longitude columns (phi 0..2*pi, wrapping -- the last
    column duplicates the first position with u=1.0 instead of u=0.0, so
    texture coordinates wrap correctly instead of pinching the seam).
    Poles are emitted as triangle fans (a real degenerate-free triangle per
    slice); every other ring pair is a quad, matching falling_cube.obj/
    scene.obj's own "f a b c d" quad convention exactly (this engine's own
    Model importer triangulates on load regardless, aiProcess_Triangulate --
    see model.cpp -- so a quad here is just as valid input as a triangle).
    """
    positions = []  # 1-indexed via len(positions) after append
    texcoords = []
    for i in range(stacks + 1):
        theta = math.pi * i / stacks
        v = 1.0 - (i / stacks)
        for j in range(slices + 1):
            phi = 2.0 * math.pi * j / slices
            x = radius * math.sin(theta) * math.cos(phi)
            y = radius * math.cos(theta)
            z = radius * math.sin(theta) * math.sin(phi)
            positions.append((x, y, z))
            texcoords.append((j / slices, v))

    def idx(i, j):
        return i * (slices + 1) + j

    lines = []
    lines.append("# Phase 14f: procedurally generated UV sphere -- see")
    lines.append("# tools/generate_primitive_meshes.py for the generator (this file's own")
    lines.append("# vertex/face data is its checked-in output, not hand-typed). Radius 0.35,")
    lines.append("# centered on its own local origin, matching this engine's own")
    lines.append("# \"entity Transform position IS the mesh's geometric center, no separate")
    lines.append("# mesh-to-collider/mesh-to-origin offset\" convention (see")
    lines.append("# falling_cube.obj's own header comment for the same idea applied to a box).")
    lines.append("")
    lines.append("mtllib sphere.mtl")
    lines.append("")

    for p in positions:
        lines.append(f"v {p[0]:.6f} {p[1]:.6f} {p[2]:.6f}")
    lines.append("")
    for t in texcoords:
        lines.append(f"vt {t[0]:.6f} {t[1]:.6f}")
    lines.append("")
    # Vertex normals: a sphere centered on its own local origin has an
    # outward normal exactly equal to the (normalized) position vector at
    # every point on its surface -- so the normal list is just the position
    # list, each entry independently normalized, aligned 1:1 by index with
    # both `positions` and `texcoords` above (every face below reuses the
    # same index for v/vt/vn at each of its own corners).
    for p in positions:
        length = math.sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2])
        nx, ny, nz = (p[0] / length, p[1] / length, p[2] / length)
        lines.append(f"vn {nx:.6f} {ny:.6f} {nz:.6f}")
    lines.append("")

    lines.append("o Sphere")
    lines.append("usemtl Sphere")
    for i in range(stacks):
        for j in range(slices):
            p00, p01 = positions[idx(i, j)], positions[idx(i, j + 1)]
            p10, p11 = positions[idx(i + 1, j)], positions[idx(i + 1, j + 1)]
            # OBJ is 1-indexed.
            v00, v01, v10, v11 = idx(i, j) + 1, idx(i, j + 1) + 1, idx(i + 1, j) + 1, idx(i + 1, j + 1) + 1
            target = (
                (p00[0] + p01[0] + p10[0] + p11[0]) / 4.0,
                (p00[1] + p01[1] + p10[1] + p11[1]) / 4.0,
                (p00[2] + p01[2] + p10[2] + p11[2]) / 4.0,
            )
            if i == 0:
                # North-pole ring: p00 == p01 (both the pole point) --
                # collapse to one real triangle (pole, row1-left, row1-right)
                # instead of a degenerate quad.
                verts = [v00, v10, v11]
                pos = [p00, p10, p11]
            elif i == stacks - 1:
                # South-pole ring: p10 == p11 (both the pole point).
                verts = [v00, v01, v11]
                pos = [p00, p01, p11]
            else:
                verts = [v00, v01, v11, v10]
                pos = [p00, p01, p11, p10]
            lines.append(emit_face(verts, pos, target, vt_indices=verts, vn_indices=verts))

    lines.append("")
    return "\n".join(lines) + "\n"


def generate_plane(half_extent=0.5):
    """A single flat quad on the XZ plane (y = 0), normal +Y -- matching
    this engine's ground plane convention (mesh.hpp's makeGroundPlane(),
    also a flat XZ quad) so "Plane" reads as a small floor/table-top patch,
    not an arbitrary flat rectangle in some other orientation."""
    positions = [
        (-half_extent, 0.0, -half_extent),
        (half_extent, 0.0, -half_extent),
        (half_extent, 0.0, half_extent),
        (-half_extent, 0.0, half_extent),
    ]
    texcoords = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
    normal = (0.0, 1.0, 0.0)

    lines = []
    lines.append("# Phase 14f: a simple flat quad, generated by")
    lines.append("# tools/generate_primitive_meshes.py (not hand-typed, though a single quad")
    lines.append("# is simple enough it could have been -- generated here purely for the same")
    lines.append("# one script/one command produces every Create-menu primitive consistency")
    lines.append("# sphere.obj already needs a generator for). Half-extent 0.5, centered on its")
    lines.append("# own local origin, lying flat on the XZ plane (normal +Y) -- matching")
    lines.append("# mesh.hpp's makeGroundPlane() orientation convention.")
    lines.append("")
    lines.append("mtllib plane.mtl")
    lines.append("")
    for t in texcoords:
        lines.append(f"vt {t[0]:.6f} {t[1]:.6f}")
    lines.append("")
    lines.append(f"vn {normal[0]:.6f} {normal[1]:.6f} {normal[2]:.6f}")
    lines.append("")
    for p in positions:
        lines.append(f"v {p[0]:.6f} {p[1]:.6f} {p[2]:.6f}")
    lines.append("")
    lines.append("o Plane")
    lines.append("usemtl Plane")
    verts = [1, 2, 3, 4]
    vts = [1, 2, 3, 4]
    vns = [1, 1, 1, 1]
    target = normal
    lines.append(emit_face(verts, positions, target, vt_indices=vts, vn_indices=vns))
    lines.append("")
    return "\n".join(lines) + "\n"


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in ("sphere", "plane"):
        print("usage: generate_primitive_meshes.py [sphere|plane]", file=sys.stderr)
        return 1

    if sys.argv[1] == "sphere":
        sys.stdout.write(generate_sphere())
    else:
        sys.stdout.write(generate_plane())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
