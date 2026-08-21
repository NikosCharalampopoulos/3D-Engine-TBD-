#version 330 core

// Phase 10: convolves uEnvironmentMap (the existing skybox cubemap, see
// engine::Skybox::textureId()) into a small diffuse irradiance cubemap --
// one of the split-sum approximation's two precomputed environment terms
// (Karis, "Real Shading in Unreal Engine 4"; this specific convolution is the
// same one LearnOpenGL's IBL-diffuse-irradiance article documents). Rendered
// once per cubemap face at startup (see engine::IBLProbe), never per-frame.
//
// Each texel's own direction (vDirection, re-normalized as N below) is
// treated as a hemisphere's pole; incoming radiance is integrated over that
// whole hemisphere, discretized as a double loop over spherical coordinates
// around N's own local tangent basis (right/up built below), each sample
// weighted by cos(theta) (Lambert's law: a surface with normal N receives
// less irradiance from directions near its own horizon) times sin(theta)
// (the solid-angle element a fixed-size (dphi, dtheta) patch of the theta/phi
// parameterization actually covers -- that patch is physically smaller near
// the pole (theta near 0) and larger near the equator (theta near pi/2), so
// without this factor the discretization would over-weight the pole). The
// whole sum is normalized by the total sample count actually accumulated
// (not a closed-form constant tied to one specific step size), so
// kPhiStep/kThetaStep can be retuned later without also having to re-derive a
// matching normalization constant by hand.
in vec3 vDirection;

out vec4 FragColor;

uniform samplerCube uEnvironmentMap;

const float PI = 3.14159265359;

void main() {
    vec3 N = normalize(vDirection);

    // An arbitrary "up" reference to build a tangent basis around N -- must
    // not be parallel to N, or cross(up, N) below degenerates to a zero
    // vector. N can point anywhere on the sphere (it's a cubemap texel
    // direction), including straight up/down, where the usual (0,1,0)
    // world-up reference would itself be parallel to N -- so this falls back
    // to (1,0,0) whenever N is too close to the Y axis.
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    vec3 irradiance = vec3(0.0);
    float sampleCount = 0.0;

    const float kPhiStep = 0.05;
    const float kThetaStep = 0.05;

    for (float phi = 0.0; phi < 2.0 * PI; phi += kPhiStep) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += kThetaStep) {
            // Spherical -> tangent-space Cartesian (Z is "up", i.e. along N),
            // then rotated into world space via the (right, up, N) basis
            // built above.
            vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 sampleDir = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

            irradiance += texture(uEnvironmentMap, sampleDir).rgb * cos(theta) * sin(theta);
            sampleCount += 1.0;
        }
    }

    // The standard closed-form result folds a factor of PI into the average
    // (see LearnOpenGL/Karis' reference derivation): the cos(theta)*sin(theta)
    // weighting above already accounts for the integrand's own solid-angle
    // and cosine terms, and dividing by sampleCount turns the discrete sum
    // into an average over the hemisphere's solid angle (2*PI steradians),
    // which combined with the Lambertian BRDF's own 1/PI leaves exactly one
    // PI factor remaining here.
    irradiance = PI * irradiance / sampleCount;

    FragColor = vec4(irradiance, 1.0);
}
