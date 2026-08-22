#include "engine/application.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <string>
#include <thread>
#include <utility>

#include "engine/frustum.hpp"
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
// Phase 11: bloom's two extra passes -- both pair with
// kPostProcessVertexShaderPath (an ordinary fullscreen quad), like
// kBrdfLutFragmentShaderPath below already does for Phase 10's BRDF LUT.
const std::string kBloomExtractFragmentShaderPath = resolveAssetPath("assets/shaders/bloom_extract.frag");
const std::string kBlurFragmentShaderPath = resolveAssetPath("assets/shaders/blur.frag");
// Phase 9: the PBR pass's own program (see application.hpp's Phase 9 note
// and assets/shaders/pbr.vert/pbr.frag).
const std::string kPBRVertexShaderPath = resolveAssetPath("assets/shaders/pbr.vert");
const std::string kPBRFragmentShaderPath = resolveAssetPath("assets/shaders/pbr.frag");
// Phase 10: the three one-time IBL precompute programs (see
// ibl_probe.hpp/ibl_probe.cpp). The irradiance/prefilter passes share
// cubemap_capture.vert (a fixed-view-per-face cube render, no depth trick --
// see that file's own header comment); the BRDF LUT pass reuses
// postprocess.vert (an ordinary fullscreen quad with no model/view/
// projection at all) paired with its own fragment shader.
const std::string kCubemapCaptureVertexShaderPath = resolveAssetPath("assets/shaders/cubemap_capture.vert");
const std::string kIrradianceFragmentShaderPath =
    resolveAssetPath("assets/shaders/irradiance_convolution.frag");
const std::string kPrefilterFragmentShaderPath = resolveAssetPath("assets/shaders/prefilter.frag");
const std::string kBrdfLutFragmentShaderPath = resolveAssetPath("assets/shaders/brdf_lut.frag");
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

// Phase 11: two procedural textured-PBR-material asset sets (albedo + a
// packed ORM map each -- see pbr_material.hpp's Phase 11 comment), replacing
// two of the sphere-grid's flat-albedo-only instances (see this file's
// sphere-instance construction below) so the grid shows real surface detail,
// not just flat colors. Generated with ImageMagick (organic blob/noise masks
// blended between two flat colors -- the same "procedural, not
// sourced/painted" approach checker.png/normal_bump.png/the skybox faces
// used), not sourced/painted by hand.
const std::string kRustedMetalAlbedoPath = resolveAssetPath("assets/textures/rusted_metal_albedo.png");
const std::string kRustedMetalORMPath = resolveAssetPath("assets/textures/rusted_metal_orm.png");
const std::string kScuffedPlasticAlbedoPath = resolveAssetPath("assets/textures/scuffed_plastic_albedo.png");
const std::string kScuffedPlasticORMPath = resolveAssetPath("assets/textures/scuffed_plastic_orm.png");

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
// 1024x1024 is generous for this engine's small hand-authored test scene.
//
// Phase 13c: every cascade uses this same resolution (rather than, say, a
// bigger map for the near cascade and smaller ones further out) -- CSM's
// whole resolution win here comes from each cascade's own tightly-fitted
// (much smaller than the old whole-scene map's) world-space footprint, not
// from spending more texels on any one cascade; keeping every cascade's
// resolution identical also lets every cascade share one
// uShadowMapTexelSize uniform upload (see render()) instead of needing a
// per-cascade one.
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

// Phase 11: bloom tuning constants (see Application::render()'s bloom
// section and this class's Phase 11 header comment).
//
// kBloomThreshold: bloom_extract.frag only keeps pixels whose luminance
// exceeds this. 1.0 (the value below which Reinhard tonemapping --
// color / (color + 1) -- is still close to a 1:1 mapping, so pixels above
// it are exactly the ones the curve is compressing hardest) looks like the
// natural cutoff on paper, but turned out too low in practice: this scene's
// point lights are intense enough (kPointLights[0]'s {6.0, 2.1, 0.9},
// kPointLights[2]'s {5.5, 5.2, 4.7}) that ordinary *diffuse* checker-floor
// texels near them -- not just tight specular highlights -- already exceed
// a raw HDR luminance of 1.0, so a 1.0 threshold's "bright" mask included a
// sizable patch of ordinary floor around the sphere grid, not just
// highlight dots; blurred and added back, that read as the floor being
// washed out/de-saturated over a wide area rather than a soft glow localized
// to each highlight (confirmed by diffing a bloom-off render against this
// one: at 1.0 the floor between spheres brightened by ~30/255 with zero
// bloom-strength contribution expected there; at 2.0 that same patch is
// pixel-identical to the bloom-off render). 2.0 keeps the mask to what this
// scene's tightly-concentrated specular highlights alone reach, which is
// what should visibly glow.
constexpr float kBloomThreshold = 2.0f;
// kBloomDownsampleFactor: brightFramebuffer_/pingpongFramebuffer0_/1_ are
// all sized at the window's real framebuffer resolution divided by this --
// bloom is a soft, low-frequency glow, so blurring at a lower resolution
// costs proportionally less per-pixel work with no visible loss of quality
// (the opposite would be true for anything with sharp detail, e.g. the main
// scene render itself, which stays full-resolution).
constexpr int kBloomDownsampleFactor = 2;
// kBloomBlurPasses: total ping-pong blur.frag draws (alternating horizontal/
// vertical -- see render()), not blur *pairs*. 6 -- 3 full horizontal+
// vertical pairs -- is within this phase brief's own suggested "4-6 passes,
// typical and sufficient" range: enough for a visibly soft glow without
// spreading it so wide it reads as a global haze rather than a localized
// highlight around each genuinely bright pixel. Must be even (an odd count
// would end on a lopsided horizontal-only-blurred result) -- see the
// static_assert below.
constexpr int kBloomBlurPasses = 6;
static_assert(kBloomBlurPasses % 2 == 0 && kBloomBlurPasses > 0,
              "kBloomBlurPasses must be a positive even number (full horizontal+vertical pairs)");
// kBloomStrength: postprocess.frag's additive blend multiplier on the
// final blurred bloom texture -- 1.0 adds it at its own real (already
// blurred, already HDR-scaled) brightness, the standard/expected bloom
// strength; not a magic number, just named so it's easy to re-tune later
// without hunting through render()'s uniform uploads.
constexpr float kBloomStrength = 1.0f;
// The shadow maps' depth textures are sampled on these fixed texture units
// every frame (see render()) -- unit 0 is always the current Material's
// diffuse texture and unit 1 its optional normal map (see
// material.hpp/Material::bind()), so 3+ is free and stays bound across every
// per-mesh Material::bind() call in the same frame (those never touch unit
// 3 or above). Phase 11 bug-review-style note: this used to start at unit 2,
// back when PBRMaterial only ever bound two of its own textures (albedo at
// textureUnit, normal at textureUnit + 1). Phase 11 adds a third
// PBRMaterial texture (the packed ORM map, bound at textureUnit + 2 -- see
// pbr_material.hpp) that would otherwise land on this exact unit and
// silently clobber whichever texture the shadow map/IBL maps left bound
// there, so every fixed unit below was shifted up by one to keep the
// material's own 0/1/2 range and the scene-level globals' range disjoint.
//
// Phase 13c: one shadow map per cascade now, so this is a base unit --
// cascade i is bound at kShadowMapTextureUnitBase + i (0, 1, 2 -- see
// Application::kCascadeCount) -- rather than a single fixed unit; every
// other scene-level global's unit below is shifted up accordingly to stay
// disjoint from all three.
constexpr unsigned int kShadowMapTextureUnitBase = 3;
// Phase 10: iblProbe_'s three precomputed maps, bound once per frame onto
// pbrShader_ (see render()) at fixed texture units that don't collide with
// any PBRMaterial::bind() call's own units (0 = albedo map, 1 = normal map,
// 2 = Phase 11's packed ORM map -- see pbr_material.hpp) or the 3 cascade
// shadow map units above (kShadowMapTextureUnitBase..+2).
constexpr unsigned int kIrradianceMapTextureUnit = 6;
constexpr unsigned int kPrefilterMapTextureUnit = 7;
constexpr unsigned int kBrdfLutTextureUnit = 8;

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
//
// kPointLights[2] (Phase 9 review): dedicated to the PBR sphere strip (see
// kSphereGridDistanceFromCamera/kSphereGridHeight below). The table-area
// lights above sit ~2.5-4 world units from the sphere strip's own position,
// and with this same short-range (1.0, 0.7, 1.8) attenuation profile that's
// enough to attenuate them to a few percent of their nominal intensity by
// the time they'd reach it -- confirmed by computing the actual distances
// from each light's fixed position to the sphere strip's own fixed center
// below. That left the strip lit by little more than the single
// (unattenuated, distance-independent) directional light's incidental
// specular reflection, which -- being one direction shared by every sphere
// in a row -- only happens to land in view on a couple of them rather than
// giving every sphere a comparable, reliably visible response. This light
// sits directly above the strip's own fixed center (position computed by
// hand from kDefaultCameraPosition/kSceneCenter/kSphereGridDistanceFromCamera/
// kSphereGridHeight the same way the constructor computes gridCenter below,
// since this table needs to stay a compile-time constant) so it reaches all
// 8 spheres at comparable range regardless of column, and is kept close to
// neutral white (not tinted like kPointLights[0]/[1]) so it doesn't bias the
// metallic row's own white-vs-albedo-tinted highlight comparison.
constexpr std::array<PointLightData, 3> kPointLights = {{
    {{0.5f, 0.95f, 0.1f}, {6.0f, 2.1f, 0.9f}, 1.0f, 0.7f, 1.8f},
    {{-1.35f, 1.15f, -0.3f}, {0.15f, 0.55f, 1.0f}, 1.0f, 0.7f, 1.8f},
    {{1.7673f, 1.9f, 2.3108f}, {5.5f, 5.2f, 4.7f}, 1.0f, 0.7f, 1.8f},
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

// Phase 13c: Cascaded Shadow Maps. Phase 7a/7b's single fixed orthographic
// projection (a hand-picked half-extent/near/far sized to cover the whole
// scene from one fixed light-space eye position -- see git history for that
// superseded computeLightSpaceMatrix()) is replaced by kCascadeCount
// separate, per-cascade orthographic projections, each tightly fitted
// around only the world-space region *that cascade's own depth slice of the
// camera's view frustum* actually covers -- see computeCascades() below.
// This is what gives CSM its resolution win: a cascade close to the camera
// covers a much smaller world-space area than the old whole-scene map did,
// so the same kShadowMapWidth/Height texel budget lands a proportionally
// finer world-space grid over it.
//
// 3 cascades is the standard/common choice for this technique (see e.g.
// LearnOpenGL's CSM article, GPU Gems 3 ch. 10) -- enough to show a
// meaningfully different world-space texel density between the nearest and
// farthest cascade without spending more shadow-map memory/depth-pass draw
// calls than this small test scene's shadow quality needs justify. (See
// Application::kCascadeCount, declared in application.hpp since
// shadowCascades_/renderShadowPass() there also need it.)
constexpr int kCascadeCount = Application::kCascadeCount;
static_assert(kCascadeCount == 3, "the constants/comments below assume 3 cascades");

// Split-depth scheme: the "practical split scheme" (GPU Gems 3, ch. 10) --
// a blend of a logarithmic split (equal *ratios* between consecutive split
// distances, matching how on-screen texel density/depth precision falls off
// with distance under a perspective projection) and a uniform split (equal
// *differences*, avoiding the log scheme's tendency to make the nearest
// cascade too thin to be useful). kCascadeSplitLambda = 0.5 blends the two
// evenly -- the commonly cited default (and this phase's own brief's
// suggested starting point) rather than a value hand-tuned to this specific
// scene. The actual resulting split distances are logged once at startup
// (see the constructor) so they're visible/checkable in a run's own log
// output rather than only inferable from these constants.
constexpr float kCascadeSplitLambda = 0.5f;

// Splits are computed between the camera's own near plane and
// kCascadeShadowDistance -- deliberately NOT the camera's real far plane
// (100 units, Camera's own default, never overridden by this engine -- see
// camera.hpp). This scene's own shadow-relevant content (scene.obj's table/
// pyramid/box, the ground plane -- kGroundHalfExtent's own diagonal-
// footprint comment below covers its reach, the largest of any caster here
// -- and the PBR sphere grid) all sits within roughly 8-9 world units of
// kDefaultCameraPosition, so splitting cascades across the camera's full
// 100-unit far plane would burn two of three cascades' resolution on empty
// space nothing ever casts a shadow into or onto. Capping cascade coverage
// at a "how far do shadows actually need to stay sharp" distance, separate
// from the camera's own far-plane culling distance, is standard practice
// (Unity/Unreal's own "max shadow distance" setting is exactly this same
// idea), not a scene-specific hack -- though the specific value (12.0) is
// chosen for this scene's own scale.
constexpr float kCascadeShadowDistance = 12.0f;

// Per-cascade frustum-fitting (see computeCascades()): each cascade's own
// light-space "eye" sits kCascadeLightBackoff back along -lightDir from that
// cascade's own frustum-slice center, and its fitted orthographic box is
// padded by kCascadeXYPadding (in the light's own right/up plane) and
// kCascadeZPadding (along the light's own view direction) beyond the tight
// bounding box of that slice's 8 unprojected corners. Both the backoff and
// the padding need to be generous enough that a shadow-casting object
// sitting just outside one cascade's own tight camera-frustum slice (e.g.
// the ground plane's edges reaching past where the camera can actually see
// them, or an object standing tall right at a cascade's depth boundary)
// still gets rendered into that cascade's own depth map rather than being
// clipped out of its light-space projection. Fixed, generous constants
// (rather than derived from the scene's real bounds) are enough here since
// this whole hand-authored scene comfortably fits within a ~20-unit radius
// of its own center -- a larger or more spread-out scene would need these
// derived from the scene's own actual bounds instead of hardcoded.
constexpr float kCascadeLightBackoff = 20.0f;
constexpr float kCascadeXYPadding = 1.0f;
constexpr float kCascadeZPadding = 5.0f;

// One cascade's own computed light-space matrix, plus the view-space depth
// marking its own FAR edge (the near edge is the previous cascade's own
// splitFar, or the camera's own near plane for cascade 0) -- uploaded to
// basic.frag/pbr.frag as uCascadeSplits[] so each fragment shader can pick
// which cascade it falls into (see render()).
struct Cascade {
    float splitFar;
    glm::mat4 lightSpaceMatrix;
};

// Builds all kCascadeCount cascades for this frame's camera pose: computes
// the practical-split-scheme depth ranges above, then for each range
// unprojects that depth slice's 8 frustum corners (via camera's own
// getProjectionMatrix(aspect, splitNear, splitFar) + frustumCornersWorldSpace(),
// see frustum.hpp) and fits a tight orthographic light-space projection
// around their bounding box -- the standard CSM per-cascade frustum-fitting
// method (LearnOpenGL's CSM article / GPU Gems 3 ch. 10), not a novel
// scheme.
// Split-depth computation, factored out of computeCascades() below purely
// so the constructor can also call it once at startup to log the actual
// resulting split distances (see this file's Phase 13c kCascadeSplitLambda
// comment) -- the split scheme itself only depends on the camera's own
// near plane and kCascadeShadowDistance, neither of which changes frame to
// frame, so there's nothing view-dependent for the constructor's one-off
// log line to be missing.
std::array<float, static_cast<std::size_t>(kCascadeCount) + 1> computeCascadeSplitDepths(float nearPlane,
                                                                                          float farPlane) {
    std::array<float, static_cast<std::size_t>(kCascadeCount) + 1> splitDepths{};
    splitDepths[0] = nearPlane;
    for (int i = 1; i <= kCascadeCount; ++i) {
        const float p = static_cast<float>(i) / static_cast<float>(kCascadeCount);
        const float logSplit = nearPlane * std::pow(farPlane / nearPlane, p);
        const float uniformSplit = nearPlane + (farPlane - nearPlane) * p;
        splitDepths[static_cast<std::size_t>(i)] =
            kCascadeSplitLambda * logSplit + (1.0f - kCascadeSplitLambda) * uniformSplit;
    }
    return splitDepths;
}

std::array<Cascade, kCascadeCount> computeCascades(const Camera& camera, float aspect, const glm::vec3& lightDir) {
    const float nearPlane = camera.nearPlane();
    const float farPlane = kCascadeShadowDistance;
    const std::array<float, static_cast<std::size_t>(kCascadeCount) + 1> splitDepths =
        computeCascadeSplitDepths(nearPlane, farPlane);

    std::array<Cascade, kCascadeCount> cascades{};
    for (int i = 0; i < kCascadeCount; ++i) {
        const float splitNear = splitDepths[static_cast<std::size_t>(i)];
        const float splitFar = splitDepths[static_cast<std::size_t>(i) + 1];

        const glm::mat4 sliceProjection = camera.getProjectionMatrix(aspect, splitNear, splitFar);
        const glm::mat4 sliceViewProjection = sliceProjection * camera.getViewMatrix();
        const std::array<glm::vec3, 8> corners = frustumCornersWorldSpace(sliceViewProjection);

        glm::vec3 center(0.0f);
        for (const glm::vec3& corner : corners) {
            center += corner;
        }
        center /= static_cast<float>(corners.size());

        const glm::vec3 eye = center - lightDir * kCascadeLightBackoff;
        const glm::mat4 lightView = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));

        glm::vec3 minBounds(std::numeric_limits<float>::max());
        glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
        for (const glm::vec3& corner : corners) {
            const glm::vec3 lightSpaceCorner = glm::vec3(lightView * glm::vec4(corner, 1.0f));
            minBounds = glm::min(minBounds, lightSpaceCorner);
            maxBounds = glm::max(maxBounds, lightSpaceCorner);
        }

        // Light-view-space Z is negative in front of the eye (OpenGL's
        // usual "camera looks down -Z" convention) and grows more negative
        // with distance -- glm::ortho's near/far are positive *distances*
        // forward from the eye, so the box's nearest corner (largest,
        // least-negative Z) maps to the smaller distance and its farthest
        // corner (most negative Z) maps to the larger one.
        const float orthoNear = -maxBounds.z - kCascadeZPadding;
        const float orthoFar = -minBounds.z + kCascadeZPadding;
        const glm::mat4 lightProjection =
            glm::ortho(minBounds.x - kCascadeXYPadding, maxBounds.x + kCascadeXYPadding,
                       minBounds.y - kCascadeXYPadding, maxBounds.y + kCascadeXYPadding, orthoNear, orthoFar);

        cascades[static_cast<std::size_t>(i)] = Cascade{splitFar, lightProjection * lightView};
    }
    return cascades;
}

// Phase 9 bug-review composition fix: the original layout here was a packed
// 4x4 grid (16 spheres, both metallic and roughness swept as a 2D matrix)
// sitting directly in the camera's image plane at kSphereGridDistanceFromCamera
// = 2.1, i.e. *in front of* the table/box/pyramid scene along the same
// view direction. At that distance and a 60-degree vertical FOV, 4 columns
// spanning kSphereGridCols * kSphereSpacing world units filled almost the
// entire frame width, so the grid didn't read as "foreground subject next to
// background scene" -- it visually buried the table/box/pyramid (squeezed
// into a gap between the middle two spheres) and, being uniformly large,
// dark red-orange spheres shoulder-to-shoulder, made the metallic/roughness
// gradient nearly invisible (see the pre-fix screenshot this review captured
// before touching any of the constants below). Two changes fix this:
//   1. Two rows of kSphereRowLength (4) instead of a 4x4 matrix -- each row
//      sweeps exactly one axis with the other axis held fixed, which is more
//      legible than a matrix at this sphere count/size (per-row comments
//      below) -- and
//   2. The whole two-row strip is placed low and roughly centered in the
//      camera's image plane, in the wide-open foreground ground area in
//      front of (i.e. at a shallower depth *and* lower in frame than) the
//      table/box/pyramid rather than dead-center overlapping them -- see
//      kSphereGridDistanceFromCamera/kSphereGridHeight below, values chosen
//      by re-projecting every sphere's world position through this engine's
//      own view/projection matrices and checking the resulting 800x600 pixel
//      footprint against the existing scene's own (also re-projected)
//      footprint.
//
// Every sphere still shares one albedo (a saturated, strongly non-grey
// red-orange): this is deliberate, not a missed opportunity to show more
// colors -- holding albedo fixed across the whole strip is exactly what makes
// the metal/dielectric Fresnel distinction directly comparable column to
// column (a metallic=1 sphere's highlight should visibly pick up this same
// red-orange tint via F0 = albedo, while a metallic=0 sphere's highlight
// should stay neutral/white via F0 = 0.04, regardless of sharing the same
// base color).
//
// Columns are laid out using the camera's own right vector (computed from
// kDefaultCameraPosition/kSceneCenter in the constructor below), not world
// X/Z: an axis-aligned world-space row recedes away from the camera along a
// mostly depth-facing direction from this engine's fixed camera angle, which
// foreshortens spacing so hard that adjacent spheres visibly crowd on screen
// even with generous world-space spacing between their centers. gridRight is
// always exactly horizontal (cross(forward, worldUp) has no Y component by
// construction) regardless of the camera's pitch, so columns stay evenly
// spaced in *screen* space without needing any world-Y component at all.
// The two rows themselves, by contrast, are stacked along plain world Y (see
// kSphereGridHeight/kSphereRowSeparation below) rather than the camera's own
// (pitched) up vector: an earlier version of this fix used the camera's up
// vector for the row offset too, which -- because that up vector isn't
// purely vertical -- silently pushed the lower row's world Y down far enough
// to sink partway *below* the ground plane, producing a visible
// interpenetration artifact (the ground plane poking a notch out of the
// lower-left sphere) caught in this same screenshot-driven review. Plain
// world-Y stacking sidesteps that: both rows' height is set directly and
// independently of any camera-relative direction, so the sphere-radius/
// ground-height margin below is exact, not an incidental side effect of the
// camera's pitch.
constexpr int kSphereRowLength = 4;  // spheres per row (metallic OR roughness axis)
// Radius/spacing chosen so adjacent spheres' screen-space footprints stay
// clearly, individually separated -- spacing is roughly 4x the sphere's own
// screen-space diameter at this distance, not just barely more than 2x
// (touching) like the pre-fix grid's 0.62 spacing at 0.22 radius was.
constexpr float kSphereRadius = 0.14f;
constexpr float kSphereColSpacing = 0.6f;
// World-unit gap between the two rows' centers (each row's own center offset
// by half this, above/below kSphereGridHeight).
constexpr float kSphereRowSeparation = 0.58f;
// Distance from kDefaultCameraPosition, measured along the camera's own view
// direction, that fixes the strip's X/Z position (its Y is set independently
// by kSphereGridHeight below -- see this block's header comment on why).
// Deliberately *closer* to the camera than the scene's own objects (roughly
// 4.6 units away) so the strip reads as a foreground subject.
constexpr float kSphereGridDistanceFromCamera = 1.5f;
// Absolute world-space height of the two-row strip's shared center (each row
// then offset by +/- kSphereRowSeparation/2 from this). Chosen, together with
// kSphereRowSeparation and kSphereRadius, so that:
//   - the lower row's sphere bottoms (kSphereGridHeight - kSphereRowSeparation/2
//     - kSphereRadius) stay safely above the ground plane's own Y
//     (kGroundY = -0.01), avoiding the interpenetration artifact described
//     above, and
//   - both rows' re-projected screen footprint sits low in the 800x600 frame,
//     entirely below the existing table/box/pyramid scene's own re-projected
//     footprint and comfortably inside the frame's bottom edge.
constexpr float kSphereGridHeight = 0.6f;
constexpr glm::vec3 kSphereAlbedo{0.85f, 0.12f, 0.08f};
// Roughness sweep (the row that holds metallic fixed) intentionally still
// starts at kMinPBRRoughness rather than exactly 0 -- matching
// PBRMaterial::kMinRoughness's own floor (see that header's comment on why
// alpha = roughness^2 == 0 is a singular case for the GGX distribution) -- and
// goes up to fully rough at 1.0, so its highlight visibly shrinks from a
// tight glint down to a broad, soft one left to right.
constexpr float kMinPBRRoughness = 0.05f;
constexpr float kMaxPBRRoughness = 1.0f;
// Metallic sweep row: held at a fixed roughness low enough for the
// prefiltered-environment IBL reflection (see engine::IBLProbe/pbr.frag's
// Phase 10 ambient term) to read as a recognizably crisp, mirror-ish
// reflection rather than a soft, generic-looking glow -- not so low that the
// GGX highlight collapses to a near-single-pixel point at this screen size
// (Phase 9's original concern) nor so low that Reinhard tonemapping drives
// its peak texel to the display's white point before any F0 tint survives
// (also Phase 9's original concern, see this constant's superseded comment
// in git history). 0.2 keeps the direct-light specular highlight comfortably
// sized and non-saturating while giving the IBL specular term enough
// coherence (mip ~0.2 * MAX_REFLECTION_LOD of engine::IBLProbe's prefiltered
// cubemap) to visibly carry the skybox's own gradient across the sphere's
// body, not just its highlight -- letting metallic = 0 -> 1 read as
// "diffuse/matte -> reflective/mirror-ish", not just "dim -> bright".
constexpr float kMetallicRowRoughness = 0.2f;
// Roughness sweep row: fully metallic (Phase 10 revision -- Phase 9's
// original 0.35 compromise is now obsolete). Phase 9's review explicitly
// flagged 0.35 as a stopgap: "with no image-based lighting yet ... a fully
// metallic sphere is legitimately near-black everywhere except the couple of
// pixels where a light's specular reflection happens to land ... unreadable
// as a demo before IBL exists to give metals their usual
// reflected-environment brightness" (see git history for that comment in
// full). IBL now exists (engine::IBLProbe's prefiltered specular cubemap),
// so a fully metallic sphere here is no longer near-black: its specular IBL
// term alone (F0 = albedo, no diffuse term to speak of) picks up a visible,
// environment-tinted reflection at every roughness value, letting this row
// demonstrate its intended effect -- a sharp mirror-like reflection at low
// roughness (left) softening into a broad, blurred one at high roughness
// (right) -- exactly the way a real metal's reflection behaves, rather than
// the diffuse-dominated compromise 0.35 was standing in for.
constexpr float kRoughnessRowMetallic = 1.0f;
constexpr float kSphereAO = 1.0f;

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

// Phase 13c: uploads every cascade's own light-space matrix + far split
// depth (uLightSpaceMatrices[i]/uCascadeSplits[i] in basic.frag/pbr.frag) --
// shared by both shader_ and pbrShader_'s per-frame uploads in render(),
// same "uploadPointLight/uploadSpotLight, called once per light per
// program" pattern above.
void uploadCascades(Shader& shader, const std::array<Cascade, kCascadeCount>& cascades) {
    for (int i = 0; i < kCascadeCount; ++i) {
        const std::string index = std::to_string(i);
        shader.setMat4("uLightSpaceMatrices[" + index + "]", cascades[static_cast<std::size_t>(i)].lightSpaceMatrix);
        shader.setFloat("uCascadeSplits[" + index + "]", cascades[static_cast<std::size_t>(i)].splitFar);
    }
}

bool cameraDemoModeFromEnv() {
    const char* value = std::getenv("ENGINE_CAMERA_DEMO");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

// Phase 13b: same getenv-gated-behavior pattern as cameraDemoModeFromEnv()
// above -- see frustumCullDemoMode_'s own comment in application.hpp for
// what this flag does.
bool frustumCullDemoModeFromEnv() {
    const char* value = std::getenv("ENGINE_FRUSTUM_CULL_DEMO");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

// Phase 13b: render() logs one combined "N/M culled" line every this-many
// frames (not every frame) -- frequent enough to see the count actually
// change as ENGINE_CAMERA_DEMO's waypoints step (every 20 frames, see
// update()'s kFramesPerStep) or across an ENGINE_FRUSTUM_CULL_DEMO run,
// without flooding the log with a near-identical line every single frame.
constexpr std::uint64_t kCullLogFrameInterval = 15;

}  // namespace

Application::Application(int width, int height, const std::string& title, std::uint64_t maxFrames)
    : window_(width, height, title),
      shader_(resources_.getShader(kVertexShaderPath, kFragmentShaderPath)),
      shadowShader_(resources_.getShader(kShadowVertexShaderPath, kShadowFragmentShaderPath)),
      skyboxShader_(resources_.getShader(kSkyboxVertexShaderPath, kSkyboxFragmentShaderPath)),
      postProcessShader_(resources_.getShader(kPostProcessVertexShaderPath, kPostProcessFragmentShaderPath)),
      bloomExtractShader_(resources_.getShader(kPostProcessVertexShaderPath, kBloomExtractFragmentShaderPath)),
      blurShader_(resources_.getShader(kPostProcessVertexShaderPath, kBlurFragmentShaderPath)),
      pbrShader_(resources_.getShader(kPBRVertexShaderPath, kPBRFragmentShaderPath)),
      irradianceShader_(resources_.getShader(kCubemapCaptureVertexShaderPath, kIrradianceFragmentShaderPath)),
      prefilterShader_(resources_.getShader(kCubemapCaptureVertexShaderPath, kPrefilterFragmentShaderPath)),
      brdfShader_(resources_.getShader(kPostProcessVertexShaderPath, kBrdfLutFragmentShaderPath)),
      // Phase 13c: kCascadeCount independent ShadowMap instances, all the
      // same fixed resolution (see kShadowMapWidth/Height's own Phase 13c
      // comment) -- std::array<ShadowMap, N>'s usual aggregate
      // initialization, each element move-constructed from its own
      // temporary ShadowMap(width, height).
      shadowCascades_{{ShadowMap(kShadowMapWidth, kShadowMapHeight), ShadowMap(kShadowMapWidth, kShadowMapHeight),
                       ShadowMap(kShadowMapWidth, kShadowMapHeight)}},
      // Phase 7b: sized from window_'s own real framebuffer size (already
      // constructed at this point -- see this header's declaration-order
      // comment) rather than the constructor's width/height parameters
      // directly, so this stays correct even on a HiDPI display where the
      // framebuffer is larger than the window's requested size.
      //
      // MSAA HDR framebuffer bug fix: constructed multisampled now, at
      // engine::kRequestedMsaaSamples (window.hpp) -- the same count
      // Window requests for the default framebuffer, for consistency (see
      // application.hpp's own MSAA bug-fix comment) -- rather than the
      // default (0, single-sample) every other Framebuffer in this class
      // still uses. Framebuffer itself clamps this to what the driver
      // actually grants and logs both values (see framebuffer.cpp).
      hdrFramebuffer_(window_.getSize().first, window_.getSize().second, kRequestedMsaaSamples),
      // MSAA HDR framebuffer bug fix: a same-size, single-sample sibling
      // hdrFramebuffer_ resolves into every frame (see render()) -- default
      // (0) sample count, same as every pre-existing Framebuffer instance
      // below.
      hdrResolveFramebuffer_(window_.getSize().first, window_.getSize().second),
      // Phase 11: bloom's own off-screen targets, sized at
      // 1/kBloomDownsampleFactor of the window's real framebuffer
      // resolution -- see this header's Phase 11 comment on
      // brightFramebuffer_/pingpongFramebuffer0_/1_ for why half-res. `/ 2`
      // (not e.g. rounding up) matches every dimension this engine actually
      // runs at (800x600 and other common even sizes); a stray odd input
      // resolution would round down by one texel here, which has no
      // visible consequence for a soft blur target.
      brightFramebuffer_(window_.getSize().first / kBloomDownsampleFactor,
                          window_.getSize().second / kBloomDownsampleFactor),
      pingpongFramebuffer0_(window_.getSize().first / kBloomDownsampleFactor,
                             window_.getSize().second / kBloomDownsampleFactor),
      pingpongFramebuffer1_(window_.getSize().first / kBloomDownsampleFactor,
                             window_.getSize().second / kBloomDownsampleFactor),
      skybox_(kSkyboxFacePaths),
      // Phase 10: convolves skybox_'s own just-constructed cubemap (see
      // ibl_probe.hpp) -- must come after skybox_ (and after
      // irradianceShader_/prefilterShader_/brdfShader_ above), matching this
      // header's own declaration-order comment.
      iblProbe_(skybox_.textureId(), *irradianceShader_, *prefilterShader_, *brdfShader_),
      // Phase 7a's demo normal-mapped surface -- see mesh.hpp's
      // makeGroundPlane() and this class's Phase 7a header comment. Shares
      // shader_ (the main lit program) with every entity's Model, unlike
      // shadowShader_ above (the depth-only program).
      groundMesh_(makeGroundPlane(kGroundHalfExtent, kGroundY, kGroundUvTiling)),
      groundMaterial_(*shader_, resources_.getTexture(kGroundDiffuseTexturePath), /*tint=*/glm::vec3(1.0f),
                      /*shininess=*/24.0f, resources_.getTexture(kGroundNormalMapPath)),
      postProcessQuad_(makeFullscreenQuad()),
      // Phase 9: the PBR sphere grid's shared geometry -- one Mesh, reused
      // (with a different PBRMaterial + Transform) by every sphere in
      // sphereInstances_ (built below, in the constructor body, since it
      // needs *pbrShader_ already constructed -- see this class's Phase 9
      // header note on declaration order).
      sphereMesh_(makeUVSphere(32, 32, kSphereRadius)),
      camera_(kDefaultCameraPosition),
      maxFrames_(maxFrames),
      cameraDemoMode_(cameraDemoModeFromEnv()),
      frustumCullDemoMode_(frustumCullDemoModeFromEnv()) {
    // No depth buffer testing existed in Phase 1 (nothing but a flat clear
    // needed it); real 3D geometry does, so faces occlude each other
    // correctly instead of painting in draw-call order.
    GL_CHECK(glEnable(GL_DEPTH_TEST));

    if (frustumCullDemoMode_) {
        // Phase 13b: same position as the normal default camera, but aimed
        // at the point directly behind it (mirrored through
        // kDefaultCameraPosition) instead of at kSceneCenter -- i.e. facing
        // 180 degrees away from the whole scene, so every entity/the ground
        // plane/every PBR sphere should end up outside the frustum and get
        // culled. update() leaves this pose alone every frame (see its own
        // frustumCullDemoMode_ branch) rather than re-deriving it, since a
        // fixed demo pose needs no per-frame recomputation.
        const glm::vec3 awayTarget = kDefaultCameraPosition + (kDefaultCameraPosition - kSceneCenter);
        camera_.setPositionLookingAt(kDefaultCameraPosition, awayTarget);
        LOG_INFO("ENGINE_FRUSTUM_CULL_DEMO set: camera faces away from the scene to prove culling drops the draw count");
    } else {
        camera_.setPositionLookingAt(kDefaultCameraPosition, kSceneCenter);
    }

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

    // Phase 9 bug-review composition fix: two single-axis rows instead of a
    // packed 4x4 matrix -- see kSphereRowLength/kSphereGridDistanceFromCamera's
    // comment above for why. Columns are laid out using the camera's own
    // right vector (gridRight, always exactly horizontal), so they're evenly
    // spaced in screen space regardless of this engine's fixed camera angle;
    // the two rows themselves are stacked along plain world Y (gridCenterY +/-
    // kSphereRowSeparation/2), not the camera's own up vector -- see this
    // block's header comment above for why (avoiding the ground-plane
    // interpenetration artifact a camera-up-based row offset produced).
    const glm::vec3 gridForward = glm::normalize(kSceneCenter - kDefaultCameraPosition);
    const glm::vec3 gridRight = glm::normalize(glm::cross(gridForward, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 gridCenter = kDefaultCameraPosition + gridForward * kSphereGridDistanceFromCamera;
    gridCenter.y = kSphereGridHeight;

    sphereInstances_.reserve(2 * static_cast<std::size_t>(kSphereRowLength));

    // Phase 11: two of the grid's flat-albedo-only spheres (one per row, an
    // interior column each -- not either row's extreme ends, so the
    // metallic=0/1 and roughness=min/max contrast points this grid exists to
    // demonstrate stay intact) are replaced with real textured PBR materials,
    // so the screenshot shows actual surface detail/variation within an
    // object rather than only flat colors swept across separate objects.
    // Textures loaded once here (through resources_, like every other asset)
    // rather than per-sphere-instance, even though each is only used once --
    // consistent with how every other texture in this engine gets loaded.
    auto rustedMetalAlbedo = resources_.getTexture(kRustedMetalAlbedoPath);
    auto rustedMetalORM = resources_.getTexture(kRustedMetalORMPath);
    auto scuffedPlasticAlbedo = resources_.getTexture(kScuffedPlasticAlbedoPath);
    auto scuffedPlasticORM = resources_.getTexture(kScuffedPlasticORMPath);
    // Column index (within each 4-wide row) replaced by a textured material
    // -- deliberately an interior column (not 0 or kSphereRowLength - 1) on
    // each row, so both rows' most illustrative points (metallic 0 vs. 1;
    // roughness min vs. max) stay pure scalar-swept spheres.
    constexpr int kRustedMetalCol = 1;
    constexpr int kScuffedPlasticCol = 2;

    // Row A (upper of the two, placed above gridCenter): metallic sweep
    // 0 -> 1 left to right at a fixed low roughness -- see
    // kMetallicRowRoughness's comment above. Demonstrates the Fresnel
    // dielectric/metal distinction: a neutral/white highlight (and dim, matte
    // red-orange body) at the metallic = 0 end versus a bright,
    // red-orange-tinted highlight (and near-black diffuse-less body) at the
    // metallic = 1 end.
    for (int col = 0; col < kSphereRowLength; ++col) {
        const float metallic = kSphereRowLength > 1
                                    ? static_cast<float>(col) / static_cast<float>(kSphereRowLength - 1)
                                    : 0.0f;
        const float colOffset =
            (static_cast<float>(col) - static_cast<float>(kSphereRowLength - 1) * 0.5f) * kSphereColSpacing;

        // col == kRustedMetalCol: a rusted-metal textured material instead of
        // the row's own scalar metallic sweep value -- the albedo/ORM
        // textures' own per-pixel metallic (mostly high, low in the rust
        // patches) and roughness (mostly low, high in the rust patches)
        // replace `metallic`/kMetallicRowRoughness entirely once bound (see
        // pbr.frag's uUseORMMap branch), so this sphere reads as "a real
        // rusted metal surface" rather than one more point on the sweep.
        SphereInstance instance =
            (col == kRustedMetalCol)
                ? SphereInstance{Transform{}, PBRMaterial(*pbrShader_, glm::vec3(1.0f), metallic,
                                                           kMetallicRowRoughness, kSphereAO, rustedMetalAlbedo,
                                                           /*normalMap=*/nullptr, rustedMetalORM)}
                : SphereInstance{Transform{}, PBRMaterial(*pbrShader_, kSphereAlbedo, metallic,
                                                           kMetallicRowRoughness, kSphereAO)};
        glm::vec3 position = gridCenter + gridRight * colOffset;
        position.y = kSphereGridHeight + kSphereRowSeparation * 0.5f;
        instance.transform.setPosition(position);
        sphereInstances_.push_back(std::move(instance));
    }

    // Row B (lower of the two, placed below gridCenter): roughness sweep
    // kMinPBRRoughness -> kMaxPBRRoughness left to right at fixed metallic =
    // 1 -- see kRoughnessRowMetallic's comment above. Demonstrates the
    // highlight visibly shrinking from a tight, bright glint at the smoothest
    // (left) end to a broad, soft one at the fully-rough (right) end.
    for (int col = 0; col < kSphereRowLength; ++col) {
        const float roughness = kSphereRowLength > 1
                                     ? kMinPBRRoughness + (kMaxPBRRoughness - kMinPBRRoughness) *
                                                              (static_cast<float>(col) /
                                                               static_cast<float>(kSphereRowLength - 1))
                                     : kMinPBRRoughness;
        const float colOffset =
            (static_cast<float>(col) - static_cast<float>(kSphereRowLength - 1) * 0.5f) * kSphereColSpacing;

        // col == kScuffedPlasticCol: a scuffed-plastic textured material --
        // same reasoning as kRustedMetalCol above, just on this row (a
        // near-zero-metallic, glossy-with-matte-scratches surface, the
        // dielectric counterpart to the metal sphere above).
        SphereInstance instance =
            (col == kScuffedPlasticCol)
                ? SphereInstance{Transform{}, PBRMaterial(*pbrShader_, glm::vec3(1.0f), kRoughnessRowMetallic,
                                                           roughness, kSphereAO, scuffedPlasticAlbedo,
                                                           /*normalMap=*/nullptr, scuffedPlasticORM)}
                : SphereInstance{Transform{}, PBRMaterial(*pbrShader_, kSphereAlbedo, kRoughnessRowMetallic,
                                                           roughness, kSphereAO)};
        glm::vec3 position = gridCenter + gridRight * colOffset;
        position.y = kSphereGridHeight - kSphereRowSeparation * 0.5f;
        instance.transform.setPosition(position);
        sphereInstances_.push_back(std::move(instance));
    }

    if (cameraDemoMode_) {
        LOG_INFO("ENGINE_CAMERA_DEMO set: driving the camera through a scripted orbit instead of live input");
    }

    // Phase 13c: log the practical-split-scheme's actual resulting cascade
    // split distances once at startup -- kCascadeSplitLambda/
    // kCascadeShadowDistance above are the tunable inputs, but this is the
    // real output a reviewer would otherwise have to compute by hand to
    // check.
    {
        const std::array<float, static_cast<std::size_t>(kCascadeCount) + 1> splitDepths =
            computeCascadeSplitDepths(camera_.nearPlane(), kCascadeShadowDistance);
        std::string splitsLog = "CSM cascade splits (view-space depth):";
        for (std::size_t i = 0; i < splitDepths.size(); ++i) {
            splitsLog += " " + std::to_string(splitDepths[i]);
        }
        LOG_INFO(splitsLog);
    }

    LOG_INFO("Application initialized");
}

void Application::update(double deltaTime, const InputState& input) {
    totalTime_ += deltaTime;

    if (frustumCullDemoMode_) {
        // Phase 13b: the camera's fixed "facing away from the scene" pose
        // was already set once in the constructor -- nothing to do here
        // every frame (no waypoints to step through, no real input to
        // read), and reading real InputState/mouse position would let a
        // stray Xvfb event nudge the camera back towards the scene, which
        // this demo mode specifically needs to not happen.
    } else if (cameraDemoMode_) {
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

void Application::renderShadowPass(const std::array<glm::mat4, kCascadeCount>& lightSpaceMatrices) {
    // Phase 13c: the whole scene is depth-rendered once per cascade, into
    // that cascade's own ShadowMap -- shadowCascades_[i]'s own light-space
    // matrix differs (a tighter, per-cascade-fitted projection -- see
    // computeCascades()), so this can't be collapsed into a single pass the
    // way the old one-shadow-map version was. Everything the color pass
    // draws (entities_, the ground plane, the PBR sphere grid) still needs
    // to appear in every cascade's own depth map exactly as before -- a
    // caster missing from one cascade's map would show a wrong (missing or
    // stale) shadow for any fragment sampling that cascade.
    for (int cascade = 0; cascade < kCascadeCount; ++cascade) {
        // Points the viewport at this cascade's own resolution and binds
        // its FBO; render() restores the window's real viewport (and
        // default framebuffer binding, done once below) once this returns.
        shadowCascades_[static_cast<std::size_t>(cascade)].bindForWriting();
        // Only a depth buffer exists on this FBO (see ShadowMap -- no color
        // attachment at all), so only GL_DEPTH_BUFFER_BIT is meaningful to
        // clear here.
        GL_CHECK(glClear(GL_DEPTH_BUFFER_BIT));

        shadowShader_->use();
        shadowShader_->setMat4("uLightSpaceMatrix", lightSpaceMatrices[static_cast<std::size_t>(cascade)]);

        for (const Entity& entity : entities_) {
            if (entity.model()) {
                entity.model()->drawDepthOnly(*shadowShader_, entity.transform.getModelMatrix());
            }
        }

        // The ground plane too, for the same "depth pass renders everything
        // the main pass renders" reason -- its own geometry is already
        // baked in world space (see makeGroundPlane()), so its model matrix
        // is identity.
        shadowShader_->setMat4("uModel", glm::mat4(1.0f));
        groundMesh_.bind();
        groundMesh_.draw();

        // Phase 9: the PBR sphere grid casts/receives shadows through this
        // same depth-only pass -- shadow.vert reads only aPos (see that
        // file), so it doesn't matter that these spheres' color pass uses
        // pbrShader_ rather than shader_. sphereMesh_ is bound once and
        // drawn once per instance, re-uploading only uModel between draws
        // (each instance shares the same geometry).
        sphereMesh_.bind();
        for (const SphereInstance& instance : sphereInstances_) {
            shadowShader_->setMat4("uModel", instance.transform.getModelMatrix());
            sphereMesh_.draw();
        }
    }

    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

void Application::render() {
    const auto [fbWidth, fbHeight] = window_.getSize();
    const float aspect = fbHeight != 0 ? static_cast<float>(fbWidth) / static_cast<float>(fbHeight) : 1.0f;

    // Phase 13c: the camera's own aspect ratio (computed above) feeds
    // computeCascades()'s per-cascade sub-frustum construction (see
    // Camera::getProjectionMatrix's near/far overload), so cascades must be
    // (re)computed here, fresh every frame, same as the frustum/view/
    // projection matrices below -- never cached across frames.
    const std::array<Cascade, kCascadeCount> cascades =
        computeCascades(camera_, aspect, glm::normalize(kLightDirection));
    std::array<glm::mat4, kCascadeCount> lightSpaceMatrices{};
    for (int i = 0; i < kCascadeCount; ++i) {
        lightSpaceMatrices[static_cast<std::size_t>(i)] = cascades[static_cast<std::size_t>(i)].lightSpaceMatrix;
    }

    // The directional light's cascaded shadow maps are rendered first, into
    // their own depth-only FBOs/viewport; glViewport is restored to the
    // window's real framebuffer size immediately after, since
    // renderShadowPass() leaves it pointed at whichever cascade it wrote to
    // last (all the same, generally different-from-the-window, resolution).
    renderShadowPass(lightSpaceMatrices);
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

    // Phase 13b: the frustum is derived fresh from this frame's own view/
    // projection (never cached across frames -- matching Camera's own "no
    // premature caching" style, see camera.hpp) and shared by every
    // drawable tested below (entities_/Model's per-mesh nodes, the ground
    // plane, every PBR sphere instance) -- one extraction per frame, not
    // one per object. cullStats accumulates a running total/culled count
    // across all of them so it can be logged once, below, after every
    // drawable this frame has been considered.
    const glm::mat4 viewProjection = projection * view;
    const Frustum frustum(viewProjection);
    CullStats cullStats;

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
    uploadCascades(*shader_, cascades);
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

    // Phase 13c: all kCascadeCount cascades' shadow maps bound once here
    // (not per-material) at fixed texture units that no Material::bind()
    // call below ever touches (see kShadowMapTextureUnitBase's comment) --
    // they stay bound across every subsequent draw call this frame. Each
    // cascade gets its own named sampler uniform (uShadowMap0/1/2, not a
    // sampler array -- see basic.frag's own comment on why) pointed at its
    // own unit.
    for (int i = 0; i < kCascadeCount; ++i) {
        shadowCascades_[static_cast<std::size_t>(i)].bindForReading(kShadowMapTextureUnitBase +
                                                                     static_cast<unsigned int>(i));
        shader_->setInt("uShadowMap" + std::to_string(i),
                         static_cast<int>(kShadowMapTextureUnitBase + static_cast<unsigned int>(i)));
    }
    // Phase 7b bug-review fix: PCF (percentage-closer filtering) in
    // basic.frag's shadowFactorForCascade() samples a small 3x3 grid of
    // texels around each fragment's shadow-map lookup rather than just the
    // one nearest texel, so shadow edges soften into a gradient of
    // partially-shadowed values instead of a single hard-aliased 0/1 step.
    // It needs the shadow map's own per-texel size (in [0,1] shadow-space
    // units) to offset those samples by the right amount -- derived here
    // from one cascade's real resolution (every cascade shares the same
    // one, see kShadowMapWidth/Height's own Phase 13c comment) rather than
    // hardcoded in the shader, so it stays correct if kShadowMapWidth/
    // Height above ever changes.
    shader_->setVec2("uShadowMapTexelSize",
                       glm::vec2(1.0f / static_cast<float>(shadowCascades_[0].width()),
                                 1.0f / static_cast<float>(shadowCascades_[0].height())));

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
            entity.model()->draw(*shader_, entity.transform.getModelMatrix(), &frustum, &cullStats);
        }
    }

    // Phase 7a's ground plane: drawn directly (not through Entity/Model,
    // see this class's header comment) with an identity model matrix, since
    // makeGroundPlane() already bakes its position into world-space vertex
    // data. Phase 13b: tested against the frustum like every other
    // drawable, even though in this engine's small fixed scene it's about
    // as likely to be culled as anything else looking away from the scene
    // would be -- there's nothing architecturally special about the ground
    // plane that should exempt it from the same culling every other
    // drawable gets.
    {
        const glm::mat4 groundModel(1.0f);
        ++cullStats.totalDrawables;
        const BoundingSphere groundWorldSphere = groundMesh_.boundingSphere().transformed(groundModel);
        if (frustum.intersects(groundWorldSphere.center, groundWorldSphere.radius)) {
            const glm::mat3 groundNormalMatrix = glm::inverseTranspose(glm::mat3(groundModel));
            shader_->setMat4("uModel", groundModel);
            shader_->setMat3("uNormalMatrix", groundNormalMatrix);
            groundMaterial_.bind();
            groundMesh_.bind();
            groundMesh_.draw();
        } else {
            ++cullStats.culledDrawables;
        }
    }

    // Phase 9: the PBR sphere test-grid, drawn with its own program
    // (pbrShader_/pbr.vert/pbr.frag) after the Blinn-Phong entities_/ground
    // plane above -- see this class's Phase 9 header comment. Scene-level
    // uniforms (view/projection/light-space matrix, the directional light,
    // ambient, view position, every point/spot light, and the shadow map)
    // are the exact same values already uploaded to shader_ above; they're
    // re-uploaded here onto pbrShader_ because GL uniform state lives on
    // each program object independently -- switching the active program via
    // use() does not carry shader_'s uniform values over to pbrShader_.
    pbrShader_->use();
    pbrShader_->setMat4("uView", view);
    pbrShader_->setMat4("uProjection", projection);
    uploadCascades(*pbrShader_, cascades);
    pbrShader_->setVec3("uLightDirection", kLightDirection);
    pbrShader_->setVec3("uLightColor", kLightColor);
    pbrShader_->setVec3("uAmbientColor", kAmbientColor);
    pbrShader_->setVec3("uViewPos", camera_.position());

    pbrShader_->setInt("uNumPointLights", static_cast<int>(kPointLights.size()));
    for (std::size_t i = 0; i < kPointLights.size(); ++i) {
        uploadPointLight(*pbrShader_, i, kPointLights[i]);
    }
    pbrShader_->setInt("uNumSpotLights", static_cast<int>(kSpotLights.size()));
    for (std::size_t i = 0; i < kSpotLights.size(); ++i) {
        uploadSpotLight(*pbrShader_, i, kSpotLights[i]);
    }

    // Every cascade's shadow map is still bound for reading on its own unit
    // from the upload above (binding a texture unit is global GL state, not
    // per-program) -- only each sampler uniform needs re-pointing at that
    // same unit on this different program.
    for (int i = 0; i < kCascadeCount; ++i) {
        pbrShader_->setInt("uShadowMap" + std::to_string(i),
                            static_cast<int>(kShadowMapTextureUnitBase + static_cast<unsigned int>(i)));
    }
    pbrShader_->setVec2("uShadowMapTexelSize",
                         glm::vec2(1.0f / static_cast<float>(shadowCascades_[0].width()),
                                   1.0f / static_cast<float>(shadowCascades_[0].height())));

    // Phase 10: iblProbe_'s three precomputed maps -- bound once per frame
    // (they never change after startup, see ibl_probe.hpp) at fixed texture
    // units, with pbr.frag's sampler uniforms pointed at those same units.
    iblProbe_.bindForSampling(kIrradianceMapTextureUnit, kPrefilterMapTextureUnit, kBrdfLutTextureUnit);
    pbrShader_->setInt("uIrradianceMap", static_cast<int>(kIrradianceMapTextureUnit));
    pbrShader_->setInt("uPrefilterMap", static_cast<int>(kPrefilterMapTextureUnit));
    pbrShader_->setInt("uBrdfLUT", static_cast<int>(kBrdfLutTextureUnit));

    sphereMesh_.bind();
    for (const SphereInstance& instance : sphereInstances_) {
        const glm::mat4 sphereModel = instance.transform.getModelMatrix();

        // Phase 13b: every instance shares sphereMesh_'s own local bounding
        // sphere, transformed by this instance's own model matrix -- see
        // BoundingSphere::transformed(). ++cullStats bookkeeping mirrors the
        // ground plane's above.
        ++cullStats.totalDrawables;
        const BoundingSphere sphereWorldSphere = sphereMesh_.boundingSphere().transformed(sphereModel);
        if (!frustum.intersects(sphereWorldSphere.center, sphereWorldSphere.radius)) {
            ++cullStats.culledDrawables;
            continue;
        }

        const glm::mat3 sphereNormalMatrix = glm::inverseTranspose(glm::mat3(sphereModel));
        pbrShader_->setMat4("uModel", sphereModel);
        pbrShader_->setMat3("uNormalMatrix", sphereNormalMatrix);
        instance.material.bind();
        sphereMesh_.draw();
    }

    // Phase 13b: one combined "N/M culled" line, logged periodically (not
    // every frame -- see kCullLogFrameInterval) across every drawable tested
    // above (entities_'s Model nodes, the ground plane, every PBR sphere
    // instance), so a headless run can confirm culling is actually skipping
    // draw calls -- e.g. ENGINE_FRUSTUM_CULL_DEMO should show most/all
    // drawables culled every logged frame, while a normal run (the whole
    // small test scene fits in view) should show zero or close to it.
    if (frameCount_ % kCullLogFrameInterval == 0) {
        LOG_INFO("Frustum culling: " + std::to_string(cullStats.culledDrawables) + "/" +
                  std::to_string(cullStats.totalDrawables) + " drawables culled this frame");
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

    // MSAA HDR framebuffer bug fix: hdrFramebuffer_'s color attachment is
    // now multisample (see application.hpp/framebuffer.hpp's own MSAA
    // bug-fix comments), so it can no longer be sampled directly by
    // anything below (bloom extraction, the final tonemap pass) the way a
    // plain sampler2D reads a single-sample texture. Resolve it into
    // hdrResolveFramebuffer_ -- a same-size, single-sample sibling -- via
    // one glBlitFramebuffer right here, once per frame, immediately after
    // the scene+skybox color pass above finishes and before anything reads
    // this frame's HDR color. Everything from here on reads
    // hdrResolveFramebuffer_ instead of hdrFramebuffer_ directly.
    hdrFramebuffer_.resolveTo(hdrResolveFramebuffer_);

    // Phase 11: bloom -- entirely screen-space passes against
    // hdrResolveFramebuffer_'s now-finished (and now-resolved) HDR color
    // buffer (scene + skybox), before that buffer is resolved to the window
    // below. See this class's Phase 11 header comment for the overall shape
    // (bright-pass extract -> ping-ponged separable blur -> additive
    // composite in the final resolve pass).
    {
        // Bright-pass extraction: hdrResolveFramebuffer_ (full res) ->
        // brightFramebuffer_ (half res -- see kBloomDownsampleFactor).
        // Downsampling and thresholding happen in the same draw call for
        // free: this pass's own (smaller) target resolution decides how
        // many texels get sampled, and Framebuffer's GL_LINEAR minification
        // filter does the actual averaging as hdrResolveFramebuffer_'s
        // full-res texture is sampled down into it.
        brightFramebuffer_.bindForWriting();
        GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
        bloomExtractShader_->use();
        hdrResolveFramebuffer_.bindColorTexture(0);
        bloomExtractShader_->setInt("uHdrBuffer", 0);
        bloomExtractShader_->setFloat("uThreshold", kBloomThreshold);
        postProcessQuad_.bind();
        postProcessQuad_.draw();

        // Separable Gaussian blur, ping-ponged between pingpongFramebuffer0_/
        // 1_ -- see kBloomBlurPasses' comment above for the exact pass
        // count/parity this loop depends on. The first iteration's source is
        // brightFramebuffer_ itself; every iteration after reads whichever
        // ping-pong target is NOT this iteration's write target (i.e.
        // whichever one the previous iteration just finished writing) --
        // exactly the standard ping-pong pattern, since a texture can't be
        // simultaneously bound for reading and drawn into.
        const glm::vec2 blurTexelSize(1.0f / static_cast<float>(pingpongFramebuffer0_.width()),
                                        1.0f / static_cast<float>(pingpongFramebuffer0_.height()));
        blurShader_->use();
        blurShader_->setInt("uImage", 0);
        blurShader_->setVec2("uTexelSize", blurTexelSize);

        bool horizontal = true;
        for (int i = 0; i < kBloomBlurPasses; ++i) {
            Framebuffer& target = horizontal ? pingpongFramebuffer0_ : pingpongFramebuffer1_;
            target.bindForWriting();
            GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
            blurShader_->setInt("uHorizontal", horizontal ? 1 : 0);
            if (i == 0) {
                brightFramebuffer_.bindColorTexture(0);
            } else {
                Framebuffer& source = horizontal ? pingpongFramebuffer1_ : pingpongFramebuffer0_;
                source.bindColorTexture(0);
            }
            postProcessQuad_.bind();
            postProcessQuad_.draw();
            horizontal = !horizontal;
        }
        // kBloomBlurPasses is asserted even (see its own comment), so the
        // loop's last write is always to pingpongFramebuffer1_ -- that's
        // where the final blurred bloom texture the resolve pass below
        // reads now lives.
    }

    // Phase 7b: resolve hdrResolveFramebuffer_'s HDR color buffer to the
    // window's real (default) framebuffer via one fullscreen tonemap +
    // gamma-correct pass -- see assets/shaders/postprocess.vert/.frag. Both
    // buffers are cleared first (color: nothing else draws here so any
    // prior frame's leftover pixels must go; depth: this pass's own
    // fullscreen quad is depth-tested against whatever the default
    // framebuffer's depth buffer last held, which is otherwise
    // stale/unrelated to this frame) so this draw can't be silently
    // rejected by a leftover depth value from a previous frame.
    //
    // MSAA HDR framebuffer bug fix: reads hdrResolveFramebuffer_ (this
    // frame's already-resolved, single-sample HDR color) rather than
    // hdrFramebuffer_ directly -- see the resolveTo() call above.
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    GL_CHECK(glViewport(0, 0, fbWidth, fbHeight));
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    postProcessShader_->use();
    hdrResolveFramebuffer_.bindColorTexture(0);
    postProcessShader_->setInt("uHdrBuffer", 0);
    // Phase 11: the bloom pipeline's final blurred output (see the bloom
    // block above) -- additively blended with uHdrBuffer before Reinhard
    // tonemapping in postprocess.frag, at a fixed texture unit (1) that
    // doesn't collide with uHdrBuffer's own (0) within this one draw call.
    pingpongFramebuffer1_.bindColorTexture(1);
    postProcessShader_->setInt("uBloomBuffer", 1);
    postProcessShader_->setFloat("uBloomStrength", kBloomStrength);
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
