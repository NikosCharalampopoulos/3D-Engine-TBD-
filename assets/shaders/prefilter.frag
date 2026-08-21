#version 330 core

// Phase 10: the prefiltered specular environment map -- the split-sum
// approximation's other precomputed environment term (Karis, "Real Shading
// in Unreal Engine 4", section 3.2). Convolves uEnvironmentMap with the GGX
// specular lobe at a fixed roughness (uRoughness, one value per mip level --
// see engine::IBLProbe's constructor), rendered offline at startup, once per
// (face, mip) pair, never per-frame.
//
// Unlike irradiance_convolution.frag's uniform hemisphere sampling (fine for
// a broad Lambertian lobe, since every direction contributes comparably), a
// GGX lobe concentrates almost all of its energy in a narrow cone around the
// reflection direction at low roughness -- uniform sampling would need a huge
// sample count just to have a realistic chance of landing inside that cone at
// all. ImportanceSampleGGX below instead draws sample directions already
// biased to land inside the lobe, using the standard low-discrepancy
// Hammersley sequence (deterministic, well-distributed over [0,1)^2, cheap to
// generate with no precomputed tables -- see radicalInverseVdC/hammersley) as
// its 2D random source.
//
// Real-time prefiltering's standard N == V == R simplification (Karis'
// reference, section 3.2): for a real shaded fragment under environment
// lighting, V varies with view angle and the true reflection lobe isn't
// perfectly symmetric around N, but baking that in would need a much
// higher-dimensional precompute (indexed by view angle too, not just
// roughness/mip level). Assuming N == V == R -- i.e. always viewed straight
// on -- loses only the lobe's grazing-angle stretch; this is a standard,
// widely-used real-time trade-off Karis' own reference makes explicitly, not
// an ad-hoc shortcut invented here.
in vec3 vDirection;

out vec4 FragColor;

uniform samplerCube uEnvironmentMap;
uniform float uRoughness;

const float PI = 3.14159265359;
// Deliberately far below the 1024-sample count a real-time-per-pixel
// integration (or this same project's brdf_lut.frag, computed only 128x128
// times total) can afford: this shader instead runs once per texel across
// every face of every mip level (up to 128x128x6 texels at mip 0 -- see
// IBLProbe's kPrefilterBaseSize) on a software (llvmpipe) GL rasterizer with
// no real GPU parallelism, where sample count multiplies total precompute
// time directly. 32 still importance-samples the correct GGX lobe (the
// algorithm is unchanged from Karis' reference, only its constant differs)
// and looks visually smooth once combined with trilinear-filtered mip
// sampling in pbr.frag -- a higher count would look marginally less noisy at
// the cost of a proportionally longer one-time startup stall.
const uint SAMPLE_COUNT = 32u;

// Van der Corput radical inverse (base 2) via bit-reversal -- the standard
// cheap way to generate the first dimension of a Hammersley sequence without
// a lookup table.
float radicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;  // / 0x100000000
}

vec2 hammersley(uint i, uint n) {
    return vec2(float(i) / float(n), radicalInverseVdC(i));
}

// Maps a low-discrepancy 2D sample Xi to a half vector H distributed
// according to the GGX normal distribution around N at the given roughness
// -- see Karis' reference for the derivation of cosTheta/phi below from the
// NDF's own importance-sampling PDF. Identical math to pbr.frag's
// distributionGGX (alpha = roughness^2), just solved for a sample direction
// instead of evaluated as a density at a fixed H.
vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness * roughness;

    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // Tangent-space -> world-space, same "pick a non-parallel up reference"
    // pattern as irradiance_convolution.frag, just tested against N.z instead
    // of N.y (matching Karis'/LearnOpenGL's own reference convention here --
    // either axis works, they just need to disagree with N somewhere).
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

void main() {
    vec3 N = normalize(vDirection);
    // N == V == R -- see header comment above.
    vec3 R = N;
    vec3 V = R;

    vec3 prefilteredColor = vec3(0.0);
    float totalWeight = 0.0;

    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 H = importanceSampleGGX(Xi, N, uRoughness);
        // Reflects V about the sampled half-vector H to get the light
        // direction that half-vector corresponds to -- the same reflect()
        // identity used everywhere else a half-vector needs inverting back to
        // a direction.
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            // Weighted by NdotL (not just averaged uniformly): samples near
            // the lobe's own grazing edge contribute less real energy than
            // ones near its center, matching the standard reference weighting.
            prefilteredColor += texture(uEnvironmentMap, L).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    prefilteredColor = totalWeight > 0.0 ? prefilteredColor / totalWeight : prefilteredColor;
    FragColor = vec4(prefilteredColor, 1.0);
}
