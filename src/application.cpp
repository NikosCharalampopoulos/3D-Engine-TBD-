#include "engine/application.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>

#include "engine/gl_debug.hpp"
#include "engine/log.hpp"
#include "engine/model.hpp"
#include "engine/paths.hpp"

namespace engine {

namespace {

// Cornflower blue, carried over from Phase 0 as the "yes, the context and
// loop actually work" clear color: a screenshot averaging to this (rather
// than black) proves clear+swap ran, and running it every frame (rather
// than once) proves the loop is actually looping. It also needs to stay
// visually distinct from the scene's own colors (see scene.mtl) so a
// screenshot can tell background from geometry at a glance.
constexpr float kClearR = 0.3921f;
constexpr float kClearG = 0.5843f;
constexpr float kClearB = 0.9294f;
constexpr float kClearA = 1.0f;

// There's no real display refresh to synchronize with under Xvfb/llvmpipe,
// so cap the frame rate by hand instead of busy-spinning the CPU as fast as
// the software rasterizer allows.
constexpr auto kFrameThrottle = std::chrono::milliseconds(16);

// Shader/model asset paths. Resolved via resolveAssetPath() against the
// running executable's own directory (see paths.hpp) rather than used as
// bare relative paths against the process's current working directory --
// CMake copies assets/ next to the built binary specifically so this
// resolves correctly regardless of the directory the exe is launched from.
const std::string kVertexShaderPath = resolveAssetPath("assets/shaders/basic.vert");
const std::string kFragmentShaderPath = resolveAssetPath("assets/shaders/basic.frag");
// Phase 7a: the shadow map's own depth-only program (see
// renderShadowPass()/shadow_map.hpp).
const std::string kShadowVertexShaderPath = resolveAssetPath("assets/shaders/shadow.vert");
const std::string kShadowFragmentShaderPath = resolveAssetPath("assets/shaders/shadow.frag");
// Phase 7b: the skybox's own program and the HDR-resolve fullscreen pass's
// program (see skybox.hpp/framebuffer.hpp and Application::render()).
const std::string kSkyboxVertexShaderPath = resolveAssetPath("assets/shaders/skybox.vert");
const std::string kSkyboxFragmentShaderPath = resolveAssetPath("assets/shaders/skybox.frag");
const std::string kPostProcessVertexShaderPath = resolveAssetPath("assets/shaders/postprocess.vert");
const std::string kPostProcessFragmentShaderPath = resolveAssetPath("assets/shaders/postprocess.frag");
// Phase 5's hand-authored test scene: three separate objects (a pyramid, a
// table, and a small box sitting on top of the table) at different
// positions, proving Model's node hierarchy + transform composition places
// more than one mesh correctly -- see assets/models/scene.obj and
// model.cpp.
const std::string kScenePath = resolveAssetPath("assets/models/scene.obj");

// Phase 7a: the ground plane's own textures (see mesh.hpp's
// makeGroundPlane() and this file's groundMesh_/groundMaterial_). The
// diffuse path intentionally reuses Model's own checker-texture fallback
// path (see model.cpp's kFallbackTexturePath) -- ResourceManager caches by
// path, so this is the same already-loaded Texture, not a second upload of
// the same PNG.
const std::string kGroundDiffuseTexturePath = resolveAssetPath("assets/textures/checker.png");
const std::string kGroundNormalMapPath = resolveAssetPath("assets/textures/normal_bump.png");

// Phase 7b: the skybox's 6 procedurally-generated face images (see
// README.md's Phase 7b notes for how they were made and why their edges
// line up seam-free) -- order must match skybox.hpp's documented
// right/left/top/bottom/front/back convention exactly.
const std::array<std::string, 6> kSkyboxFacePaths = {
    resolveAssetPath("assets/textures/skybox/right.png"),
    resolveAssetPath("assets/textures/skybox/left.png"),
    resolveAssetPath("assets/textures/skybox/top.png"),
    resolveAssetPath("assets/textures/skybox/bottom.png"),
    resolveAssetPath("assets/textures/skybox/front.png"),
    resolveAssetPath("assets/textures/skybox/back.png"),
};

// Phase 7a: fixed resolution for the directional light's shadow map (see
// shadow_map.hpp) -- independent of the window's own framebuffer size.
// 1024x1024 is generous for this engine's small hand-authored test scene; a
// larger/more detailed scene would want a bigger map (or cascaded shadow
// maps, out of scope here).
constexpr int kShadowMapWidth = 1024;
constexpr int kShadowMapHeight = 1024;

// Phase 7b bug-review fix: postprocess.frag's brightness knob, applied
// before the Reinhard tonemap curve (see that shader's header comment for
// why the gamma-encode step it used to also apply was removed rather than
// kept). Reinhard's own compression still pulls every value down somewhat
// (e.g. reinhard(0.8) = 0.44), so a modest >1.0 exposure restores the
// overall brightness/punch Phase 7a's uncorrected direct-to-framebuffer
// render had, without reintroducing the gamma-curve's channel-ratio
// (saturation) collapse. 1.4 was chosen by comparing sampled pixel values
// against Phase 7a's screenshot until overall brightness matched closely;
// exposed as one named constant (not a magic literal at the call site) so
// it's easy to re-tune if art direction changes later.
constexpr float kPostProcessExposure = 1.4f;
// The shadow map's depth texture is sampled on this fixed texture unit
// every frame (see render()) -- unit 0 is always the current Material's
// diffuse texture and unit 1 its optional normal map (see
// material.hpp/Material::bind()), so 2 is free and stays bound across every
// per-mesh Material::bind() call in the same frame (those never touch unit
// 2).
constexpr unsigned int kShadowMapTextureUnit = 2;

// Phase 4's directional light: a fixed "sun" direction/color, not yet
// animated or configurable -- proving the Phong math works is this phase's
// goal, not building a full light-management system. uLightDirection points
// *from* the light *toward* the scene (see basic.frag), coming down and
// across so every visible cube face gets a different N.L term instead of
// one face being lit edge-on.
//
// Phase 7a note on the horizontal (x, z) sign: this light's shadows fall on
// the side of each object the light continues past -- i.e. towards +x, +z
// here, the same side kDefaultCameraPosition (below) views the scene from.
// Phase 4-6 used (-0.5, -1.0, -0.3) (shadows falling towards -x, -z, away
// from the camera and mostly hidden behind their own casters from this
// camera's viewpoint) -- fine when nothing sampled the shadow, but Phase
// 7a's whole point is a shadow the default camera can actually see, so the
// horizontal components are mirrored here (same steep, mostly-downward
// character/magnitude, still a plausible "sun" angle) rather than moving
// the long-established default camera position instead.
constexpr glm::vec3 kLightDirection{0.6f, -0.7f, 0.35f};
constexpr glm::vec3 kLightColor{1.0f, 0.95f, 0.85f};
constexpr glm::vec3 kAmbientColor{0.15f, 0.15f, 0.18f};

// Phase 7a: point lights + a spot light, joining the Phase 4 directional
// light above (which stays the one shadow-casting light -- see
// renderShadowPass()/basic.frag). Mirrors basic.frag's PointLight/SpotLight
// uniform structs field-for-field so uploadPointLight()/uploadSpotLight()
// below can copy each field straight across.
//
// Positions are chosen from scene.obj's own documented object extents (see
// that file's header comments): one warm point light sits just above
// BoxOnTable (center (0.5, 0.38, 0.1), top at y = 0.56) so it visibly tints
// the box/table area away from the neutral-white directional light; one
// cool point light sits just above the pyramid's apex ((-1.35, 0.7, -0.3))
// for the same reason on the opposite side of the scene. The spot light
// sits above and slightly behind the table, aimed down and slightly
// forward, so its soft-edged cone visibly falls across the table/ground
// area a camera looking at the scene from Application's default position
// can actually see.
struct PointLightData {
    glm::vec3 position;
    glm::vec3 color;
    float constant;
    float linear;
    float quadratic;
};

struct SpotLightData {
    glm::vec3 position;
    // Points *from* the light *toward* the scene, same "sun ray direction"
    // convention as kLightDirection -- need not be pre-normalized, since
    // basic.frag normalizes it itself before use.
    glm::vec3 direction;
    glm::vec3 color;
    float constant;
    float linear;
    float quadratic;
    // cos(inner/outer cone half-angle), precomputed here as literals rather
    // than a glm::cos(glm::radians(...)) call so this whole table can stay
    // a compile-time constant like kPointLights below. 12.5/20 degrees are
    // the classic LearnOpenGL "soft spotlight" reference values:
    // cos(12.5 deg) = 0.9762960, cos(20 deg) = 0.9396926.
    float innerCutoffCos;
    float outerCutoffCos;
};

// Attenuation constants (constant, linear, quadratic) follow the standard
// "point light range" reference table (Ogre3D/LearnOpenGL): (1.0, 0.7, 1.8)
// is the ~7-unit-range profile, chosen because these lights sit well under
// one world unit from the surfaces they're meant to visibly tint -- a
// longer-range (smaller linear/quadratic) profile would barely attenuate at
// all across this engine's small test scene and wash out the "distinct
// tint near the light" effect this phase's screenshot needs to show.
// Phase 7b: kPointLights[0]'s color is deliberately well above the old
// [0,1] range (6.0 in its red channel) now that render() renders into a
// floating-point HDR buffer (see hdrFramebuffer_/framebuffer.hpp) instead
// of straight to the default framebuffer -- this is HDR's whole point:
// letting a light's real intensity exceed 1.0 and be tonemapped back down
// smoothly (see assets/shaders/postprocess.frag's Reinhard step) instead of
// being clamped. Writing a color this bright straight to an 8-bit
// framebuffer (as Phase 7a's pipeline did) would hard-clip every channel
// above 1.0 to a flat white with a hard-edged boundary the instant it
// crosses 1.0; the tonemapped result instead rolls off gradually as
// distance from the light increases. kPointLights[1] is left at its
// original Phase 7a intensity for comparison.
constexpr std::array<PointLightData, 2> kPointLights = {{
    {{0.5f, 0.95f, 0.1f}, {6.0f, 2.1f, 0.9f}, 1.0f, 0.7f, 1.8f},
    {{-1.35f, 1.15f, -0.3f}, {0.15f, 0.55f, 1.0f}, 1.0f, 0.7f, 1.8f},
}};

// Slightly longer effective range (0.35, 0.44 -- the ~13-unit-range profile)
// than the point lights above since this spot light sits farther from the
// table it's aimed at.
constexpr std::array<SpotLightData, 1> kSpotLights = {{
    {{0.2f, 1.7f, 0.5f}, {-0.05f, -1.0f, -0.2f}, {0.3f, 1.0f, 0.35f}, 1.0f, 0.35f, 0.44f, 0.9762960f, 0.9396926f},
}};

// Kept in sync by hand with MAX_POINT_LIGHTS/MAX_SPOT_LIGHTS in basic.frag;
// these static_asserts at least catch this file's own table growing past
// what that shader's fixed-size arrays can hold, without needing a shared
// constant across a GLSL/C++ boundary.
static_assert(kPointLights.size() <= 8, "kPointLights exceeds MAX_POINT_LIGHTS in basic.frag");
static_assert(kSpotLights.size() <= 4, "kSpotLights exceeds MAX_SPOT_LIGHTS in basic.frag");

// Phase 2's fixed eye position, kept here only as a comment for context: it
// was glm::vec3(0, 0, 3) looking at the origin with up (0, 1, 0). Phase 3's
// default camera (see the Application constructor below) is deliberately
// placed somewhere else entirely -- off to the side, higher up, and farther
// back -- so a headless screenshot visibly proves the view now comes from a
// live Camera rather than that old hardcoded matrix.
constexpr glm::vec3 kDefaultCameraPosition{2.6f, 1.9f, 3.4f};
// Phase 5's scene.obj lays its three objects out (deliberately) so their
// combined bounding box is still roughly centered near the origin -- see
// assets/models/scene.obj -- so this unchanged Phase 3/4 camera target
// still frames the whole scene, not just one object.
constexpr glm::vec3 kSceneCenter{0.0f, 0.0f, 0.0f};

// Phase 7a: the hand-built ground plane's own placement (see mesh.hpp's
// makeGroundPlane()). Sized/centered to sit under the whole scene (which
// spans roughly x in [-1.7, 1.0], z in [-0.65, 0.5] before sceneTransform_'s
// rotation -- see scene.obj) with room to spare on every side so a shadow
// cast by any of the three objects lands on the plane rather than running
// off its edge, and set just below y = 0 (every scene.obj object's own
// lowest vertex) so it renders behind them rather than z-fighting with the
// table/pyramid's own bottom faces.
constexpr float kGroundHalfExtent = 2.6f;
constexpr float kGroundY = -0.01f;
constexpr float kGroundUvTiling = 6.0f;

// Directional light's shadow-map projection: a directional light has no
// real position (it's meant to be infinitely far away), so this picks a
// fixed, reasonable light-space "eye" -- a point kLightDistance back along
// -kLightDirection from the scene's center -- purely to build an
// orthographic view/projection pair from.
//
// kOrthoHalfExtent must cover the ground plane's *diagonal* footprint as
// seen from the light, not just kGroundHalfExtent itself: because the light
// looks along an oblique direction (kLightDirection has both a horizontal
// and vertical component, not a top-down one), the light-space "right" axis
// does not line up with the ground plane's own X or Z edges. A square of
// half-extent kGroundHalfExtent projected onto an axis running diagonally
// across it needs up to its full diagonal half-length
// (kGroundHalfExtent * sqrt(2) ~= 3.68 for kGroundHalfExtent = 2.6), not
// just kGroundHalfExtent itself, to stay fully inside [-kOrthoHalfExtent,
// kOrthoHalfExtent]. A previous value of 3.0 here was verified (by
// projecting the ground plane's corners into light space) to fall about 0.56
// units short of that, silently clipping the plane's far corners out of the
// shadow frustum -- harmless in this particular scene (nothing casts a
// shadow anywhere near those corners) but wrong on its own terms, and a
// latent bug for any future scene that moves a shadow-casting object out
// there. 4.0 comfortably covers the diagonal with margin to spare.
//
// kOrthoNear/kOrthoFar cover the distance from that eye point back through
// the scene and out the other side with margin.
constexpr float kLightDistance = 6.0f;
constexpr float kOrthoHalfExtent = 4.0f;
constexpr float kOrthoNear = 0.5f;
constexpr float kOrthoFar = 12.0f;

glm::mat4 computeLightSpaceMatrix() {
    const glm::vec3 lightDir = glm::normalize(kLightDirection);
    const glm::vec3 lightEye = kSceneCenter - lightDir * kLightDistance;
    const glm::mat4 lightView = glm::lookAt(lightEye, kSceneCenter, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 lightProjection = glm::ortho(-kOrthoHalfExtent, kOrthoHalfExtent, -kOrthoHalfExtent,
                                                   kOrthoHalfExtent, kOrthoNear, kOrthoFar);
    return lightProjection * lightView;
}

// Uploads one PointLightData/SpotLightData's fields to `uPointLights[index]`
// / `uSpotLights[index]` in basic.frag. Named-uniform lookups are built as
// plain strings each call (like every other Shader::set*() call site in
// this engine -- see shader.hpp's "no caching" note); a handful of lights
// times a few fields each per frame is not a cost worth optimizing away
// yet.
void uploadPointLight(Shader& shader, std::size_t index, const PointLightData& light) {
    const std::string prefix = "uPointLights[" + std::to_string(index) + "].";
    shader.setVec3(prefix + "position", light.position);
    shader.setVec3(prefix + "color", light.color);
    shader.setFloat(prefix + "constant", light.constant);
    shader.setFloat(prefix + "linear", light.linear);
    shader.setFloat(prefix + "quadratic", light.quadratic);
}

void uploadSpotLight(Shader& shader, std::size_t index, const SpotLightData& light) {
    const std::string prefix = "uSpotLights[" + std::to_string(index) + "].";
    shader.setVec3(prefix + "position", light.position);
    shader.setVec3(prefix + "direction", light.direction);
    shader.setVec3(prefix + "color", light.color);
    shader.setFloat(prefix + "constant", light.constant);
    shader.setFloat(prefix + "linear", light.linear);
    shader.setFloat(prefix + "quadratic", light.quadratic);
    shader.setFloat(prefix + "innerCutoff", light.innerCutoffCos);
    shader.setFloat(prefix + "outerCutoff", light.outerCutoffCos);
}

bool cameraDemoModeFromEnv() {
    const char* value = std::getenv("ENGINE_CAMERA_DEMO");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

}  // namespace

Application::Application(int width, int height, const std::string& title, std::uint64_t maxFrames)
    : window_(width, height, title),
      shader_(resources_.getShader(kVertexShaderPath, kFragmentShaderPath)),
      shadowShader_(resources_.getShader(kShadowVertexShaderPath, kShadowFragmentShaderPath)),
      skyboxShader_(resources_.getShader(kSkyboxVertexShaderPath, kSkyboxFragmentShaderPath)),
      postProcessShader_(resources_.getShader(kPostProcessVertexShaderPath, kPostProcessFragmentShaderPath)),
      shadowMap_(kShadowMapWidth, kShadowMapHeight),
      // Phase 7b: sized from window_'s own real framebuffer size (already
      // constructed at this point -- see this header's declaration-order
      // comment) rather than the constructor's width/height parameters
      // directly, so this stays correct even on a HiDPI display where the
      // framebuffer is larger than the window's requested size.
      hdrFramebuffer_(window_.getSize().first, window_.getSize().second),
      skybox_(kSkyboxFacePaths),
      // Phase 7a's demo normal-mapped surface -- see mesh.hpp's
      // makeGroundPlane() and this class's Phase 7a header comment. Shares
      // shader_ (the main lit program) with every entity's Model, unlike
      // shadowShader_ above (the depth-only program).
      groundMesh_(makeGroundPlane(kGroundHalfExtent, kGroundY, kGroundUvTiling)),
      groundMaterial_(*shader_, resources_.getTexture(kGroundDiffuseTexturePath), /*tint=*/glm::vec3(1.0f),
                      /*shininess=*/24.0f, resources_.getTexture(kGroundNormalMapPath)),
      postProcessQuad_(makeFullscreenQuad()),
      camera_(kDefaultCameraPosition),
      maxFrames_(maxFrames),
      cameraDemoMode_(cameraDemoModeFromEnv()) {
    // No depth buffer testing existed in Phase 1 (nothing but a flat clear
    // needed it); real 3D geometry does, so faces occlude each other
    // correctly instead of painting in draw-call order.
    GL_CHECK(glEnable(GL_DEPTH_TEST));

    camera_.setPositionLookingAt(kDefaultCameraPosition, kSceneCenter);

    // The scene is one Entity wrapping the same Phase 5 model
    // (assets/models/scene.obj), loaded through resources_ instead of
    // constructed directly. A small fixed rotation is applied to its
    // Transform (rather than identity), for the same reason Phase 2-4 fixed
    // cubeTransform_'s rotation: proving the composition (entity transform *
    // accumulated parent node transform * node's own local transform, see
    // Model::drawNode()) is actually being applied, not just compiling,
    // regardless of which frame a headless screenshot lands on. 12 degrees
    // is small enough that scene.obj's three objects (deliberately laid out
    // to fit within Phase 3/4's unchanged camera framing) stay comfortably
    // in frame after the rotation.
    Entity sceneEntity("scene", resources_.getModel(kScenePath, *shader_));
    sceneEntity.transform.setRotation(glm::angleAxis(glm::radians(12.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    entities_.push_back(std::move(sceneEntity));

    if (cameraDemoMode_) {
        LOG_INFO("ENGINE_CAMERA_DEMO set: driving the camera through a scripted orbit instead of live input");
    }
    LOG_INFO("Application initialized");
}

void Application::update(double deltaTime, const InputState& input) {
    totalTime_ += deltaTime;

    if (cameraDemoMode_) {
        // Headless-safe stand-in for real input: Xvfb has no real keyboard/
        // mouse, so there's nothing for processMovement()/processMouseInput()
        // to read under the verification harness. Instead, step through a
        // small fixed set of known camera positions (all looking at the
        // scene), advancing one step every kFramesPerStep frames. Keyed off
        // frameCount_ (an exact integer, incremented once per loop iteration)
        // rather than totalTime_, so which waypoint is showing at any given
        // frame is fully deterministic and doesn't depend on how long each
        // frame actually took to render on this machine.
        constexpr std::uint64_t kFramesPerStep = 20;
        constexpr std::array<glm::vec3, 4> kWaypoints = {{
            {3.2f, 0.6f, 0.0f},    // orbit: right side, low
            {0.05f, 3.2f, 0.05f},  // orbit: near-overhead (exercises the pitch clamp/near-vertical case)
            {-3.2f, 0.6f, 0.0f},   // orbit: left side
            {0.0f, 0.6f, -3.2f},   // orbit: behind the scene
        }};
        const std::size_t waypoint = (frameCount_ / kFramesPerStep) % kWaypoints.size();
        camera_.setPositionLookingAt(kWaypoints[waypoint], kSceneCenter);
    } else {
        // Real free-fly input: WASD + Space/Shift (or E/Q) move the camera,
        // scaled by deltaTime so speed is frame-rate independent; mouse-look
        // reads the absolute cursor position each frame and lets Camera
        // derive its own delta. `input` is the InputState run() already
        // polled from window_ once this frame (see input.hpp) -- Camera
        // itself no longer touches window_ directly. Under Xvfb there's no
        // real input device driving any of this -- every InputState flag is
        // false and the cursor position never changes -- so this simply
        // leaves the camera at its constructor-set default pose during
        // headless verification, which is expected and fine.
        camera_.processMovement(input, static_cast<float>(deltaTime));
        camera_.processMouseInput(input.cursorX, input.cursorY);
    }
}

void Application::renderShadowPass(const glm::mat4& lightSpaceMatrix) {
    // Points the viewport at shadowMap_'s own resolution and binds its FBO;
    // render() restores the window's real viewport (and default framebuffer
    // binding, done here) once this returns.
    shadowMap_.bindForWriting();
    // Only a depth buffer exists on this FBO (see ShadowMap -- no color
    // attachment at all), so only GL_DEPTH_BUFFER_BIT is meaningful to
    // clear here.
    GL_CHECK(glClear(GL_DEPTH_BUFFER_BIT));

    shadowShader_->use();
    shadowShader_->setMat4("uLightSpaceMatrix", lightSpaceMatrix);

    for (const Entity& entity : entities_) {
        if (entity.model()) {
            entity.model()->drawDepthOnly(*shadowShader_, entity.transform.getModelMatrix());
        }
    }

    // The ground plane too, for the same "depth pass renders everything the
    // main pass renders" reason -- its own geometry is already baked in
    // world space (see makeGroundPlane()), so its model matrix is identity.
    shadowShader_->setMat4("uModel", glm::mat4(1.0f));
    groundMesh_.bind();
    groundMesh_.draw();

    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

void Application::render() {
    const auto [fbWidth, fbHeight] = window_.getSize();
    const float aspect = fbHeight != 0 ? static_cast<float>(fbWidth) / static_cast<float>(fbHeight) : 1.0f;

    // The directional light's shadow map is rendered first, into its own
    // depth-only FBO/viewport; glViewport is restored to the window's real
    // framebuffer size immediately after, since renderShadowPass() leaves it
    // pointed at shadowMap_'s (generally different) resolution.
    const glm::mat4 lightSpaceMatrix = computeLightSpaceMatrix();
    renderShadowPass(lightSpaceMatrix);
    GL_CHECK(glViewport(0, 0, fbWidth, fbHeight));

    // Phase 7b: the whole lit scene (+ skybox) renders into hdrFramebuffer_
    // -- a floating-point off-screen target -- instead of straight to the
    // default framebuffer; see render()'s tail below for the fullscreen
    // tonemap/gamma pass that resolves it to the window afterward.
    hdrFramebuffer_.bindForWriting();

    // Cornflower blue is still cleared first as a cheap safety fallback
    // (same rationale as every earlier phase -- see kClearR/G/B's comment),
    // even though skybox_.draw() below is expected to fully overwrite every
    // background pixel every frame; if that draw were ever skipped/failed,
    // this is what a screenshot would show instead of undefined/garbage
    // color.
    GL_CHECK(glClearColor(kClearR, kClearG, kClearB, kClearA));
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    const glm::mat4 view = camera_.getViewMatrix();
    const glm::mat4 projection = camera_.getProjectionMatrix(aspect);

    // View/projection and lighting are scene-level state, constant across
    // every node/mesh Model::draw() below is about to issue -- set once per
    // frame on the (one, shared) shader program rather than re-set inside
    // the per-node/per-mesh loop. GL uniform values live on the program
    // object itself and aren't disturbed by the repeated glUseProgram calls
    // each Material::bind() makes as Model::draw() walks the scene, so this
    // is safe to set just once here.
    shader_->use();
    shader_->setMat4("uView", view);
    shader_->setMat4("uProjection", projection);
    shader_->setMat4("uLightSpaceMatrix", lightSpaceMatrix);
    shader_->setVec3("uLightDirection", kLightDirection);
    shader_->setVec3("uLightColor", kLightColor);
    shader_->setVec3("uAmbientColor", kAmbientColor);
    shader_->setVec3("uViewPos", camera_.position());

    // Phase 7a: point/spot lights, uploaded as a live count + a fixed-size
    // array each frame (see basic.frag's uNumPointLights/uPointLights and
    // uNumSpotLights/uSpotLights) -- the standard forward-rendering
    // "fixed-size uniform array + count" pattern, so the shader only loops
    // over lights that actually exist rather than every array slot.
    shader_->setInt("uNumPointLights", static_cast<int>(kPointLights.size()));
    for (std::size_t i = 0; i < kPointLights.size(); ++i) {
        uploadPointLight(*shader_, i, kPointLights[i]);
    }
    shader_->setInt("uNumSpotLights", static_cast<int>(kSpotLights.size()));
    for (std::size_t i = 0; i < kSpotLights.size(); ++i) {
        uploadSpotLight(*shader_, i, kSpotLights[i]);
    }

    // Shadow map bound once here (not per-material) at a fixed texture unit
    // that no Material::bind() call below ever touches (see
    // kShadowMapTextureUnit's comment) -- it stays bound across every
    // subsequent draw call this frame.
    shadowMap_.bindForReading(kShadowMapTextureUnit);
    shader_->setInt("uShadowMap", static_cast<int>(kShadowMapTextureUnit));
    // Phase 7b bug-review fix: PCF (percentage-closer filtering) in
    // basic.frag's shadowFactor() samples a small 3x3 grid of texels around
    // each fragment's shadow-map lookup rather than just the one nearest
    // texel, so shadow edges soften into a gradient of partially-shadowed
    // values instead of a single hard-aliased 0/1 step. It needs the shadow
    // map's own per-texel size (in [0,1] shadow-space units) to offset those
    // samples by the right amount -- derived here from shadowMap_'s real
    // resolution rather than hardcoded in the shader, so it stays correct
    // if kShadowMapWidth/Height above ever changes.
    shader_->setVec2("uShadowMapTexelSize",
                       glm::vec2(1.0f / static_cast<float>(shadowMap_.width()),
                                 1.0f / static_cast<float>(shadowMap_.height())));

    // Each entity's transform matrix is the "rootTransform" Model::draw()
    // composes above the file's own node hierarchy: draw() recurses through
    // the model's node tree, uploading uModel/uNormalMatrix per node as
    // entity.transform * (accumulated parent node transform) * (node's own
    // local transform), and binding + drawing each node's mesh(es) with
    // their own Material. This phase's scene is exactly one Entity, but
    // iterating entities_ (rather than drawing one hardcoded model_)
    // establishes the pattern for however many later phases add.
    for (const Entity& entity : entities_) {
        if (entity.model()) {
            entity.model()->draw(*shader_, entity.transform.getModelMatrix());
        }
    }

    // Phase 7a's ground plane: drawn directly (not through Entity/Model,
    // see this class's header comment) with an identity model matrix, since
    // makeGroundPlane() already bakes its position into world-space vertex
    // data.
    {
        const glm::mat4 groundModel(1.0f);
        const glm::mat3 groundNormalMatrix = glm::inverseTranspose(glm::mat3(groundModel));
        shader_->setMat4("uModel", groundModel);
        shader_->setMat3("uNormalMatrix", groundNormalMatrix);
        groundMaterial_.bind();
        groundMesh_.bind();
        groundMesh_.draw();
    }

    // Phase 7b: the skybox is drawn LAST, still into hdrFramebuffer_ -- see
    // skybox.hpp's Skybox::draw() for the GL_LEQUAL depth trick that makes
    // it only paint over pixels nothing above just drew, i.e. the actual
    // background. Drawing it after (rather than before) every opaque
    // entity/the ground plane, instead of disabling depth testing/writes,
    // means the depth test itself does the "don't overwrite real geometry"
    // work for free -- no extra bookkeeping needed to keep the sky from
    // painting over the table/box/pyramid/ground.
    skybox_.draw(*skyboxShader_, view, projection);

    // Phase 7b: resolve hdrFramebuffer_'s HDR color buffer to the window's
    // real (default) framebuffer via one fullscreen tonemap + gamma-correct
    // pass -- see assets/shaders/postprocess.vert/.frag. Both buffers are
    // cleared first (color: nothing else draws here so any prior frame's
    // leftover pixels must go; depth: this pass's own fullscreen quad is
    // depth-tested against whatever the default framebuffer's depth buffer
    // last held, which is otherwise stale/unrelated to this frame) so this
    // draw can't be silently rejected by a leftover depth value from a
    // previous frame.
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    GL_CHECK(glViewport(0, 0, fbWidth, fbHeight));
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    postProcessShader_->use();
    hdrFramebuffer_.bindColorTexture(0);
    postProcessShader_->setInt("uHdrBuffer", 0);
    postProcessShader_->setFloat("uExposure", kPostProcessExposure);
    postProcessQuad_.bind();
    postProcessQuad_.draw();
}

void Application::run() {
    std::string startMsg = "Entering main loop";
    if (maxFrames_ != 0) {
        startMsg += " (capped at " + std::to_string(maxFrames_) + " frame(s), headless mode)";
    }
    LOG_INFO(startMsg);

    double lastTime = glfwGetTime();

    while (!window_.shouldClose()) {
        if (maxFrames_ != 0 && frameCount_ >= maxFrames_) {
            LOG_INFO("Reached max frame count, exiting main loop");
            break;
        }

        window_.pollEvents();

        const double currentTime = glfwGetTime();
        const double deltaTime = currentTime - lastTime;
        lastTime = currentTime;

        // Polled once per frame, right after pollEvents() (same timing
        // real keyboard/mouse reads always had) and threaded down through
        // update() to whatever needs it (currently just Camera) -- see
        // input.hpp. ESC is read from this same snapshot (input.escapePressed)
        // rather than Application calling window_.isKeyPressed() directly, so
        // Application -- like Camera since Phase 6 -- never reaches into
        // Window/GLFW key constants itself; InputState is the one place that
        // does.
        const InputState input = pollInputState(window_);
        if (input.escapePressed) {
            LOG_INFO("ESC pressed, exiting main loop");
            break;
        }

        update(deltaTime, input);
        render();
        window_.swapBuffers();

        ++frameCount_;
        std::this_thread::sleep_for(kFrameThrottle);
    }

    LOG_INFO("Exited main loop after " + std::to_string(frameCount_) + " frame(s), " +
              std::to_string(totalTime_) + "s total");
}

}  // namespace engine
