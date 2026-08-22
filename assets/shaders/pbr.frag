#version 330 core

// Phase 9: metallic/roughness Cook-Torrance PBR, replacing basic.frag's
// Blinn-Phong model for whatever's drawn with this program (this phase: the
// new sphere test-grid -- see Application's sphereMaterials_/sphereMesh_).
// basic.vert/basic.frag are left completely untouched as the reference/
// fallback path for the rest of the scene (table/box/pyramid/ground) -- see
// README.md's Phase 9 notes for why the two coexist.
//
// Reuses basic.frag's light uniform arrays/attenuation/spot-cone logic and
// its shadow-map sampling verbatim (same struct layouts/uniform names, same
// slope-scaled-bias 3x3 PCF, same tangent-space normal-mapping TBN
// construction) -- only what happens to each light's contribution once its
// radiance/direction reach the fragment changes: instead of N.L diffuse +
// Blinn-Phong specular, every light's contribution goes through the
// Cook-Torrance microfacet BRDF below.
in vec3 vNormal;
in vec3 vFragPos;
in vec2 vTexCoord;
in vec3 vTangent;
// Phase 13c: replaces vFragPosLightSpace -- see pbr.vert's comment.
in float vViewSpaceDepth;

out vec4 FragColor;

// --- PBR material (see engine::PBRMaterial) ---
uniform vec3 uAlbedo;
uniform float uMetallic;
uniform float uRoughness;
uniform float uAO;

uniform sampler2D uAlbedoMap;
uniform int uUseAlbedoMap;
uniform sampler2D uNormalMap;
uniform int uUseNormalMap;
// Phase 11: a packed "ORM" map (R = ambient occlusion, G = roughness,
// B = metallic -- see engine::PBRMaterial's header comment). When bound,
// its G/B channels replace uRoughness/uMetallic outright and its R channel
// multiplies uAO (so the scalar AO knob still acts as an overall multiplier
// even with a map bound) -- see main()'s "material inputs" block below.
uniform sampler2D uORMMap;
uniform int uUseORMMap;

// --- Directional light (the one shadow-casting light) ---
uniform vec3 uLightDirection;
uniform vec3 uLightColor;
uniform vec3 uAmbientColor;
uniform vec3 uViewPos;

// Phase 13c: Cascaded Shadow Maps -- see basic.frag's identical comment on
// uLightSpaceMatrices/uCascadeSplits/uShadowMap0/1/2 (this shader reuses the
// exact same uniform names/layout, same duplication-by-design rationale as
// every other reused block in this file).
#define NUM_CASCADES 3
uniform mat4 uLightSpaceMatrices[NUM_CASCADES];
uniform float uCascadeSplits[NUM_CASCADES];
uniform sampler2D uShadowMap0;
uniform sampler2D uShadowMap1;
uniform sampler2D uShadowMap2;
uniform vec2 uShadowMapTexelSize;

// --- Phase 10: image-based lighting (see engine::IBLProbe) ---
// uIrradianceMap: a small, pre-convolved diffuse irradiance cubemap (the
// environment integrated over a cosine-weighted hemisphere at every texel).
// uPrefilterMap: the environment pre-convolved against the GGX specular lobe
// at increasing roughness across its mip chain -- mip 0 = roughness 0 (a
// crisp mirror reflection) up to MAX_REFLECTION_LOD's mip = roughness 1 (a
// broad, blurred average). uBrdfLUT: the split-sum's precomputed BRDF
// integral, indexed by (N.V, roughness). Together these replace the old flat
// `uAmbientColor * albedo * ao * (1 - metallic)` placeholder ambient term
// (see this shader's own removed Phase 9 comment) with real, direction- and
// roughness-aware ambient lighting derived from the actual visible skybox.
uniform samplerCube uIrradianceMap;
uniform samplerCube uPrefilterMap;
uniform sampler2D uBrdfLUT;
// kPrefilterMipLevels - 1 (see IBLProbe::kPrefilterMipLevels) -- the highest
// valid mip index of uPrefilterMap, i.e. the roughness-1.0 mip. Kept in sync
// with that constant by hand (no shared GLSL/C++ constant crosses this
// boundary anywhere else in this engine either -- see e.g. MAX_POINT_LIGHTS/
// MAX_SPOT_LIGHTS above, kept in sync with application.cpp's kPointLights/
// kSpotLights the same way).
const float MAX_REFLECTION_LOD = 4.0;

#define MAX_POINT_LIGHTS 8
#define MAX_SPOT_LIGHTS 4

struct PointLight {
    vec3 position;
    vec3 color;
    float constant;
    float linear;
    float quadratic;
};

struct SpotLight {
    vec3 position;
    vec3 direction;
    vec3 color;
    float constant;
    float linear;
    float quadratic;
    float innerCutoff;
    float outerCutoff;
};

uniform int uNumPointLights;
uniform PointLight uPointLights[MAX_POINT_LIGHTS];
uniform int uNumSpotLights;
uniform SpotLight uSpotLights[MAX_SPOT_LIGHTS];

const float PI = 3.14159265359;

float attenuationFor(float constant, float linear, float quadratic, float distance) {
    return 1.0 / (constant + linear * distance + quadratic * distance * distance);
}

// Directional-light shadow factor -- identical to basic.frag's
// shadowFactorForCascade()/shadowFactor() (same cascade selection, same PCF
// kernel, same slope-scaled bias, same cascade-blend band); duplicated here
// rather than shared via #include since this engine's Shader class links
// each program from exactly one vertex + one fragment file with no
// preprocessing/#include support (see shader.hpp).
float sampleCascadeDepth(int index, vec2 uv) {
    if (index == 0) {
        return texture(uShadowMap0, uv).r;
    } else if (index == 1) {
        return texture(uShadowMap1, uv).r;
    }
    return texture(uShadowMap2, uv).r;
}

float shadowFactorForCascade(int cascadeIndex, vec3 normal, vec3 lightDir) {
    vec4 fragPosLightSpace = uLightSpaceMatrices[cascadeIndex] * vec4(vFragPos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0) {
        return 0.0;
    }

    float currentDepth = projCoords.z;
    float bias = max(0.006 * (1.0 - dot(normal, lightDir)), 0.0015);

    float shadowSum = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 offset = vec2(float(x), float(y)) * uShadowMapTexelSize;
            float closestDepth = sampleCascadeDepth(cascadeIndex, projCoords.xy + offset);
            shadowSum += (currentDepth - bias > closestDepth) ? 1.0 : 0.0;
        }
    }
    return shadowSum / 9.0;
}

const float kCascadeBlendBand = 0.75;

float shadowFactor(vec3 normal, vec3 lightDir) {
    int cascadeIndex = 0;
    for (int i = 0; i < NUM_CASCADES - 1; ++i) {
        if (vViewSpaceDepth > uCascadeSplits[i]) {
            cascadeIndex = i + 1;
        }
    }

    float shadow = shadowFactorForCascade(cascadeIndex, normal, lightDir);

    if (cascadeIndex < NUM_CASCADES - 1) {
        float splitDepth = uCascadeSplits[cascadeIndex];
        float blend = clamp((vViewSpaceDepth - (splitDepth - kCascadeBlendBand)) / kCascadeBlendBand, 0.0, 1.0);
        if (blend > 0.0) {
            float nextShadow = shadowFactorForCascade(cascadeIndex + 1, normal, lightDir);
            shadow = mix(shadow, nextShadow, blend);
        }
    }

    return shadow;
}

// --- Cook-Torrance microfacet BRDF terms ---

// GGX/Trowbridge-Reitz normal distribution. alpha = roughness^2 (the
// standard perceptual remapping -- using roughness directly here instead is
// a common bug: it makes the roughness slider behave non-linearly and
// mismatched against every other engine/tool's convention). Denominator
// floored with a small epsilon so an extremely low roughness (alpha^2 tiny)
// combined with N.H very close to 1 can't divide by (numerically) zero and
// produce Inf/NaN.
//
// The epsilon here matters more than it looks: at N.H == 1 the denominator
// is exactly pi * alpha^4, which is already a genuinely tiny (but not
// remotely float-underflow-tiny) number for this engine's own minimum
// roughness -- PBRMaterial::kMinRoughness (0.045) gives pi*alpha^4 ~= 5.3e-11,
// still 27 orders of magnitude above float32's smallest normal value
// (~1.2e-38). An earlier version of this guard floored the denominator at
// 1e-7 -- large enough to look like "just a small epsilon," but actually
// bigger than that legitimate denominator, so it silently clamped away the
// lowest-roughness sphere's *entire* peak brightness advantage: a
// side-by-side numeric check (re-evaluating this exact formula in Python for
// roughness = 0.05 vs. 0.2) found the 0.05 case coming out *dimmer* at its
// own peak than the 0.2 case -- backwards from "lower roughness = sharper,
// brighter highlight." 1e-12 sits safely below every denominator this engine
// legitimately produces (roughness >= ~0.03) while still guarding the
// genuine zero-roughness edge case.
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 1e-12);
}

// Schlick-GGX geometry term for ONE direction (view or light), with the
// direct-lighting remapping k = (roughness + 1)^2 / 8 -- deliberately NOT
// the IBL remapping (k = roughness^2 / 2), which is a different constant
// for a different lighting scenario (Phase 10's job, not this phase's direct
// lights).
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float denom = NdotV * (1.0 - k) + k;
    return NdotV / max(denom, 1e-7);
}

// Smith's method: the view-direction and light-direction geometry terms are
// independent and simply multiplied together.
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggxV = geometrySchlickGGX(NdotV, roughness);
    float ggxL = geometrySchlickGGX(NdotL, roughness);
    return ggxV * ggxL;
}

// Fresnel-Schlick approximation. F0 is the surface's reflectance at normal
// incidence: ~0.04 for common dielectrics (glass, plastic, skin, ...) or the
// surface's own albedo for a metal -- see F0's construction in main() below,
// `mix(vec3(0.04), albedo, metallic)`. This is THE physically-important
// metal/dielectric distinction in the whole BRDF: a metal's reflectance is
// colored (tinted by its albedo) and has no separate diffuse term at all
// (see kD's `(1.0 - metallic)` factor below), while a dielectric's
// reflectance is a small, colorless (white/grey) constant regardless of its
// own albedo color.
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// The roughness-aware Fresnel variant IBL's ambient term uses instead of
// plain fresnelSchlick() above (Karis' reference, section on IBL diffuse):
// direct lighting's F0..1 Fresnel curve assumes a perfectly smooth mirror
// surface, which over-estimates a rough surface's edge-on reflectance (a
// rough dielectric's grazing-angle response is less sharply Fresnel-bright
// than a mirror's) -- clamping the curve's upper bound to
// max(1 - roughness, F0) instead of a flat 1.0 tempers that overestimate as
// roughness increases, falling back to the ordinary Schlick curve exactly
// when roughness == 0.
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// One light's full direct contribution: D * G * F specular, energy-conserving
// Lambertian diffuse, both weighted by N.L and the light's incoming
// radiance. `radiance` already has attenuation/spot-cone/shadow folded in by
// the caller -- this function only implements the BRDF itself.
vec3 shadeDirectLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic, float roughness,
                       vec3 F0) {
    vec3 H = normalize(V + L);

    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    // Epsilon in the denominator (not just floored via max()) avoids a
    // divide-by-zero at grazing angles, where both N.V and N.L can approach
    // 0 simultaneously.
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    // kS (specular reflectance ratio) is exactly F -- energy split between
    // specular and diffuse must sum to (at most) the incoming light, and F
    // already IS that specular fraction. kD is the remainder, additionally
    // zeroed out by metallic: a pure metal reflects all incoming light
    // specularly and has no diffuse (sub-surface) scattering at all -- this
    // `(1.0 - metallic)` factor is exactly the "common bug" of forgetting it
    // and getting non-physical diffuse response from metals.
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    vec3 diffuse = kD * albedo / PI;

    float NdotL = max(dot(N, L), 0.0);
    return (diffuse + specular) * radiance * NdotL;
}

void main() {
    vec3 normal = normalize(vNormal);

    // Tangent-space normal mapping -- identical construction to basic.frag
    // (Gram-Schmidt re-orthogonalized TBN, bitangent = cross(normal,
    // tangent)); applies identically regardless of the lighting model
    // driving the final shading below. None of this phase's sphere-grid
    // materials actually bind a normal map (uUseNormalMap stays 0 for them),
    // but the path is here for any PBR material that does.
    if (uUseNormalMap != 0) {
        vec3 T = normalize(vTangent - normal * dot(normal, vTangent));
        vec3 B = cross(normal, T);
        mat3 TBN = mat3(T, B, normal);

        vec3 sampledNormal = texture(uNormalMap, vTexCoord).rgb * 2.0 - 1.0;
        normal = normalize(TBN * sampledNormal);
    }

    vec3 albedo = uAlbedo;
    if (uUseAlbedoMap != 0) {
        albedo *= texture(uAlbedoMap, vTexCoord).rgb;
    }
    float metallic = uMetallic;
    float roughness = uRoughness;
    float ao = uAO;
    // Phase 11: packed ORM map overrides roughness/metallic outright (G/B
    // channels) and multiplies (not replaces) the scalar AO uniform with its
    // own R channel -- see uORMMap's comment above. Roughness is re-floored
    // here the same way PBRMaterial::bind() floors its scalar uniform
    // (kMinRoughness = 0.045f there) -- alpha = roughness^2 is singular at
    // alpha == 0, and a hand-authored texture has no equivalent CPU-side
    // clamp of its own to rely on.
    if (uUseORMMap != 0) {
        vec3 orm = texture(uORMMap, vTexCoord).rgb;
        roughness = max(orm.g, 0.045);
        metallic = clamp(orm.b, 0.0, 1.0);
        ao *= orm.r;
    }

    vec3 N = normal;
    vec3 V = normalize(uViewPos - vFragPos);

    // F0: 0.04 is the standard "average dielectric" reflectance approximation;
    // a metal instead uses its own albedo as F0 (a colored, high-reflectance
    // Fresnel term) -- see fresnelSchlick()'s comment above for why this one
    // line is the crux of getting metals vs. dielectrics right.
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);

    // --- Directional light (shadowed) ---
    {
        vec3 L = normalize(-uLightDirection);
        float shadow = shadowFactor(N, L);
        vec3 radiance = uLightColor * (1.0 - shadow);
        Lo += shadeDirectLight(N, V, L, radiance, albedo, metallic, roughness, F0);
    }

    // --- Point lights (unshadowed) ---
    for (int i = 0; i < uNumPointLights; ++i) {
        vec3 toLight = uPointLights[i].position - vFragPos;
        float distance = length(toLight);
        vec3 L = normalize(toLight);

        float atten =
            attenuationFor(uPointLights[i].constant, uPointLights[i].linear, uPointLights[i].quadratic, distance);
        vec3 radiance = uPointLights[i].color * atten;

        Lo += shadeDirectLight(N, V, L, radiance, albedo, metallic, roughness, F0);
    }

    // --- Spot lights (unshadowed) ---
    for (int i = 0; i < uNumSpotLights; ++i) {
        vec3 toLight = uSpotLights[i].position - vFragPos;
        float distance = length(toLight);
        vec3 L = normalize(toLight);

        float atten =
            attenuationFor(uSpotLights[i].constant, uSpotLights[i].linear, uSpotLights[i].quadratic, distance);

        float theta = dot(L, normalize(-uSpotLights[i].direction));
        float epsilon = max(uSpotLights[i].innerCutoff - uSpotLights[i].outerCutoff, 0.0001);
        float spotIntensity = smoothstep(0.0, 1.0, (theta - uSpotLights[i].outerCutoff) / epsilon);

        vec3 radiance = uSpotLights[i].color * atten * spotIntensity;
        Lo += shadeDirectLight(N, V, L, radiance, albedo, metallic, roughness, F0);
    }

    // Ambient: real split-sum image-based lighting (Karis, "Real Shading in
    // Unreal Engine 4"), replacing Phase 9's flat
    // `uAmbientColor * albedo * ao * (1 - metallic)` placeholder -- see that
    // formula's own removed comment, which named this exact upgrade as
    // Phase 10's job. uAmbientColor itself is no longer read here at all: the
    // whole point of IBL is that the environment's own convolved radiance
    // (uIrradianceMap/uPrefilterMap, both derived from the actual skybox --
    // see engine::IBLProbe) replaces a single hand-picked flat-ambient
    // constant with a direction- and roughness-aware one.
    //
    // kS/kD split the incoming ambient energy between specular and diffuse
    // exactly like shadeDirectLight()'s own kS/kD above, just using the
    // roughness-aware Fresnel variant (see fresnelSchlickRoughness()'s
    // comment) rather than plain fresnelSchlick(): direct lighting evaluates
    // Fresnel at one specific light/view/half-vector geometry per light, but
    // IBL's diffuse term has no single light direction to evaluate a
    // per-light Fresnel term against, so it uses N/V directly (the same
    // "how much of *all* incoming ambient light this surface reflects
    // specularly vs. diffusely, from this view angle" role kS/kD play for one
    // direct light, but averaged over the whole environment instead of one
    // incoming ray).
    vec3 kS = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    vec3 irradiance = texture(uIrradianceMap, N).rgb;
    vec3 diffuseIBL = irradiance * albedo;

    // The prefiltered map's mip chain is indexed by roughness directly
    // (roughness 0 -> mip 0, roughness 1 -> the last mip, see
    // MAX_REFLECTION_LOD/IBLProbe::kPrefilterMipLevels) -- textureLod (not a
    // plain texture() call) is required here specifically because this LOD
    // must be driven by the material's own roughness value, not by screen-
    // space derivatives the way GL's automatic mip selection would pick for
    // an ordinarily-sampled texture.
    vec3 R = reflect(-V, N);
    vec3 prefilteredColor = textureLod(uPrefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 envBRDF = texture(uBrdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specularIBL = prefilteredColor * (F0 * envBRDF.x + envBRDF.y);

    vec3 ambient = (kD * diffuseIBL + specularIBL) * ao;

    FragColor = vec4(ambient + Lo, 1.0);
}
