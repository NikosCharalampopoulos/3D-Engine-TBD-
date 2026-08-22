#version 330 core

// Phase 13e: converts an equirectangular HDR environment map (a single
// floating-point sampler2D, uEquirectangularMap -- see
// engine::loadHdrEquirectangularAsCubemap()) into one face of a
// floating-point GL_TEXTURE_CUBE_MAP, rendered once per face at startup
// (paired with cubemap_capture.vert, the same fixed-per-face-view vertex
// stage engine::IBLProbe's own one-time convolution passes already share --
// see that file's own header comment for why vDirection == the cube's own
// local position works as a world-space direction here). Never runs
// per-frame.
//
// Direction -> equirectangular UV convention (must match
// tools/generate_hdri.py's own authoring convention exactly -- see that
// script's header comment for the full derivation of why row 0 of the .hdr
// file is the zenith and column 0/(W-1) are the same wrapped azimuth):
//
//   u = atan2(dir.z, dir.x) / (2*pi) + 0.5   -- azimuth, wraps at u=0/1
//   v = asin(dir.y) / pi + 0.5               -- elevation, v=1 at dir.y=+1
//                                                (straight up), v=0 at
//                                                dir.y=-1 (straight down)
//
// This is the standard LearnOpenGL/Karis "spherical environment map" UV
// formula (equivalent to the more commonly quoted v = acos(dir.y)/pi up to
// a vertical flip -- asin's zero-at-equator symmetry is used here purely
// because it composes directly with engine::Texture's existing
// stbi_set_flip_vertically_on_load(true) load convention, which
// loadHdrEquirectangularAsCubemap() reuses rather than inventing a second,
// unflipped image-loading convention just for this one asset -- see that
// function's own header comment for the exact row-order derivation). Get
// this sign/axis convention wrong and every face still renders *something*
// (no crash, no GL error) -- just a rotated, mirrored, or pole-swapped sky,
// which is why this project's own verification (see README.md's Phase 13e
// section) checks the sun's rendered position against its known
// authored (elevation, azimuth) rather than only confirming the shader
// compiles and links.
in vec3 vDirection;

out vec4 FragColor;

uniform sampler2D uEquirectangularMap;

const float PI = 3.14159265359;

void main() {
    vec3 dir = normalize(vDirection);
    float u = atan(dir.z, dir.x) / (2.0 * PI) + 0.5;
    float v = asin(clamp(dir.y, -1.0, 1.0)) / PI + 0.5;
    FragColor = vec4(texture(uEquirectangularMap, vec2(u, v)).rgb, 1.0);
}
