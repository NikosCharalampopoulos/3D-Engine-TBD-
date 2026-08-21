#version 330 core

// Phase 4: textured Blinn-Phong lighting, replacing Phase 2-3's flat
// per-draw-call uColor. One directional light (a distant light with a
// fixed direction and no falloff, e.g. "the sun") is the minimum bar for
// this phase; a point light with distance attenuation is a plausible
// future addition but isn't required here.
in vec3 vNormal;
in vec3 vFragPos;
in vec2 vTexCoord;

out vec4 FragColor;

uniform sampler2D uDiffuseTexture;
uniform vec3 uTint;
uniform float uShininess;

// Directional light: uLightDirection points *from* the light *toward* the
// scene (the usual "sun ray direction" convention), so surfaces are lit
// along -uLightDirection.
uniform vec3 uLightDirection;
uniform vec3 uLightColor;
uniform vec3 uAmbientColor;
uniform vec3 uViewPos;

void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(-uLightDirection);
    vec3 viewDir = normalize(uViewPos - vFragPos);

    // Diffuse: standard N.L, clamped so a back-facing surface (light
    // behind it) contributes zero rather than negative light.
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor;

    // Specular via the Blinn-Phong halfway vector (normalize(lightDir +
    // viewDir)) rather than the classic reflect()-based Phong term. This is
    // a deliberate choice, not an oversight: Blinn-Phong is numerically
    // more robust (reflect() can produce a reflection vector that needs a
    // much higher exponent to look equivalent, and grazing angles are
    // better behaved with the halfway vector) and is the convention most
    // modern real-time renderers use when they say "Phong lighting".
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), uShininess);
    vec3 specular = spec * uLightColor;

    vec4 texColor = texture(uDiffuseTexture, vTexCoord);

    // Ambient + diffuse are modulated by the surface's own texture color
    // and tint (they represent light reflecting off the surface's actual
    // material color); specular represents direct highlight reflectance off
    // the surface and is deliberately left un-modulated by the texture, the
    // usual approximation for a non-metallic/dielectric surface.
    vec3 litColor = (uAmbientColor + diffuse) * texColor.rgb * uTint + specular;
    FragColor = vec4(litColor, texColor.a);
}
