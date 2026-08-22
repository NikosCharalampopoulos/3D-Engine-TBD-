#version 330 core

// Phase 4: textured Blinn-Phong lighting, replacing Phase 2-3's flat
// per-draw-call uColor.
//
// Phase 7a extends this in three ways:
//   1. Multiple lights: the original single directional light is joined by
//      a fixed-size array of point lights and spot lights (uNumPointLights/
//      uNumSpotLights say how many of each array's entries are actually
//      live -- the standard "fixed uniform array + count" forward-rendering
//      pattern, see uPointLights/uSpotLights below). Point/spot use the
//      standard 1/(constant + linear*d + quadratic*d^2) attenuation; spot
//      additionally fades between its inner and outer cutoff angle via
//      smoothstep (a soft edge) rather than a hard binary cutoff.
//   2. Normal mapping: if uUseNormalMap is set, the surface normal used for
//      lighting comes from sampling uNormalMap through a per-fragment TBN
//      matrix instead of the interpolated vertex normal directly -- see
//      the TBN construction in main() below.
//   3. Shadow mapping: the directional light's contribution (only) is
//      reduced for fragments the light itself couldn't see -- see
//      shadowFactor() below. Point/spot lights are unaffected (no shadow
//      maps exist for them this phase, per the phase brief).
in vec3 vNormal;
in vec3 vFragPos;
in vec2 vTexCoord;
in vec3 vTangent;
// Phase 13c: replaces vFragPosLightSpace -- see basic.vert's comment.
in float vViewSpaceDepth;

out vec4 FragColor;

uniform sampler2D uDiffuseTexture;
uniform vec3 uTint;
uniform float uShininess;

// Optional tangent-space normal map (see engine::Material) -- uUseNormalMap
// is uploaded every Material::bind() call (never left stale from a
// previous, different material sharing this program), so branching on it
// is always meaningful for the material actually being drawn this call.
uniform sampler2D uNormalMap;
uniform int uUseNormalMap;

// Directional light: uLightDirection points *from* the light *toward* the
// scene (the usual "sun ray direction" convention), so surfaces are lit
// along -uLightDirection. This is the one shadow-casting light this phase
// implements (see uShadowMap0/1/2/shadowFactor() below).
uniform vec3 uLightDirection;
uniform vec3 uLightColor;
uniform vec3 uAmbientColor;
uniform vec3 uViewPos;

// Phase 13c: Cascaded Shadow Maps. NUM_CASCADES separate depth maps (see
// engine::ShadowMap/Application::shadowCascades_), each covering a
// different depth range of the camera's own view frustum, replace the
// single whole-scene shadow map Phase 7a/7b used -- see
// application.cpp's computeCascades() for how each cascade's own light-
// space matrix/depth range is built. uLightSpaceMatrices[i] is a mat4
// array (not opaque -- see below), so it can be dynamically indexed by a
// per-fragment cascade index with no restriction.
#define NUM_CASCADES 3
uniform mat4 uLightSpaceMatrices[NUM_CASCADES];
// The far edge (view-space depth) of cascades 0..NUM_CASCADES-1 -- cascade
// i covers the depth range (uCascadeSplits[i-1], uCascadeSplits[i]]
// (cascade 0's own near edge is implicitly the camera's own near plane,
// never needed here since every visible fragment's depth is already at
// least that).
uniform float uCascadeSplits[NUM_CASCADES];
// GLSL 330 core does NOT guarantee a sampler (an opaque type) array can be
// indexed by a non-constant, per-fragment-varying expression -- that
// guarantee only arrives with GL_ARB_gpu_shader5/GLSL 400's "dynamically
// uniform expression" rules, neither of which this engine's GL 3.3 core
// target requires or can rely on across drivers. Three separate named
// samplers plus an explicit branch (sampleCascadeDepth() below) sidesteps
// the question entirely rather than depending on driver-specific leniency
// -- unlike uLightSpaceMatrices/uCascadeSplits above (ordinary mat4/float
// arrays, not opaque types, which GLSL 330 has always allowed dynamic
// indexing into).
uniform sampler2D uShadowMap0;
uniform sampler2D uShadowMap1;
uniform sampler2D uShadowMap2;
// All three cascades share one resolution (see application.cpp's
// kShadowMapWidth/Height), so one texel-size uniform serves all of them.
// Phase 7b bug-review fix: the shadow map's per-texel size in [0,1]
// shadow-space units (1.0 / resolution, uploaded from Application -- see
// ShadowMap's real width/height), used by shadowFactorForCascade()'s PCF
// kernel below to offset its neighboring samples by exactly one texel each
// step.
uniform vec2 uShadowMapTexelSize;

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
    // Points *from* the light *toward* the scene, same convention as
    // uLightDirection -- NOT from the fragment toward the light.
    vec3 direction;
    vec3 color;
    float constant;
    float linear;
    float quadratic;
    // cos(angle) of the inner/outer cone, precomputed on the CPU
    // (Application::render()) so this shader does one cheap dot-product
    // compare per fragment instead of an acos() per fragment.
    float innerCutoff;
    float outerCutoff;
};

// Only the first uNumPointLights/uNumSpotLights entries of each array are
// meaningful; Application uploads exactly that many and this shader never
// reads past them, so unused array slots' garbage/default uniform state is
// never touched.
uniform int uNumPointLights;
uniform PointLight uPointLights[MAX_POINT_LIGHTS];
uniform int uNumSpotLights;
uniform SpotLight uSpotLights[MAX_SPOT_LIGHTS];

// Standard inverse-square-ish distance falloff (the classic "point light
// range" formula from Ogre3D/LearnOpenGL, not physically exact inverse-
// square, but the well-known standard approximation real-time renderers
// use because it stays well-behaved -- non-infinite -- as distance
// approaches 0).
float attenuationFor(float constant, float linear, float quadratic, float distance) {
    return 1.0 / (constant + linear * distance + quadratic * distance * distance);
}

// Fetches cascade `index`'s own depth texture at `uv` -- an explicit branch
// rather than a dynamically-indexed sampler array, see uShadowMap0/1/2's own
// comment above for why.
float sampleCascadeDepth(int index, vec2 uv) {
    if (index == 0) {
        return texture(uShadowMap0, uv).r;
    } else if (index == 1) {
        return texture(uShadowMap1, uv).r;
    }
    return texture(uShadowMap2, uv).r;
}

// Directional-light shadow factor for ONE specific cascade: 0.0 = fully
// lit, 1.0 = fully shadowed. Computes fragPosLightSpace itself (as
// uLightSpaceMatrices[cascadeIndex] * vec4(vFragPos, 1.0)) rather than
// reading a vertex-precomputed varying -- see basic.vert's own comment on
// why CSM moved that computation here. Perspective-dividing by w (always 1
// for this phase's orthographic light projections, but doing it anyway
// keeps this correct even if a later phase swaps in a perspective light
// projection) and remapping [-1,1] NDC to [0,1] texture space gives the
// coordinates to look the fragment up in that cascade's shadow map at.
//
// Bias: slope-scaled (scaled by 1 - N.L, i.e. bigger for surfaces nearly
// edge-on to the light) rather than a single fixed constant, clamped to a
// small minimum -- a fixed bias big enough to fix grazing-angle acne would
// be needlessly large (and start visibly detaching shadows from their
// casters, "peter-panning") for surfaces facing the light head-on, where
// depth-map quantization error is much smaller.
//
// Phase 7b bug-review fix: PCF (percentage-closer filtering). A single
// shadow-map sample gives a binary 0/1 result per fragment, which is exactly
// what produces a hard, aliased/jagged shadow boundary -- two fragments a
// sub-pixel apart can land in different shadow-map texels and jump straight
// from fully lit to fully shadowed. Sampling a small 3x3 grid of texels
// around the fragment's own lookup position (offset by uShadowMapTexelSize
// per step) and averaging each one's binary in/out result instead produces
// intermediate values (e.g. 3/9, 6/9) exactly along that boundary, which
// reads as a soft, anti-aliased edge instead of a jagged one.
float shadowFactorForCascade(int cascadeIndex, vec3 normal, vec3 lightDir) {
    vec4 fragPosLightSpace = uLightSpaceMatrices[cascadeIndex] * vec4(vFragPos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    // Outside this cascade's own orthographic frustum (or beyond its far
    // plane) -- there is no shadow-map data for this fragment in this
    // cascade, so treat it as unshadowed rather than sampling/clamping into
    // a meaningless texel. In practice this shouldn't happen for the
    // cascade a fragment is actually assigned to (each cascade's box is fit
    // to fully contain its own depth slice), but stays as a defensive guard
    // -- e.g. against the small padding margins (see
    // application.cpp's kCascadeXYPadding/kCascadeZPadding) not being
    // generous enough for some corner case.
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

// Phase 13c: picks which cascade vViewSpaceDepth falls into, then blends
// smoothly into the next cascade over a small transition band near each
// split. A hard cutoff between cascades is visible as a seam: a fragment a
// sub-pixel away across the split boundary can jump from one cascade's own
// PCF kernel/bias/effective resolution to a completely different one,
// which reads as a visible line where shadow softness/aliasing changes
// abruptly. kCascadeBlendBand (view-space world units) is fixed rather than
// derived from uCascadeSplits[] -- generous enough relative to this scene's
// own smallest cascade gap (see application.cpp's computeCascades(), the
// cascade 0 -> 1 gap is the tightest at this phase's chosen split scheme)
// to visibly smooth the seam without blending across so wide a band that
// two cascades' differently-sized effective texel footprints average into
// a visibly softer ring around each split.
const float kCascadeBlendBand = 0.75;

float shadowFactor(vec3 normal, vec3 lightDir) {
    int cascadeIndex = 0;
    for (int i = 0; i < NUM_CASCADES - 1; ++i) {
        if (vViewSpaceDepth > uCascadeSplits[i]) {
            cascadeIndex = i + 1;
        }
    }

    float shadow = shadowFactorForCascade(cascadeIndex, normal, lightDir);

    // Blend towards the next cascade only near this cascade's own far
    // split, and never past the last cascade (nothing to blend into).
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

void main() {
    vec3 normal = normalize(vNormal);

    // Tangent-space normal mapping: build a per-fragment TBN (tangent,
    // bitangent, normal) basis and use it to rotate the normal map's
    // tangent-space sample into world space -- the same space
    // uLightDirection/uPointLights[i].position/uViewPos are already
    // expressed in, so the lighting math below doesn't need to know or
    // care whether a normal map was involved.
    if (uUseNormalMap != 0) {
        // Re-orthogonalize the (already roughly-orthogonal, interpolated)
        // tangent against the normal via Gram-Schmidt before using it --
        // interpolating vTangent/vNormal independently across a triangle
        // can leave them not quite perpendicular, and a non-orthogonal TBN
        // matrix would skew the transformed normal.
        vec3 T = normalize(vTangent - normal * dot(normal, vTangent));
        // Bitangent derived as cross(normal, tangent) rather than carried
        // as its own vertex attribute -- see mesh.hpp's comment on this
        // tradeoff (no separate handedness tracking).
        vec3 B = cross(normal, T);
        mat3 TBN = mat3(T, B, normal);

        vec3 sampledNormal = texture(uNormalMap, vTexCoord).rgb * 2.0 - 1.0;
        normal = normalize(TBN * sampledNormal);
    }

    vec3 viewDir = normalize(uViewPos - vFragPos);

    vec4 texColor = texture(uDiffuseTexture, vTexCoord);
    vec3 baseColor = texColor.rgb * uTint;

    // Diffuse/specular accumulate across every light; ambient is scene-wide
    // and added once at the end (not once per light) -- same structure
    // Phase 4's single-light version used, just generalized to a sum.
    vec3 diffuseSum = vec3(0.0);
    vec3 specularSum = vec3(0.0);

    // --- Directional light (shadowed) ---
    {
        vec3 lightDir = normalize(-uLightDirection);
        float diff = max(dot(normal, lightDir), 0.0);
        vec3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfwayDir), 0.0), uShininess);

        float shadow = shadowFactor(normal, lightDir);
        float lit = 1.0 - shadow;

        diffuseSum += lit * diff * uLightColor;
        specularSum += lit * spec * uLightColor;
    }

    // --- Point lights (unshadowed) ---
    for (int i = 0; i < uNumPointLights; ++i) {
        vec3 toLight = uPointLights[i].position - vFragPos;
        float distance = length(toLight);
        vec3 lightDir = normalize(toLight);

        float diff = max(dot(normal, lightDir), 0.0);
        vec3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfwayDir), 0.0), uShininess);

        float atten =
            attenuationFor(uPointLights[i].constant, uPointLights[i].linear, uPointLights[i].quadratic, distance);

        diffuseSum += atten * diff * uPointLights[i].color;
        specularSum += atten * spec * uPointLights[i].color;
    }

    // --- Spot lights (unshadowed) ---
    for (int i = 0; i < uNumSpotLights; ++i) {
        vec3 toLight = uSpotLights[i].position - vFragPos;
        float distance = length(toLight);
        vec3 lightDir = normalize(toLight);

        float diff = max(dot(normal, lightDir), 0.0);
        vec3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfwayDir), 0.0), uShininess);

        float atten =
            attenuationFor(uSpotLights[i].constant, uSpotLights[i].linear, uSpotLights[i].quadratic, distance);

        // theta is cos(angle between the fragment and the spot's own aim
        // direction); bigger theta = closer to dead-center of the cone.
        // Comparing against innerCutoff/outerCutoff (both cosines) means
        // "bigger than" is the *inside* test, matching how cos() decreases
        // as the angle grows.
        float theta = dot(lightDir, normalize(-uSpotLights[i].direction));
        float epsilon = max(uSpotLights[i].innerCutoff - uSpotLights[i].outerCutoff, 0.0001);
        // smoothstep gives a soft-edged cone (a gradual fade from the inner
        // to the outer cutoff angle) instead of a hard binary in/out cutoff.
        float spotIntensity = smoothstep(0.0, 1.0, (theta - uSpotLights[i].outerCutoff) / epsilon);

        diffuseSum += atten * spotIntensity * diff * uSpotLights[i].color;
        specularSum += atten * spotIntensity * spec * uSpotLights[i].color;
    }

    // Ambient + accumulated diffuse are modulated by the surface's own
    // texture color and tint (they represent light reflecting off the
    // surface's actual material color); accumulated specular represents
    // direct highlight reflectance and is deliberately left un-modulated by
    // the texture, the usual approximation for a non-metallic/dielectric
    // surface -- unchanged from Phase 4, just summed over more lights now.
    vec3 litColor = (uAmbientColor + diffuseSum) * baseColor + specularSum;
    FragColor = vec4(litColor, texColor.a);
}
