#!/usr/bin/env python3
"""Phase 13e: generates a real, valid equirectangular Radiance .hdr sky
environment map -- assets/textures/hdri/sky.hdr -- entirely offline (no
network fetch), matching this project's existing "procedurally generate
every texture" convention (checker.png/normal_bump.png/the old skybox
faces/rusted_metal_*/scuffed_plastic_* were all made the same way, just with
ImageMagick instead of numpy -- see README.md's Phase 4/7b/11 notes).

Equirectangular direction <-> UV convention (must match
assets/shaders/equirect_to_cubemap.frag exactly, see that file's own header
comment for the full derivation):

    Reader side (engine::loadHdrEquirectangularAsCubemap(), same flip
    convention as engine::Texture -- stbi_set_flip_vertically_on_load(true)):
    after loading, GL texcoord v=1 samples this file's FIRST scanline (row
    0) and v=0 samples its LAST scanline (row H-1).

    Shader side (equirect_to_cubemap.frag): for a direction `dir`,
        u = atan2(dir.z, dir.x) / (2*pi) + 0.5
        v = asin(dir.y) / pi + 0.5
    so v=1 <-> dir.y=+1 (straight up) and v=0 <-> dir.y=-1 (straight down).

    Combining the two: row 0 (top of this file, as conventional image
    authoring puts the "top of the world" at the top of the picture) must be
    dir.y=+1 (zenith), and row H-1 (bottom of the file) must be dir.y=-1
    (nadir) -- i.e. this script's row index j maps to colatitude
    theta = pi * j/(H-1), dir.y = cos(theta), exactly the standard
    equirectangular sky-authoring convention. Column i maps to azimuth
    phi = 2*pi*(i/(W-1) - 0.5) (wraps at the left/right seam, which is why
    column 0 and column W-1 must produce near-identical content -- confirmed
    below).
"""
import math
import struct
import sys

import numpy as np

W, H = 1024, 512

# --- Build a (H, W) grid of world directions matching the convention above ---
j = np.arange(H, dtype=np.float64).reshape(H, 1)
i = np.arange(W, dtype=np.float64).reshape(1, W)

t = j / (H - 1)             # 0 at top row -> 1 at bottom row
theta = math.pi * t         # colatitude: 0 at zenith, pi at nadir
u = i / (W - 1)
phi = 2.0 * math.pi * (u - 0.5)

sin_theta = np.sin(theta)
dir_y = np.cos(theta) * np.ones((1, W))          # broadcast to (H, W)
dir_x = sin_theta * np.cos(phi)
dir_z = sin_theta * np.sin(phi)

# --- Sky gradient: darker/cooler at zenith, brighter/warmer near the horizon,
# a plausible dark "ground" below the horizon -- a simple, physically-inspired
# (not physically simulated) color-temperature falloff, the same spirit as
# the old procedural skybox's gradient (see README's Phase 7b section) but
# now carrying real HDR intensity (well above 1.0) instead of being baked
# into 8-bit sRGB.
zenith_color = np.array([0.06, 0.12, 0.38])   # deep blue, straight up
horizon_color = np.array([1.35, 0.78, 0.42])  # warm, bright, at the horizon
ground_near_color = np.array([0.22, 0.17, 0.13])   # dim warm dirt just below the horizon
ground_far_color = np.array([0.03, 0.03, 0.035])   # near-black further down

# elevation in [-1, 1] (dir_y); smoothstep-blend sky (elevation >= 0) between
# zenith and horizon, and ground (elevation < 0) between horizon and deep
# ground.
def smoothstep(edge0, edge1, x):
    tt = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return tt * tt * (3.0 - 2.0 * tt)

sky_mix = smoothstep(0.0, 1.0, dir_y)  # 0 at horizon -> 1 at zenith
ground_mix = smoothstep(0.0, 1.0, -dir_y)  # 0 at horizon -> 1 at nadir

color = np.empty((H, W, 3), dtype=np.float64)
for c in range(3):
    sky = horizon_color[c] + (zenith_color[c] - horizon_color[c]) * sky_mix
    ground = horizon_color[c] + (ground_near_color[c] - horizon_color[c]) * np.clip(ground_mix * 3.0, 0.0, 1.0)
    ground = ground + (ground_far_color[c] - ground_near_color[c]) * np.clip((ground_mix - 0.33) / 0.67, 0.0, 1.0)
    color[:, :, c] = np.where(dir_y >= 0.0, sky, ground)

# --- Sun: a small, very bright disk well above 1.0 (the whole point of HDR
# -- see the phase brief), plus a softer warm glow/corona around it so it
# reads as a light source embedded in the sky rather than a flat sticker.
# Placed at a fixed, documented elevation/azimuth so the rendered skybox's
# sun position can be reasoned about/verified against the shader's direction
# convention (see this project's own verification methodology in
# README.md -- reasoning through the math, not just eyeballing it).
sun_elevation_deg = 4.0    # low in the sky (see this project's own default
                           # camera framing, application.cpp's
                           # kDefaultCameraPosition/kSceneCenter -- this
                           # elevation keeps the sun inside that camera's
                           # visible vertical field of view instead of
                           # passing safely, invisibly, overhead)
sun_azimuth_deg = 230.0    # close to that same default camera's own forward
                           # azimuth (~232.6 degrees, computed from
                           # kDefaultCameraPosition -> kSceneCenter) so the
                           # sun actually lands inside frame rather than off
                           # to a side never rendered
sun_theta = math.radians(90.0 - sun_elevation_deg)  # colatitude
sun_phi = math.radians(sun_azimuth_deg)
sun_dir = np.array([
    math.sin(sun_theta) * math.cos(sun_phi),
    math.cos(sun_theta),
    math.sin(sun_theta) * math.sin(sun_phi),
])
sun_dir = sun_dir / np.linalg.norm(sun_dir)

dot = dir_x * sun_dir[0] + dir_y * sun_dir[1] + dir_z * sun_dir[2]
dot = np.clip(dot, -1.0, 1.0)
angle_deg = np.degrees(np.arccos(dot))

sun_core_radius_deg = 1.1
sun_glow_radius_deg = 14.0
sun_core_intensity = 220.0     # >>1.0: real HDR, the whole point of this phase
sun_glow_intensity = 9.0

core = np.clip(1.0 - angle_deg / sun_core_radius_deg, 0.0, 1.0)
core = core * core  # sharpen the disk edge a bit
glow = np.clip(1.0 - angle_deg / sun_glow_radius_deg, 0.0, 1.0)
glow = glow * glow * glow

sun_color = np.array([1.0, 0.92, 0.75])  # slightly warm-white, not pure white
for c in range(3):
    color[:, :, c] += core * sun_core_intensity * sun_color[c]
    color[:, :, c] += glow * sun_glow_intensity * sun_color[c]

color = np.clip(color, 0.0, None).astype(np.float32)

# Sanity: left/right seam (column 0 vs column W-1) should match almost
# exactly (both sit at azimuth +/-180 degrees, the same physical direction) --
# verifies the azimuth wrap is continuous before this ever reaches the GPU's
# GL_REPEAT wrap.
seam_diff = np.abs(color[:, 0, :] - color[:, -1, :]).max()
print(f"left/right seam max abs diff: {seam_diff:.6f}", file=sys.stderr)
assert seam_diff < 1e-3, "equirect azimuth seam does not wrap continuously"

print(f"sun direction (world, y-up): {sun_dir}", file=sys.stderr)
print(f"peak radiance (sun core): {color.max():.2f}", file=sys.stderr)
print(f"zenith (row 0) sample: {color[0, W // 2]}", file=sys.stderr)
print(f"horizon (row {H // 2}) sample: {color[H // 2, W // 2]}", file=sys.stderr)


# --- Radiance RGBE (.hdr) writer: a faithful, from-scratch Python port of
# stb_image_write.h's stbi_write_hdr_core()/stbiw__write_hdr_scanline()
# (new-style per-component run-length-encoded RGBE, "#?RADIANCE" header) --
# this project's own vendored external/stb/stb_image.h (stbi_loadf(), see
# src/texture.cpp's Phase 13e HDR-loading extension) decodes exactly this
# format, confirmed by reading its stbi__hdr_load() RLE-decode path (width in
# [8, 32768) selects the RLE path on both the read and write side here, so
# this is never accidentally exercising the "flat" fallback path instead).
def linear_to_rgbe(r, g, b):
    maxcomp = max(r, g, b)
    if maxcomp < 1e-32:
        return 0, 0, 0, 0
    mantissa, exponent = math.frexp(maxcomp)
    normalize = mantissa * 256.0 / maxcomp
    e = int(max(0, min(255, exponent + 128)))
    rr = int(max(0, min(255, math.floor(r * normalize))))
    gg = int(max(0, min(255, math.floor(g * normalize))))
    bb = int(max(0, min(255, math.floor(b * normalize))))
    return rr, gg, bb, e


def write_run(out, length, databyte):
    out.append(bytes([(length + 128) & 0xFF, databyte & 0xFF]))


def write_dump(out, data):
    out.append(bytes([len(data)]) + bytes(data))


def rle_component(out, comp):
    # comp: list[int] (one scanline's worth of one RGBE channel's bytes).
    width = len(comp)
    x = 0
    while x < width:
        r = x
        while r + 2 < width and not (comp[r] == comp[r + 1] == comp[r + 2]):
            r += 1
        if r + 2 >= width:
            r = width
        while x < r:
            length = min(r - x, 128)
            write_dump(out, comp[x:x + length])
            x += length
        if r + 2 < width:
            run_start = r
            while r < width and comp[r] == comp[run_start]:
                r += 1
            while x < r:
                length = min(r - x, 127)
                write_run(out, length, comp[x])
                x += length


def write_hdr(path, pixels):
    """pixels: (H, W, 3) float32 array, row 0 = first scanline written
    (this file's own top row, per this script's own convention above --
    unlike stb_image_write's own default of flipping unless told
    otherwise, this hand-rolled port writes rows in array order with no
    implicit flip, matching how `color` above was authored)."""
    h, w, _ = pixels.shape
    out = []
    header = (
        b"#?RADIANCE\n"
        b"# Phase 13e procedural sky HDRI (see tools' generate_hdri.py)\n"
        b"FORMAT=32-bit_rle_rgbe\n"
        b"\n" + f"-Y {h} +X {w}\n".encode("ascii")
    )
    out.append(header)

    use_rle = 8 <= w < 32768
    for row in range(h):
        scanline = pixels[row]
        if not use_rle:
            for col in range(w):
                r, g, b = scanline[col]
                rr, gg, bb, e = linear_to_rgbe(float(r), float(g), float(b))
                out.append(bytes([rr, gg, bb, e]))
            continue

        rgbe_bytes = [[0] * w for _ in range(4)]
        for col in range(w):
            r, g, b = scanline[col]
            rr, gg, bb, e = linear_to_rgbe(float(r), float(g), float(b))
            rgbe_bytes[0][col] = rr
            rgbe_bytes[1][col] = gg
            rgbe_bytes[2][col] = bb
            rgbe_bytes[3][col] = e

        out.append(bytes([2, 2, (w & 0xFF00) >> 8, w & 0x00FF]))
        for c in range(4):
            rle_component(out, rgbe_bytes[c])

    with open(path, "wb") as f:
        for chunk in out:
            f.write(chunk)


if __name__ == "__main__":
    out_path = sys.argv[1] if len(sys.argv) > 1 else "sky.hdr"
    write_hdr(out_path, color)
    print(f"wrote {out_path} ({W}x{H})", file=sys.stderr)
