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
// Phase 10: iblProbe_'s three precomputed maps, bound once per frame onto
// pbrShader_ (see render()) at fixed texture units that don't collide with
// any PBRMaterial::bind() call's own units (0 = albedo map, 1 = normal map --
// see pbr_material.hpp) or kShadowMapTextureUnit above.
constexpr unsigned int kIrradianceMapTextureUnit = 3;
constexpr unsigned int kPrefilterMapTextureUnit = 4;
constexpr unsigned int kBrdfLutTextureUnit = 5;

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
      pbrShader_(resources_.getShader(kPBRVertexShaderPath, kPBRFragmentShaderPath)),
      irradianceShader_(resources_.getShader(kCubemapCaptureVertexShaderPath, kIrradianceFragmentShaderPath)),
      prefilterShader_(resources_.getShader(kCubemapCaptureVertexShaderPath, kPrefilterFragmentShaderPath)),
      brdfShader_(resources_.getShader(kPostProcessVertexShaderPath, kBrdfLutFragmentShaderPath)),
      shadowMap_(kShadowMapWidth, kShadowMapHeight),
      // Phase 7b: sized from window_'s own real framebuffer size (already
      // constructed at this point -- see this header's declaration-order
      // comment) rather than the constructor's width/height parameters
      // directly, so this stays correct even on a HiDPI display where the
      // framebuffer is larger than the window's requested size.
      hdrFramebuffer_(window_.getSize().first, window_.getSize().second),
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

        SphereInstance instance{Transform{}, PBRMaterial(*pbrShader_, kSphereAlbedo, metallic, kMetallicRowRoughness,
                                                           kSphereAO)};
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

        SphereInstance instance{Transform{}, PBRMaterial(*pbrShader_, kSphereAlbedo, kRoughnessRowMetallic,
                                                           roughness, kSphereAO)};
        glm::vec3 position = gridCenter + gridRight * colOffset;
        position.y = kSphereGridHeight - kSphereRowSeparation * 0.5f;
        instance.transform.setPosition(position);
        sphereInstances_.push_back(std::move(instance));
    }

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

    // Phase 9: the PBR sphere grid casts/receives shadows through this same
    // depth-only pass -- shadow.vert reads only aPos (see that file), so it
    // doesn't matter that these spheres' color pass uses pbrShader_ rather
    // than shader_. sphereMesh_ is bound once and drawn once per instance,
    // re-uploading only uModel between draws (each instance shares the same
    // geometry).
    sphereMesh_.bind();
    for (const SphereInstance& instance : sphereInstances_) {
        shadowShader_->setMat4("uModel", instance.transform.getModelMatrix());
        sphereMesh_.draw();
    }

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
    pbrShader_->setMat4("uLightSpaceMatrix", lightSpaceMatrix);
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

    // shadowMap_ is still bound for reading on kShadowMapTextureUnit from
    // the upload above (binding a texture unit is global GL state, not
    // per-program) -- only the sampler uniform needs re-pointing at that
    // same unit on this different program.
    pbrShader_->setInt("uShadowMap", static_cast<int>(kShadowMapTextureUnit));
    pbrShader_->setVec2("uShadowMapTexelSize",
                         glm::vec2(1.0f / static_cast<float>(shadowMap_.width()),
                                   1.0f / static_cast<float>(shadowMap_.height())));

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
        const glm::mat3 sphereNormalMatrix = glm::inverseTranspose(glm::mat3(sphereModel));
        pbrShader_->setMat4("uModel", sphereModel);
        pbrShader_->setMat3("uNormalMatrix", sphereNormalMatrix);
        instance.material.bind();
        sphereMesh_.draw();
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
