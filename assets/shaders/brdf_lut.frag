#version 330 core

// Phase 10: the split-sum approximation's BRDF integration term -- a 2D LUT
// (see engine::IBLProbe) indexed by (N.V, roughness) via vTexCoord, storing
// the two-channel (scale, bias) applied to F0 in pbr.frag's specular IBL term
// (Karis, "Real Shading in Unreal Engine 4", section 3.2: the specular
// integral factors into "prefiltered environment color" times "this BRDF
// term", each precomputable independently -- that's the "split" the whole
// approximation is named for). Computed once, offline, covering every
// (N.V, roughness) combination up front instead of re-integrating the full
// BRDF per-pixel every frame.
//
// Reuses postprocess.vert as its vertex stage (both are plain fullscreen-quad
// passes with no model/view/projection at all -- see that file's own header
// comment) with this different fragment shader; see engine::IBLProbe's
// constructor for how the two are paired into one GL program.
in vec2 vTexCoord;

out vec4 FragColor;

const float PI = 3.14159265359;
// Comfortably high (unlike prefilter.frag's 32) since this pass runs only
// once, over a small fixed-size 2D grid (see IBLProbe::kBrdfLutSize) rather
// than once per texel of a multi-face, multi-mip cubemap -- total sample
// work stays far smaller even at this much higher per-texel count.
const uint SAMPLE_COUNT = 1024u;

float radicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint n) {
    return vec2(float(i) / float(n), radicalInverseVdC(i));
}

// Identical to prefilter.frag's importanceSampleGGX -- duplicated rather than
// shared for the same "no #include support" reason pbr.frag's own duplicated
// shadowFactor()/BRDF helpers give (see shader.hpp/pbr.frag's header
// comments): this engine's Shader class links each program from exactly one
// vertex + one fragment file with no preprocessing step to pull in shared
// GLSL source.
vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness * roughness;

    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

// The IBL-specific Schlick-GGX geometry remapping, k = roughness^2 / 2
// (Karis' reference) -- a DIFFERENT constant from pbr.frag's own
// geometrySchlickGGX, which uses the direct-lighting remapping
// k = (roughness+1)^2 / 8 (see that function's own comment on why the two
// must not be confused/shared).
float geometrySchlickGGX_IBL(float NdotV, float roughness) {
    float k = (roughness * roughness) / 2.0;
    float denom = NdotV * (1.0 - k) + k;
    return NdotV / max(denom, 1e-7);
}

float geometrySmith_IBL(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggxV = geometrySchlickGGX_IBL(NdotV, roughness);
    float ggxL = geometrySchlickGGX_IBL(NdotL, roughness);
    return ggxV * ggxL;
}

// Integrates the split-sum's BRDF half over a fixed (NdotV, roughness) pair,
// following Karis'/LearnOpenGL's reference derivation exactly: V and N are
// both fixed in a convenient local frame (N along +Z, V in the XZ plane) so
// only the sampled light direction L varies -- the resulting (A, B) pair is
// exactly pbr.frag's envBRDF.xy, used there as
// `specularIBL = prefilteredColor * (F0 * envBRDF.x + envBRDF.y)`.
vec2 integrateBRDF(float NdotV, float roughness) {
    vec3 V;
    V.x = sqrt(1.0 - NdotV * NdotV);
    V.y = 0.0;
    V.z = NdotV;

    float A = 0.0;
    float B = 0.0;

    vec3 N = vec3(0.0, 0.0, 1.0);

    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 H = importanceSampleGGX(Xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0) {
            float G = geometrySmith_IBL(N, V, L, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV + 1e-7);
            float Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }

    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);
    return vec2(A, B);
}

void main() {
    // vTexCoord.x = N.V in [0,1], vTexCoord.y = roughness in [0,1] -- the
    // LUT's own two axes (see IBLProbe's constructor for how this pass's
    // fullscreen quad maps onto that domain).
    vec2 integratedBRDF = integrateBRDF(vTexCoord.x, vTexCoord.y);
    FragColor = vec4(integratedBRDF, 0.0, 1.0);
}
