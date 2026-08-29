#version 430 core

// Phase 13d: bumped from #version 330 core to 430 core -- see basic.frag's
// identical comment (clustered lighting's per-cluster light-list SSBO
// requires GLSL 4.30's Shader Storage Buffer Objects).
//
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
// (no uAmbientColor here -- Phase 10 replaced this shader's flat ambient
// placeholder with real image-based lighting; see uIrradianceMap/
// uPrefilterMap/uBrdfLUT below and this file's own Phase 10 comment. basic.frag
// still has its own uAmbientColor for the unrelated Blinn-Phong path.)
uniform vec3 uLightDirection;
uniform vec3 uLightColor;
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

// Phase 13f: screen-space ambient occlusion -- see assets/shaders/ssao.frag/
// ssao_blur.frag and application.cpp's Phase 13f render() additions. A
// per-pixel, geometry-derived occlusion factor (1.0 = fully open, 0.0 =
// fully occluded) sampled at this fragment's own screen position
// (gl_FragCoord.xy / uScreenSize -- uScreenSize already exists below for
// Phase 13d's clustered-lighting tile lookup) and multiplied into the
// ambient term below, alongside (not instead of) uAO/the ORM map's own
// material-authored AO above: uAO represents baked-in per-surface detail a
// texture author chose (independent of any other geometry in the scene),
// while this SSAO term represents *this frame's* actual nearby geometry
// blocking ambient light (a box sitting on a table darkens the contact
// crease between them regardless of either surface's own material AO) --
// the standard practice is to multiply both together since they model two
// different, independent sources of the same "how occluded is ambient light
// here" question. uSSAOEnabled (see application.cpp's ENGINE_SSAO_DISABLE)
// lets a headless run compare this shader's output with SSAO's exact
// contribution isolated versus forced off, without needing a second build.
uniform sampler2D uSSAOMap;
uniform int uSSAOEnabled;

// Phase 18f: Solid shading mode -- see basic.frag's own uSolidShading
// comment for the full design; the identical flag, just gating this
// shader's own uAlbedoMap sample instead. When true (nonzero), albedo below
// stays exactly uAlbedo (the material's own flat scalar color), completely
// skipping the uAlbedoMap texture() lookup -- every other PBR term
// (metallic/roughness/normal/lighting/IBL) is untouched, so shape/shading
// still reads correctly, just without a diffuse/albedo texture.
uniform int uSolidShading;

// Phase 13g: Screen-Space Reflections -- refines the IBL-only specular term
// below (specularIBL, Phase 10's prefiltered-environment-cubemap reflection)
// for smooth/mirror-like surfaces by ray-marching this frame's already-fully-
// rendered opaque scene color (uSSRColorBuffer -- Application's
// hdrResolveFramebuffer_, resolved once already before this shader's SSR
// compositing draw runs) against SSAO's own view-space normal + depth
// pre-pass (uSSRDepthMap -- Application's ssaoGBuffer_ depth texture; see
// that pass's own comment in application.cpp/gbuffer.vert for why this
// engine's existing SSAO G-buffer is exactly the input SSR needs too, with
// no second redundant geometry pre-pass). uSSREnabled is 0 during this
// shader's ordinary once-per-frame draw of the sphere grid (Application's
// first PBR draw call, alongside every other entity) -- pbr.frag's own
// IBL-only ambient term is computed exactly as Phase 10 left it there -- and
// only set to 1 when Application redraws the sphere grid a SECOND time,
// immediately after resolving the first pass's color, into the same
// off-screen target (see Application::renderSSRComposite()). SSR
// fundamentally needs to sample a color buffer that's already finished
// rendering, which this shader's own single forward pass's output obviously
// isn't while it's mid-flight producing that exact buffer -- hence the
// two-pass split rather than trying to fold this into one pass (see
// application.hpp's own Phase 13g header comment for the full rationale).
// uView/uProjection are new this phase (every previous computation in this
// shader worked entirely in world space, or -- for uProjection -- had no
// reason to reproject a view-space position back to screen space at all);
// they're only needed to bring this fragment's position/normal into the
// same view space the ray march (and SSAO's own depth reconstruction)
// already operates in, and to re-project each marched step back to screen
// space to sample uSSRDepthMap/uSSRColorBuffer.
//
// Phase 13g bug fix: uSSRColorBuffer's own texture now carries a full mip
// chain (hdrResolveFramebuffer_ built with mipmappedColor = true, refreshed
// every frame via Framebuffer::generateColorMipmaps() -- see application.cpp)
// rather than a single level, sampled below with textureGrad rather than a
// plain texture() call -- see that call site's own comment for the reflection-
// aliasing bug this fixes.
uniform mat4 uView;
uniform mat4 uProjection;
uniform sampler2D uSSRColorBuffer;
uniform sampler2D uSSRDepthMap;
uniform mat4 uSSRInvProjection;
uniform int uSSREnabled;

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

// Phase 13d: clustered light culling -- identical mechanism to basic.frag's
// own copy of this same block (same duplication-by-design rationale as
// every other reused block in this file, see this file's own header
// comment). Each light's data stays in uPointLights[]/uSpotLights[] above,
// unchanged; only which/how many of those entries a fragment loops over
// changes -- see computeClusterIndex()/main() below.
#define CLUSTER_GRID_X 12
#define CLUSTER_GRID_Y 8
#define CLUSTER_GRID_Z 24

struct ClusterLightList {
    uint pointCount;
    uint pointIndices[MAX_POINT_LIGHTS];
    uint spotCount;
    uint spotIndices[MAX_SPOT_LIGHTS];
};

layout(std430, binding = 1) readonly buffer ClusterLightListBuffer {
    ClusterLightList lightLists[];
};

uniform vec2 uScreenSize;
uniform float uClusterNearPlane;
uniform float uClusterFarPlane;
uniform int uClusterDebug;

uint computeClusterIndex() {
    vec2 tileSize = uScreenSize / vec2(float(CLUSTER_GRID_X), float(CLUSTER_GRID_Y));
    uint clusterX = uint(clamp(gl_FragCoord.x / tileSize.x, 0.0, float(CLUSTER_GRID_X - 1)));
    uint clusterY = uint(clamp(gl_FragCoord.y / tileSize.y, 0.0, float(CLUSTER_GRID_Y - 1)));

    float depth = max(vViewSpaceDepth, uClusterNearPlane);
    float zRatio =
        log(depth / uClusterNearPlane) * (float(CLUSTER_GRID_Z) / log(uClusterFarPlane / uClusterNearPlane));
    uint clusterZ = uint(clamp(floor(zRatio), 0.0, float(CLUSTER_GRID_Z - 1)));

    return clusterX + clusterY * uint(CLUSTER_GRID_X) + clusterZ * uint(CLUSTER_GRID_X) * uint(CLUSTER_GRID_Y);
}

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

// --- Phase 13g: screen-space reflection ray march ---

// Fixed ray-march tuning for this small, roughly-1-world-unit-scale scene
// (see application.cpp's kSSAORadius comment on this engine's scale) -- not
// exposed as uniforms/env-tunable, since nothing in this phase's brief asks
// for runtime tuning, just a "good enough, functionally correct" fixed
// march. kSSRMaxDistance (3 view-space units) comfortably covers the PBR
// sphere grid's own footprint (two rows of four spheres, application.cpp's
// kSphereColSpacing = 0.6 apart) plus the ground plane behind it;
// kSSRMaxSteps (28) at that distance gives a ~0.107-unit step -- coarser
// than this scene's own kSphereRadius (0.14), but the kSSRRefineSteps
// binary search right after each coarse hit narrows that back down well
// past sub-object precision (see its own comment), and this project's
// software rasterizer (llvmpipe) makes every dependent-texture-fetch loop
// iteration here costly enough (this pass's own early-out gate above this
// function's call site trims *which* fragments run it at all, but not how
// long the ones that do take) that a smaller step count than this
// technique's usual 48-64-step reference range is the right tradeoff for
// this small hand-authored scene, the same "good enough, not a AAA
// production title" reasoning kSSAOKernelSize's own comment already makes.
// kSSRRefineSteps (4) is a small binary-search refinement once the coarse
// march below finds a bracketing pair of steps, halving the initial
// ~0.107-unit uncertainty down under 0.007 units in 4 halvings -- the
// standard fix for a coarse march's own banding (a hit only known to within
// one whole step otherwise, which visibly quantizes the reflection into
// stair-stepped bands). kSSRThickness gates how far behind the sampled
// surface a hit is still accepted as *that* surface, rather than the ray
// having sailed past a thin occluder into unrelated, much deeper geometry
// (the classic SSR self-intersection/false-hit failure mode) -- scaled to
// the march's own per-step distance rather than a fixed absolute, so it
// stays proportionate if kSSRMaxDistance/kSSRMaxSteps above are ever
// retuned.
//
// Phase 13g bug review #2: kSSRThickness was originally 2 steps' worth
// (~0.214 units). That margin is generous enough to hide a second, distinct
// false-hit mechanism from the mottled-reflection bug this file's other
// Phase 13g bug-fix comment already covers -- a headless screenshot's
// top-right sphere (this time between the specular highlight and the
// terminator, ENGINE_SSR_DISABLE=1 confirming it away completely) showed a
// jagged, disconnected dark patch sitting in the middle of an otherwise
// smooth shadow gradient, unmoved by that first fix (identical before/after
// its textureGrad change). A debug build that colored each accepted hit by
// its coarse-march step index (i.e. how far along the ray the crossing was
// found) showed the difference immediately: fragments just outside the
// patch correctly found their hit late in the march (step ~16 of 28, out on
// the reflected floor, matching the smoothly-varying hitUV neighboring
// fragments already show), while fragments inside the patch found a "hit"
// at step ~4-5 -- a plateau of near-identical, suspiciously early hitUVs
// sandwiched inside what should have been one continuous ramp. Decoding
// those early hitUVs back to screen pixels landed on an ordinary,
// unremarkable patch of the same checkerboard floor -- not a silhouette,
// not the sphere itself (self-intersection is still geometrically
// impossible off a convex surface, see this file's other Phase 13g
// comment), just a legitimate point on a legitimate surface, reached far
// too early. The cause: uSSRDepthMap is SSAO's existing G-buffer depth,
// rendered at HALF the window's resolution purely for SSAO's own
// affordability (kSSAODownsampleFactor, see application.cpp) and reused
// here to avoid a second full-res geometry pre-pass (see this file's Phase
// 13g header comment) -- but that buffer is a SEPARATE rasterization of the
// same triangles at coarser pixel centers, not a downsampled copy of the
// full-res one this fragment's own march otherwise reasons about, so it can
// legitimately disagree with the true surface by a small amount right where
// depth is changing fast across screen space -- exactly the grazing/
// silhouette angles this sphere's edge already amplifies into large per-
// fragment hitUV swings (see the textureGrad fix's own comment). A 2-step
// margin was large enough to swallow that small disagreement as "still the
// surface" at an early march step, where a fixed view-space margin covers a
// much larger fraction of the (so-far-tiny) distance traveled than the same
// margin does later in the march -- accepting a too-early, wrong hit for
// some fragments (whichever land squarely on the low-res buffer's own
// disagreement) while immediate neighbors narrowly avoid it and correctly
// keep marching to the true, far intersection everyone should share. A
// neighbor-texel depth-discontinuity check was tried first (rejecting a hit
// whose surrounding uSSRDepthMap texels disagreed sharply, the standard
// "false hit at a silhouette edge" guard) and measured to have *zero*
// effect here -- confirming this isn't an edge/discontinuity at all, just
// an otherwise-smooth surface sampled at a slightly wrong position, which a
// discontinuity check can't see. Tightening kSSRThickness directly closes
// the gap for *this* one sphere/angle -- but see this constant's own bug
// review #3 comment below for why applying that tight a margin at the
// *coarse* march step, as this fix originally did, was itself a new bug.
//
// Phase 13g bug review #3: kSSRThickness=0.1 steps' worth (~0.0107 units),
// gating the COARSE march step the way bug review #2 left it, rejects
// nearly every legitimate hit in the entire scene, not just the false ones
// that fix targeted -- confirmed with a debug build that colored every
// traceSSR() call site green/magenta by found/!found (see this file's other
// Phase 13g comments for the technique): every sphere smooth enough for
// roughnessFade/grazingFade to even attempt a march came back almost solid
// magenta, correctly reflecting only a thin ring right at the silhouette
// (see main()'s own grazingFade) rather than the sharp checkerboard/sphere
// reflections a working SSR pass should show across most of each sphere's
// face. The reason: kSSRThickness is a gate on `sceneViewZ - currPos.z`,
// the gap between the ray and the recorded surface *at the coarse step that
// first lands behind it* -- and that gap is NOT bounded by anything related
// to how "thick" the true surface is; it is simply "how far past the true
// crossing did this one ~0.107-unit step happen to overshoot", which for a
// ray traveling mostly along view-space Z (the common case for any
// reflection that isn't near-grazing) is uniformly distributed between 0
// and nearly a full step's own Z-extent -- routinely several times
// kSSRThickness's ~0.0107-unit margin. Bug review #2's own sweep (verifying
// down to 0.1 "does not measurably improve further" and "shows no visible
// loss of the legitimate checkerboard reflections elsewhere on the grid")
// evidently never actually rendered anything past the one previously-broken
// sphere/angle it was chasing -- a full ENGINE_SSR_DISABLE=1 diff (this
// review's own first check) shows differences only in a thin ring at every
// sphere's silhouette, exactly where grazingFade below still lets a sliver
// of SSR through; the debug hit-map above shows why: everywhere else,
// traceSSR() was simply returning false. The fix: gate the SAME
// kSSRThickness constant against the REFINED (post-bisection) gap instead
// of the coarse one -- kSSRRefineSteps' own binary search already narrows
// the bracket toward wherever `currPos.z <= sceneViewZ` first flips, and for
// an ordinary, consistently-sampled surface that drives the residual gap
// toward zero regardless of how much the initial coarse step overshot (the
// bisection is homing in on the true crossing, not the coarse sample), so a
// tight margin now correctly accepts it. The original false-hit case this
// constant exists for is different in kind, not just degree: it comes from
// uSSRDepthMap disagreeing with the true continuous surface over a
// *discrete, roughly constant-within-one-low-res-texel* span (see bug
// review #2's own diagnosis) -- bisecting within that span keeps re-reading
// essentially the same wrong recorded depth while `mid.z` barely moves, so
// the residual gap does NOT collapse toward zero the way a real surface's
// does, and the tight post-refinement margin still rejects it. Re-verified
// with the same debug hit-map (now predominantly green, matching a working
// SSR pass) and the same before/after luminance comparison bug review #2
// used on its own previously-broken sphere (unchanged, confirming that fix
// itself still holds).
//
// Phase 13g bug review #4: bug review #3's re-verification above was itself
// incomplete -- a full headless screenshot of the 8-sphere grid (not just
// the two individual spheres bug reviews #2/#3 had each separately been
// chasing) showed 4 of the 8 spheres (every one smooth/metallic enough that
// roughnessFade and a low diffuse contribution -- see shadeDirectLight's own
// `kD` -- don't mask a wrong specular term) with a bizarre swirling/donut-
// shaped distortion centered on the sphere, nothing like a reflection,
// confirmed SSR-specific by ENGINE_SSR_DISABLE=1 rendering all 8 cleanly.
// A debug build visualizing raw hitUV directly (this file's own established
// technique, extended with an out parameter for the coarse crossing's own
// step index and view-space position) showed the actual bug: nearly EVERY
// fragment on EVERY sphere -- not just the 4 visibly-swirling ones --
// crossed at coarse step i=1 or i=2, with hitUV varying wildly and
// non-monotonically (a visible vortex in the raw hitUV colors themselves)
// exactly where the swirl appears in the final shaded image. That is far too
// early and far too unstable to be this scene's real geometry: kSphereRadius
// = 0.14 and kSphereColSpacing = 0.6 (see application.cpp) mean a genuine
// first reflection target is essentially never found within the first
// couple of ~0.107-unit coarse steps for a ray leaving a sphere's surface in
// a non-grazing direction.
//
// The actual cause was hiding in plain sight in the coarse march loop
// itself, not in this constant: bug review #3's refactor (splitting
// "detect a crossing" from "judge a crossing," so kSSRThickness could move
// to the refined gap) accidentally deleted the crossing test itself along
// the way. The comment immediately above the coarse loop's crossing-record
// still describes "the ray has intersected real geometry once it goes
// behind... the actual surface stored there" -- but the code below it no
// longer checks that at all; it unconditionally treats the FIRST screen
// pixel with any non-background depth as a crossing, whether or not
// `currPos` has actually gone behind that depth yet. Since a reflection
// ray's very first ~0.107-unit step from a sphere's own surface almost
// always still projects to a screen pixel showing *some* real geometry
// (this sphere's own silhouette, the floor right underneath it, a
// neighboring object), that fires immediately for nearly every fragment.
// The subsequent bisection then refines between viewPos (the ray's own
// origin) and that first, essentially-arbitrary step -- a bracket so
// close to the reflecting fragment's own surface that it converges to a
// small, stable residual gap almost every time (passing kSSRThickness's
// tight post-refinement margin, exactly as bug review #3 intended it to for
// a *real* surface), yielding a confidently-accepted hitUV that is really
// just wherever that first short, near-tangent step happened to land --
// hypersensitive to the exact reflect direction per bug review #1's own
// convex-mirror angular-amplification finding, which is what turns a smooth
// sweep across a sphere's face into the observed swirl. All 8 spheres run
// through this same broken march equally; only 4 show it visually because
// the other 4 sit at a metallic/roughness combination where roughnessFade
// is already small (fading the wrong result back out) or `kD` is large
// enough that the correct diffuse term still dominates the final blend
// (see shadeDirectLight/ambient's own `kD`/`ao`/`ssao` weighting) -- the bug
// was never confined to 4 spheres, only its visibility was.
//
// The fix: restore the missing `currPos.z <= sceneViewZ` crossing test in
// the coarse loop below (making "keep marching, this step is still in front
// of whatever's at this screen pixel" possible again, not just "stop
// immediately"), while keeping bug review #3's own, still-correct insight of
// judging kSSRThickness against the refined gap rather than the coarse one.
// Re-verified with the same raw-hitUV debug build (now a smooth, monotonic
// gradient across every sphere's face, no vortex on any of the 8) and a full
// 8-sphere headless screenshot (all 4 previously-swirling spheres clean, the
// other 4 and bug review #2's own previously-fixed sphere unchanged).
const int kSSRMaxSteps = 28;
const float kSSRMaxDistance = 3.0;
const int kSSRRefineSteps = 4;
const float kSSRThickness = 0.1 * (kSSRMaxDistance / float(kSSRMaxSteps));

// Reconstructs a view-space position from a screen UV + raw device depth --
// identical math to ssao.frag's own reconstructViewPos() (necessarily
// duplicated, not shared -- this engine's Shader class links each program
// from exactly one vertex + one fragment file with no #include support, the
// same reason every other reused block in this file is copied rather than
// shared, see this file's own header comment).
vec3 ssrReconstructViewPos(vec2 uv, float depth) {
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = uSSRInvProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

// Marches `viewReflectDir` from `viewPos` through view space, looking for
// the first step whose device depth (projected back to screen space) falls
// behind uSSRDepthMap's own stored depth -- i.e. real geometry now occupies
// that screen pixel nearer the camera than the ray itself, meaning the ray
// has struck it. Returns false (no hit) if the ray leaves the screen, goes
// behind the camera, or reaches kSSRMaxDistance with nothing struck -- the
// classic "SSR simply has no data here" cases (a reflection whose true
// source lies off-screen, or one that never meets anything within this
// scene's own small scale) -- callers fall back to the existing IBL term in
// every one of those cases (see main()'s own fade logic below).
bool traceSSR(vec3 viewPos, vec3 viewReflectDir, out vec2 hitUV) {
    vec3 rayStep = viewReflectDir * (kSSRMaxDistance / float(kSSRMaxSteps));
    vec3 prevPos = viewPos;
    vec3 currPos = viewPos;
    bool crossed = false;
    // Phase 13g bug review #3: crossingUV/crossingSceneViewZ record the
    // coarse march's own crossing sample (screen UV + the recorded surface's
    // view-space Z there) without judging it yet -- kSSRThickness is no
    // longer applied at this coarse resolution at all, see that constant's
    // own bug review #3 comment for why.
    //
    // Phase 13g bug review #4: see kSSRThickness's own bug review #4 comment
    // for the full story -- the crossing test below (`currPos.z <=
    // sceneViewZ`) is not optional bookkeeping, it is what makes this a
    // *crossing* at all. Losing it (review #3's actual regression) turns
    // this loop into "accept the first screen pixel with any foreground
    // depth at all," true at i==1 for almost every fragment in this scene.
    vec2 crossingUV = vec2(0.0);
    float crossingSceneViewZ = 0.0;

    for (int i = 1; i <= kSSRMaxSteps; ++i) {
        prevPos = currPos;
        currPos = viewPos + rayStep * float(i);

        vec4 clip = uProjection * vec4(currPos, 1.0);
        if (clip.w <= 0.0) {
            return false;  // behind the camera -- nothing meaningful to project
        }
        vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            return false;  // ray exited the visible frame
        }

        float sceneDepth = texture(uSSRDepthMap, uv).r;
        // The far plane / background -- nothing for the ray to strike at
        // this screen position, keep marching rather than reconstructing a
        // meaningless far-plane position (mirrors ssao.frag's own
        // background check).
        if (sceneDepth >= 1.0) {
            continue;
        }

        float sceneViewZ = ssrReconstructViewPos(uv, sceneDepth).z;
        // View-space Z is negative in front of the camera and grows more
        // negative with distance (this engine's usual convention, see
        // pbr.vert's vViewSpaceDepth comment) -- the ray has intersected
        // real geometry once it goes behind (a more-negative Z than) the
        // actual surface stored there.
        //
        // Phase 13g bug review #4: this `if` is the crossing test itself --
        // see kSSRThickness's own bug review #4 comment for why it must gate
        // whether the loop even treats this step as a crossing, not just
        // whether marching continues. Record the crossing and stop marching
        // once it fires -- see kSSRThickness's bug review #3 comment for why
        // accepting/rejecting *this* crossing is still deferred to after
        // refinement, not done here.
        if (currPos.z <= sceneViewZ) {
            crossed = true;
            crossingUV = uv;
            crossingSceneViewZ = sceneViewZ;
            break;
        }
        // Still in front of the recorded surface at this screen position --
        // real, unrelated geometry the ray hasn't reached yet (e.g. this
        // same sphere's own nearby silhouette, or another object entirely).
        // Keep marching rather than stopping here (bug review #4's fix --
        // see that comment for what stopping unconditionally, as this loop
        // used to, actually broke).
    }

    if (!crossed) {
        return false;
    }

    // Binary-search refinement between prevPos (the last step still in
    // front of the surface) and currPos (the first step behind it) -- see
    // kSSRRefineSteps' own comment above for why. hi/hiUV/hiSceneViewZ track
    // the closest-known "still behind the surface" sample as the bracket
    // narrows -- seeded from the coarse crossing itself (crossingUV/
    // crossingSceneViewZ above), since currPos/crossingSceneViewZ already
    // *is* that sample before the first halving ever runs.
    vec3 lo = prevPos;
    vec3 hi = currPos;
    vec2 hiUV = crossingUV;
    float hiSceneViewZ = crossingSceneViewZ;
    for (int i = 0; i < kSSRRefineSteps; ++i) {
        vec3 mid = 0.5 * (lo + hi);
        vec4 clip = uProjection * vec4(mid, 1.0);
        vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
        float sceneDepth = texture(uSSRDepthMap, uv).r;
        // An off-screen/background midpoint reads as "still in front of the
        // surface" (a very distant, very-negative Z) rather than
        // reconstructing a meaningless position for it.
        float sceneViewZ = sceneDepth >= 1.0 ? -1.0e6 : ssrReconstructViewPos(uv, sceneDepth).z;
        if (mid.z <= sceneViewZ) {
            hi = mid;
            hiUV = uv;
            hiSceneViewZ = sceneViewZ;
        } else {
            lo = mid;
        }
    }

    // Phase 13g bug review #3: kSSRThickness applied HERE, against the
    // refined/converged gap, rather than at the coarse march step above --
    // see that constant's own bug review #3 comment for the full story on
    // why gating the coarse step broke almost every legitimate hit in the
    // scene, and why this refined gap is the one that actually distinguishes
    // a real surface (converges toward 0) from the false-hit case this
    // constant was originally introduced for (does not).
    if (hiSceneViewZ - hi.z >= kSSRThickness) {
        return false;
    }
    hitUV = hiUV;
    return true;
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
    // Phase 18f: Solid mode skips the texture sample outright -- see
    // uSolidShading's own comment above.
    if (uUseAlbedoMap != 0 && uSolidShading == 0) {
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

    // Phase 13d: this fragment's own cluster + its culled point/spot light
    // counts -- see basic.frag's identical comment.
    uint clusterIndex = computeClusterIndex();
    uint clusterPointCount = lightLists[clusterIndex].pointCount;
    uint clusterSpotCount = lightLists[clusterIndex].spotCount;

    // --- Point lights (unshadowed), culled to this fragment's own cluster ---
    for (uint li = 0u; li < clusterPointCount; ++li) {
        uint i = lightLists[clusterIndex].pointIndices[li];
        vec3 toLight = uPointLights[i].position - vFragPos;
        float distance = length(toLight);
        vec3 L = normalize(toLight);

        float atten =
            attenuationFor(uPointLights[i].constant, uPointLights[i].linear, uPointLights[i].quadratic, distance);
        vec3 radiance = uPointLights[i].color * atten;

        Lo += shadeDirectLight(N, V, L, radiance, albedo, metallic, roughness, F0);
    }

    // --- Spot lights (unshadowed), culled to this fragment's own cluster ---
    for (uint li = 0u; li < clusterSpotCount; ++li) {
        uint i = lightLists[clusterIndex].spotIndices[li];
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

    // Phase 13g: refine the IBL-only specular term above with a real
    // screen-space ray-marched reflection where one is available -- see
    // this file's own Phase 13g comment (uSSREnabled) and traceSSR() above.
    // Only evaluated during Application's second, SSR-compositing draw of
    // the sphere grid.
    //
    // Roughness/grazing-angle fades are computed FIRST, before ever calling
    // traceSSR() -- both are known from data this fragment already has
    // (roughness; N/V), independent of whether a ray march would even find a
    // hit. Bailing out here when either is already ~0 (a rough surface, or a
    // grazing/edge-on view angle -- see each fade's own comment below) skips
    // traceSSR()'s dependent-texture-fetch loop entirely for exactly the
    // fragments whose SSR contribution would be faded back to ~0 anyway,
    // rather than paying for a multi-step ray march whose result is then
    // thrown away -- meaningfully cheaper on this project's software-
    // rasterizer (llvmpipe) headless verification target, where this
    // engine's other screen-space passes (SSAO) already downsample/shrink
    // their own kernel specifically to stay affordable there (see
    // application.cpp's kSSAODownsampleFactor/kSSAOKernelSize comments).
    if (uSSREnabled != 0) {
        // Grazing/edge-on view angle fade: SSR's screen-space march itself
        // (not the reflected geometry) becomes unreliable at near-90-degree
        // view angles -- the classic SSR artifact zone, where the marched
        // ray runs nearly parallel to the screen, so a small screen-space
        // step covers a large, imprecise view-space distance. NdotV is
        // already computed just below for the IBL Fresnel term; computed
        // here too since it's needed before that point in the code.
        float ndotV = max(dot(N, V), 0.0);
        float grazingFade = smoothstep(0.02, 0.25, ndotV);

        // Roughness fade: SSR is only meaningful for fairly smooth,
        // mirror-like surfaces (a single sharp ray-marched hit standing in
        // for what should, at higher roughness, be a broad blur a
        // one-sample-per-fragment ray march can't produce) -- this fades
        // fully back to the existing prefiltered-IBL term (already
        // correctly blurred by roughness via uPrefilterMap's own mip
        // selection above) past a modest roughness.
        float roughnessFade = 1.0 - smoothstep(0.35, 0.6, roughness);

        if (grazingFade > 0.001 && roughnessFade > 0.001) {
            vec3 viewPos = vec3(uView * vec4(vFragPos, 1.0));
            vec3 viewNormal = normalize(mat3(uView) * N);
            vec3 viewReflectDir = reflect(normalize(viewPos), viewNormal);

            vec2 hitUV;
            if (traceSSR(viewPos, viewReflectDir, hitUV)) {
                // Screen-edge fade: the classic SSR blind spot -- a
                // reflection whose hit point lies near the frame's own edge
                // is one whose true reflected geometry may extend just
                // off-screen, where this technique (unlike real ray
                // tracing) has no data at all; fading over the outer 20% of
                // the screen avoids a hard, visible pop as such a hit's UV
                // slides past the edge from frame to frame.
                vec2 edgeDist = min(hitUV, vec2(1.0) - hitUV);
                float screenEdgeFade = smoothstep(0.0, 0.2, min(edgeDist.x, edgeDist.y));

                float ssrFade = clamp(screenEdgeFade * grazingFade * roughnessFade, 0.0, 1.0);

                // Phase 13g bug fix: textureGrad (not a plain texture()
                // call) against uSSRColorBuffer's own mip chain (see
                // Application's hdrResolveFramebuffer_ construction/
                // generateColorMipmaps() calls), fed this fragment's actual
                // dFdx/dFdy of hitUV rather than letting GL derive an
                // implicit LOD from screen-space derivatives of the input
                // texture coordinate the ordinary way. hitUV isn't an
                // ordinary interpolated texture coordinate -- it's the
                // *output* of a per-fragment ray march reflecting off this
                // curved sphere, and reflection off a convex mirror doubles
                // angular sensitivity relative to the surface itself (the
                // textbook "curved mirror" reflection-vector derivative),
                // so hitUV can shift by many screen pixels between two
                // adjacent fragments even where the surface normal barely
                // changes -- worst right at grazing/silhouette angles,
                // exactly grazingFade's own admitted range. Sampling a
                // single mip-0 texel per fragment there (this bug's original
                // form) undersamples the reflected checkerboard ground
                // plane's own sharp, high-frequency squares -- adjacent
                // fragments land on wildly different, sometimes differently-
                // colored squares -- which reads as a mottled/checkered
                // noisy patch rather than a clean reflected gradient, not
                // any kind of self-intersection (verified: a sphere is
                // strictly convex, so reflect()'s own construction
                // guarantees the marched ray immediately enters the
                // *outward* half-space of every point it leaves and can
                // never geometrically reintersect that same convex surface;
                // a debug build that visualized raw hitUV directly showed a
                // smoothly-varying hit location here, and decoding it back
                // to screen pixels landed squarely on the reflected floor's
                // alternating blue/tan squares, not on this sphere or its
                // neighbor). textureGrad's explicit derivatives let it
                // average over exactly the texel footprint this fragment's
                // reflection actually spans, the same fix ordinary texture
                // minification gets from mipmapping automatically -- this
                // buffer just needed the same treatment applied manually,
                // since its lookup coordinate is computed, not interpolated.
                vec2 hitUVDx = dFdx(hitUV);
                vec2 hitUVDy = dFdy(hitUV);
                vec3 ssrColor = textureGrad(uSSRColorBuffer, hitUV, hitUVDx, hitUVDy).rgb;
                // Weighted by the same split-sum specular term specularIBL
                // itself uses (F0 * envBRDF.x + envBRDF.y) -- SSR only
                // replaces *where the reflected radiance comes from* (the
                // actual nearby scene instead of the distant prefiltered
                // environment), not how strongly this surface reflects it
                // at all.
                vec3 ssrSpecular = ssrColor * (F0 * envBRDF.x + envBRDF.y);
                specularIBL = mix(specularIBL, ssrSpecular, ssrFade);
            }
        }
    }

    // Phase 13f: screen-space AO, sampled at this fragment's own screen
    // position (the same gl_FragCoord/uScreenSize UV computeClusterIndex()
    // above already uses) and multiplied in alongside the material's own
    // scalar/ORM-map AO -- see uSSAOMap's own comment above for why both.
    float ssao = uSSAOEnabled != 0 ? texture(uSSAOMap, gl_FragCoord.xy / uScreenSize).r : 1.0;
    vec3 ambient = (kD * diffuseIBL + specularIBL) * ao * ssao;

    vec3 finalColor = ambient + Lo;

    // Phase 13d debug visualization (ENGINE_CLUSTER_DEBUG=1) -- identical to
    // basic.frag's own version, see that shader's comment.
    if (uClusterDebug != 0) {
        float totalCount = float(clusterPointCount + clusterSpotCount);
        float maxCount = float(MAX_POINT_LIGHTS + MAX_SPOT_LIGHTS);
        vec3 heat = vec3(totalCount / maxCount, 0.15, 1.0 - totalCount / maxCount);
        finalColor = mix(finalColor, heat, 0.5);
    }

    FragColor = vec4(finalColor, 1.0);
}
