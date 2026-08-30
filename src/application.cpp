#include "engine/application.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>

#include "engine/asset_drop.hpp"
#include "engine/camera_capture.hpp"
#include "engine/camera_component.hpp"
#include "engine/frustum.hpp"
#include "engine/gizmo.hpp"
#include "engine/gl_debug.hpp"
#include "engine/hdri_loader.hpp"
#include "engine/light.hpp"
#include "engine/log.hpp"
#include "engine/material_override.hpp"
#include "engine/model.hpp"
#include "engine/paths.hpp"
#include "engine/physics.hpp"
#include "engine/scene_file_ops.hpp"
#include "engine/scene_serialization.hpp"
#include "engine/transform_hierarchy.hpp"

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
// Phase 13f: SSAO's three passes -- gbufferShader_ pairs its own dedicated
// vertex stage (gbuffer.vert, since it needs a view-space normal varying
// none of this engine's other vertex shaders compute) with gbuffer.frag;
// ssaoShader_/ssaoBlurShader_ both pair the existing fullscreen-quad
// postprocess.vert with their own fragment shaders, like every other
// screen-space pass in this engine.
const std::string kGBufferVertexShaderPath = resolveAssetPath("assets/shaders/gbuffer.vert");
const std::string kGBufferFragmentShaderPath = resolveAssetPath("assets/shaders/gbuffer.frag");
const std::string kSSAOFragmentShaderPath = resolveAssetPath("assets/shaders/ssao.frag");
const std::string kSSAOBlurFragmentShaderPath = resolveAssetPath("assets/shaders/ssao_blur.frag");
// Phase 18d: the selection mask pass's own program -- a dedicated, minimal
// vertex stage (selection_mask.vert, uModel/uView/uProjection only, mirroring
// shadow.vert's own "just enough to place the geometry" shape) paired with a
// fragment stage that discards against SSAO's own already-computed
// ssaoGBuffer_ depth texture (see selection_mask.frag's own comment) rather
// than reusing gbuffer.vert/.frag (which computes a view-space normal this
// pass has no use for) or shadow.vert/.frag (whose empty fragment stage
// writes nothing at all -- this pass needs a real color output, the mask
// itself).
const std::string kSelectionMaskVertexShaderPath = resolveAssetPath("assets/shaders/selection_mask.vert");
const std::string kSelectionMaskFragmentShaderPath = resolveAssetPath("assets/shaders/selection_mask.frag");
// Phase 18e: the translate gizmo's own program -- see gizmo.vert/.frag's own
// header comments and renderGizmo()'s own application.hpp comment.
const std::string kGizmoVertexShaderPath = resolveAssetPath("assets/shaders/gizmo.vert");
const std::string kGizmoFragmentShaderPath = resolveAssetPath("assets/shaders/gizmo.frag");
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
// Phase 13e: the equirectangular-HDRI-to-cubemap conversion pass -- reuses
// cubemap_capture.vert (same fixed-per-face-view vertex stage Phase 10's own
// irradiance/prefilter passes share) paired with a new fragment shader.
const std::string kEquirectToCubemapFragmentShaderPath =
    resolveAssetPath("assets/shaders/equirect_to_cubemap.frag");
// Phase 13e: the real HDRI environment map (see tools/generate_hdri.py for
// how it was generated) that replaces the old 6-PNG procedural skybox as
// skybox_'s own source by default -- see buildSkybox() below.
const std::string kHdriPath = resolveAssetPath("assets/textures/hdri/sky.hdr");
// Per-face resolution of the HDRI-converted cubemap (see
// loadHdrEquirectangularAsCubemap()'s own header comment for why 512 is
// enough) -- a background/IBL source, never a surface the camera gets close
// enough to see individual texels of.
constexpr int kHdriCubemapFaceSize = 512;
// Phase 13d: clustered lighting's two compute passes (see
// cluster_light_culler.hpp/ClusterLightCuller).
const std::string kClusterAABBComputeShaderPath = resolveAssetPath("assets/shaders/cluster_aabb.comp");
const std::string kClusterCullComputeShaderPath = resolveAssetPath("assets/shaders/cluster_cull.comp");
// Phase 5's hand-authored test scene: three separate objects (a pyramid, a
// table, and a small box sitting on top of the table) at different
// positions, proving Model's node hierarchy + transform composition places
// more than one mesh correctly -- see assets/models/scene.obj and
// model.cpp. Phase 8b: only still used by the ENGINE_LEGACY_SCENE fallback
// path below -- the default path loads this same model by way of
// assets/scenes/default.json's own "model"."path" field instead of this
// constant.
const std::string kScenePath = resolveAssetPath("assets/models/scene.obj");

// Phase 8b: the scene file loadScene() reads by default (see
// scene_serialization.hpp) -- checked into assets/ like every other asset,
// describing the exact same one entity (kScenePath's model, rotated 12
// degrees around Y) the old hardcoded construction below built directly in
// C++. See this class's own Phase 8b constructor comment for the
// ENGINE_LEGACY_SCENE escape hatch back to that hardcoded path.
const std::string kDefaultScenePath = resolveAssetPath("assets/scenes/default.json");

// Phase 18i: where newScene()'s currentScenePath_ points until the user
// actually saves it somewhere -- see that member's own application.hpp
// comment for why this is a plain std::string sentinel path (not a
// std::optional<std::string>) and currentScenePath_'s own comment for why
// this specific choice was made. Never read/written by anything at startup
// -- it only ever becomes currentScenePath_'s value via newScene(), and this
// engine never creates a file at this path on its own (a plain Save Scene
// after New Scene is what would first write it, exactly like saving a
// brand-new, never-yet-saved document in any other editor).
const std::string kUntitledScenePath = resolveAssetPath("assets/scenes/untitled.json");

// Phase 14f: the three mesh assets the Scene panel's Create menu's
// "Cube"/"Sphere"/"Plane" items load. "Cube" deliberately REUSES
// falling_cube.obj (Phase 8e's own physics-demo asset) rather than a new,
// separate cube asset -- one box mesh is enough, and this keeps the Create
// menu consistent with the one primitive this project already has checked
// in. "Sphere"/"Plane" are two new, checked-in assets generated by
// tools/generate_primitive_meshes.py (see that script's own header comment
// for why a script instead of hand-typed OBJ text, and for the exact
// generation command) -- there is no procedural-Mesh-to-Model pathway in
// this engine (see model.hpp's own Phase 14f comment), so these load through
// the ordinary resources_.getModel() cache exactly like every other asset,
// the smallest, most consistent-with-existing-architecture way to add two
// more primitives.
const std::string kCreateCubeModelPath = resolveAssetPath("assets/models/falling_cube.obj");
const std::string kCreateSphereModelPath = resolveAssetPath("assets/models/sphere.obj");
const std::string kCreatePlaneModelPath = resolveAssetPath("assets/models/plane.obj");

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
// Phase 13f: ssaoBlurred_'s own fixed texture unit, bound once per frame
// onto both shader_ and pbrShader_ (see render()) -- the next free unit
// after the three IBL maps above and the (up to) three shadow-cascade units
// (kShadowMapTextureUnitBase..+2, i.e. 3/4/5).
constexpr unsigned int kSSAOMapTextureUnit = 9;
// Phase 13g: SSR's own two fixed texture units -- the next free ones after
// SSAO's blurred map above. Both are only ever bound while pbrShader_
// redraws the sphere grid a second time in the SSR compositing pass (see
// Application::renderSSRComposite()); uSSRColorBuffer samples
// hdrResolveFramebuffer_ (this frame's already fully-shaded opaque scene,
// resolved once already before this pass runs) and uSSRDepthMap reuses
// SSAO's own ssaoGBuffer_ depth texture (see that pass's comment above and
// pbr.frag's own Phase 13g comment for why SSAO's existing G-buffer is
// exactly the input SSR needs too, with no second redundant geometry
// pre-pass).
constexpr unsigned int kSSRColorBufferTextureUnit = 10;
constexpr unsigned int kSSRDepthMapTextureUnit = 11;

// Phase 18d: the selection mask pass's own depth-discard tolerance -- see
// selection_mask.frag's own uDepthBias comment for exactly what this
// absorbs (float rounding between two independent rasterizations of the
// same triangles, this pass's own vs. ssaoGBuffer_'s gbuffer.vert/.frag).
// Same non-linear [0,1] depth-buffer units as kSSAOBias above; a similar
// small magnitude for a similar reason (both exist purely to swallow
// precision noise between two passes that are supposed to agree, not to
// meaningfully change which fragments pass), picked empirically the same
// way -- large enough to never visibly let a selected object's own surface
// wrongly self-occlude at grazing angles, small enough to never visibly
// let a REAL occluder's silhouette bleed the outline through it (see this
// phase's own README section for the headless screenshot that confirms
// both).
constexpr float kSelectionMaskDepthBias = 0.0015f;
// Phase 18d: the selection outline's fixed color -- the SAME teal Phase
// 17a's own editor theme already uses for a selected Scene-panel row/active
// toolbar button (editor_ui.cpp's kAccentTeal, #2DC3B2 / (0.176, 0.765,
// 0.698)), for the visual consistency this phase's own brief calls out.
// Hand-copied here rather than shared through a common header: editor_ui.cpp
// keeps kAccentTeal `constexpr` and file-local (inside applyEditorTheme(),
// see that function's own comment) since nothing outside that one function
// previously needed it -- introducing a shared theme-constants header for
// this one cross-file value would be a disproportionate amount of new
// plumbing for a single glm::vec3, so this constant is kept in sync by hand
// instead, the same "kept in sync by hand across a boundary" situation this
// engine already accepts for SSAOKernel's own SSAO_KERNEL_SIZE (see
// ssao.hpp's own comment).
constexpr glm::vec3 kSelectionOutlineColor{0.176f, 0.765f, 0.698f};

// Phase 18e: the translate gizmo's three axis colors -- the standard,
// near-universal DCC convention (Blender/Unity/Unreal all agree on this
// exact X=red/Y=green/Z=blue mapping), deliberately NOT this project's own
// teal editor accent (kSelectionOutlineColor just above): a manipulation
// tool's whole job is "which axis am I about to drag," and three
// maximally-distinguishable, colorblind-conventional colors serve that far
// better than a single theme-matched accent would (which the selection
// outline above -- a passive highlight, not something a user aims a click
// at -- has no equivalent need to distinguish between multiple simultaneous
// targets). Slightly desaturated/darkened from pure (1,0,0)/(0,1,0)/(0,0,1)
// so each also reads clearly against this engine's own light-gray ground
// plane and dark viewport background, not just against a neutral gray test
// swatch.
constexpr glm::vec3 kGizmoAxisColorX{0.85f, 0.18f, 0.18f};
constexpr glm::vec3 kGizmoAxisColorY{0.25f, 0.78f, 0.25f};
constexpr glm::vec3 kGizmoAxisColorZ{0.20f, 0.45f, 0.95f};

// Phase 13f: SSAO tuning constants (see ssao.hpp/ssao.cpp and
// assets/shaders/ssao.frag/ssao_blur.frag).
//
// kSSAOKernelSize: hemisphere sample count -- 32 sits at the low end of this
// technique's usual 32-64 range (LearnOpenGL's own reference implementation
// uses 64), chosen here because this scene's raw kernel-sampled noise is
// smoothed by the blur pass immediately afterward anyway (see
// kSSAONoiseDim below) and this is a small hand-authored scene running on a
// software rasterizer under headless verification, not a AAA production
// title -- doubling the sample count would cost roughly twice this pass's
// runtime for a subtlety difference this scene's own screenshot-level
// verification wouldn't meaningfully show.
constexpr int kSSAOKernelSize = 32;
// kSSAODownsampleFactor: ssaoGBuffer_/ssaoRaw_/ssaoBlurred_ are all sized at
// the window's real framebuffer resolution divided by this -- the same
// "soft/low-frequency effect, so downsample it" tradeoff bloom's own
// kBloomDownsampleFactor already makes (see that constant's comment), just
// applied here because this technique's per-pixel cost (kSSAOKernelSize
// texture fetches/pixel in the kernel pass alone) is steep enough on this
// project's software-rasterizer (llvmpipe) headless verification target to
// matter for wall-clock run time, not because SSAO is conceptually as soft
// as bloom's own glow -- a half-res AO buffer bilinearly upsampled back onto
// full-res geometry (Framebuffer's own GL_LINEAR minification/magnification,
// see framebuffer.cpp) reads as indistinguishable from full-res at this
// scene's contact-crease scale, confirmed by the screenshot/pixel-sample
// verification this phase's own review requires.
constexpr int kSSAODownsampleFactor = 2;

// Phase 14c bug-review fix: shared by the constructor's own initializer list
// (below) and resizeViewportTargetsIfNeeded() (this file, further down) for
// computing bloom's/SSAO's downsampled target dimensions from whatever the
// viewport's own current width/height happen to be. Plain integer division
// alone (`dimension / factor`) floors to 0 whenever `dimension < factor` --
// concretely, a 1-pixel-tall viewport divided by kBloomDownsampleFactor (2)
// -- and a 0-sized Framebuffer is invalid GL (glCheckFramebufferStatus never
// reports GL_FRAMEBUFFER_COMPLETE for one, so Framebuffer's own constructor
// throws). resizeViewportTargetsIfNeeded() already guarded its own copy of
// this division with std::max(1, ...); the constructor's initializer list
// below used to divide viewportWidth_/viewportHeight_ directly, unguarded,
// on the (incorrect -- see this project's own Phase 14c bug-review) theory
// that "this engine has never run at a window size small enough for this to
// reach 0" -- ENGINE_WINDOW_WIDTH=1 ENGINE_WINDOW_HEIGHT=1 (a value
// main.cpp's own windowDimensionFromEnv() validation accepts: it only
// rejects <= 0) reaches exactly that constructor-time 0x0 Framebuffer and
// crashes the whole process on startup with "Framebuffer: HDR framebuffer
// incomplete" before a single frame ever renders -- confirmed by actually
// running the app at that window size. Both call sites now share this one
// clamped helper instead of each hand-rolling (or, as the constructor did,
// omitting) the same std::max(1, ...) floor.
int clampedDownsampleDimension(int dimension, int factor) {
    return std::max(1, dimension / factor);
}

// kSSAONoiseDim: the tileable rotation-noise texture's width/height (a
// square NxN texture) -- 4 is the standard size this technique's reference
// implementations use, small enough to tile many times across an
// 800x600-class window (a bigger noise texture would mean visibly fewer,
// more widely-spaced independent per-pixel rotations) while still being
// large enough that ssao_blur.frag's own NxN box blur (radius
// kSSAONoiseDim / 2, see render()) meaningfully smooths the noise this
// texture's own periodicity introduces.
constexpr int kSSAONoiseDim = 4;
// kSSAORadius/kSSAOBias: view-space units -- this engine's own hand-
// authored scene sits at roughly a 1-world-unit scale (the PBR sphere grid's
// own radius is 0.14, see kSphereRadius; the ground plane's half-extent is
// 2.6, see kGroundHalfExtent), noticeably smaller than the "meters, human-
// scale rooms" scene LearnOpenGL's own reference SSAO radius (0.5) assumes,
// so both constants are tuned down from that reference to avoid the classic
// "radius too large for the scene" failure mode: a giant sampling
// hemisphere relative to the sphere grid's own small radius would sample
// well past each sphere's own silhouette into open air/the ground plane
// behind it, producing a visible dark halo around every sphere instead of
// contact-only occlusion. 0.35 keeps the hemisphere comparable to (a bit
// larger than) the sphere grid's own radius -- enough to reach the
// sphere-to-ground contact crease and the box-on-table contact area without
// reaching all the way past an object's own silhouette into unrelated
// background geometry. kSSAOBias (a small constant offset in the occlusion
// depth comparison, see ssao.frag) is the standard self-acne guard: without
// it, a perfectly flat surface's own depth-buffer quantization noise can
// register as self-occlusion (every kernel sample landing "behind" the very
// surface it started on), producing a visible fine speckle pattern across
// flat surfaces that have no actual nearby occluder at all.
constexpr float kSSAORadius = 0.35f;
constexpr float kSSAOBias = 0.015f;

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
// Phase 15a: PointLightData used to be defined here, private to this file.
// It's now light.hpp's own public PointLightSample -- field-for-field
// identical -- since collectPointLights() (light.cpp) needs a type it can
// return that this file also consumes, and duplicating an identical struct
// in two places just to keep one of them "private" wasn't worth it. See
// light.hpp's own comment for the full rationale.

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
constexpr std::array<PointLightSample, 3> kPointLights = {{
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

// Phase 15a: MAX_POINT_LIGHTS in basic.frag/pbr.frag, promoted from a bare
// literal (kept only in the static_assert below, pre-Phase-15) to a named
// constant now that render() also passes it to collectPointLights() as the
// live budget kPointLights' 3 fixed entries share with however many
// ECS PointLight entities exist -- see this file's own Phase 15a render()
// comment. Still kept in sync BY HAND with the shaders' own #define (no
// shared constant across the GLSL/C++ boundary exists), same as
// kMaxSpotLights below.
constexpr std::size_t kMaxPointLights = 8;
constexpr std::size_t kMaxSpotLights = 4;
static_assert(kPointLights.size() <= kMaxPointLights, "kPointLights exceeds MAX_POINT_LIGHTS in basic.frag");
static_assert(kSpotLights.size() <= kMaxSpotLights, "kSpotLights exceeds MAX_SPOT_LIGHTS in basic.frag");

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

// Phase 14f: where a newly Create'd entity (spawnEntityFromCreateMenu(),
// below) is placed -- a fixed distance directly in front of camera_'s
// current position/facing direction (camera_.position() + camera_.front() *
// kCreateEntityDistanceFromCamera) is this phase's own chosen heuristic
// (see the phase brief's own "your call" on this): simple, and it puts a
// freshly created object where the user is already looking, rather than at
// a fixed world-space point that could be off-screen or behind them
// depending on where the camera happens to be. kCreateEntityMinHeight then
// floors the RESULT's own Y so an object never spawns partially (or
// entirely) buried in the ground plane (kGroundY, just above) when the
// camera happens to be pointed downward -- 0.4 is comfortably above every
// Create'd primitive's own half-extent-ish size (falling_cube.obj's 0.25,
// sphere.obj's 0.35 radius, plane.obj's flat 0.5 half-extent) so the whole
// object clears the ground, not just its own local origin.
constexpr float kCreateEntityDistanceFromCamera = 3.0f;
constexpr float kCreateEntityMinHeight = 0.4f;

// Phase 8e: the largest deltaTime a single stepPhysics() call is allowed to
// integrate with (see update()'s own definition below and physics.hpp for
// stepPhysics() itself), regardless of how long this frame's real render
// work actually took. 1/60 s -- an ordinary 60 FPS frame budget -- is the
// standard "fixed max physics timestep" choice: it keeps the simulation's
// own behavior reproducible across machines of very different real speed,
// rather than a slower machine's larger per-frame deltaTime literally
// making gravity accelerate objects faster in wall-clock terms. It matters
// concretely on THIS project's own headless verification path in
// particular -- tools/run_headless.sh's own comment on a Debug build's
// per-frame cost (CSM's 3 depth passes, clustered lighting's compute
// dispatch, SSAO's 3 screen-space passes, bloom, SSR, all under Mesa's
// llvmpipe software rasterizer) measures roughly 0.2-0.3s of *real* elapsed
// time per rendered frame -- without this clamp, a single frame's raw
// deltaTime would already be large enough to integrate this phase's own
// falling demo entity (assets/scenes/default.json's "falling_cube", loaded
// via loadScene() below) straight past the ground and settle it within the
// first frame or two, leaving nothing for a multi-frame headless screenshot
// sequence (ENGINE_MAX_FRAMES=5/30/90) to actually show falling at
// different heights. Camera movement
// (camera_.processMovement(), below) deliberately keeps using the
// unclamped, real deltaTime -- free-fly movement speed staying tied to
// actual wall-clock time is the correct, expected behavior there; only
// physics integration has a reason to decouple from it.
constexpr float kMaxPhysicsTimestep = 1.0f / 60.0f;

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
void uploadPointLight(Shader& shader, std::size_t index, const PointLightSample& light) {
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

// Phase 13d: same getenv-gated-behavior pattern -- see clusterDebugMode_'s
// own comment in application.hpp for what this flag does.
bool clusterDebugModeFromEnv() {
    const char* value = std::getenv("ENGINE_CLUSTER_DEBUG");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

// Phase 13f: same getenv-gated-behavior pattern as every env var above --
// see ssaoDisabled_'s own application.hpp comment for what this flag does.
bool ssaoDisabledFromEnv() {
    const char* value = std::getenv("ENGINE_SSAO_DISABLE");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

// Phase 13f: same getenv-gated-behavior pattern -- see ssaoDebugMode_'s own
// application.hpp comment for what this flag does.
bool ssaoDebugModeFromEnv() {
    const char* value = std::getenv("ENGINE_SSAO_DEBUG");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

// Phase 13g: same getenv-gated-behavior pattern as every env var above --
// see ssrDisabled_'s own application.hpp comment for what this flag does.
bool ssrDisabledFromEnv() {
    const char* value = std::getenv("ENGINE_SSR_DISABLE");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

// Phase 16: ENGINE_DEBUG_FORCE_CAMERA_CAPTURE=1, unset by default -- same
// getenv-gated-behavior pattern as every plain on/off flag above. Unlike
// cameraDemoMode_/frustumCullDemoMode_/etc. (each read every frame from
// update()), this is consulted exactly once, in the constructor, and fed
// straight into setCameraCaptured() -- see this class's own Phase 16
// constructor comment for why. Named/documented as this project's
// established "debug env var calls the exact same production function a
// real interaction would" precedent (ENGINE_DEBUG_CREATE/
// spawnEntityFromCreateMenu(), ENGINE_DEBUG_SAVE_SCENE/saveCurrentScene(),
// ...), the closest a headless run with no real pointer device can get to
// proving a Viewport double-click's own resulting state transition: it
// can't reproduce the double-click GESTURE itself (Xvfb has no real mouse to
// click with -- see this class's own Phase 16 README section for exactly
// what this can and can't verify as a result), but starting already
// captured exercises the EXACT SAME setCameraCaptured() call, cursor-mode
// GLFW call, and per-frame processMovement()/processMouseInput() gating a
// real double-click would trigger, plus lets Escape's own precedence logic
// (decideCameraCapture(), camera_capture.hpp) be exercised end-to-end
// against a real captured state instead of only unit-tested in isolation.
bool debugForceCameraCaptureFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_FORCE_CAMERA_CAPTURE");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

// Phase 18b: ENGINE_DEBUG_FORCE_PLAY_MODE=1, unset by default -- same
// getenv-gated-behavior pattern as every plain on/off flag above, and the
// SAME "debug env var calls the exact same production function a real
// interaction would" precedent ENGINE_DEBUG_FORCE_CAMERA_CAPTURE's own
// comment above already names: Xvfb has no real pointer device, so there is
// no way for a real Play-button CLICK to ever reach
// editorUI_.renderDockspaceShell()'s own toolbar under headless
// verification. This closes that gap for physicsRunning_ the same way
// ENGINE_DEBUG_FORCE_CAMERA_CAPTURE already closes it for cameraCaptured_ --
// applied once, in the constructor (see that call site's own comment for
// why the constructor body, not this in-class default, is where it's
// applied), setting the exact same physicsRunning_ member a real Play click
// would, so update()'s own `if (physicsRunning_) { stepPhysics(...); }`
// gate runs the identical production code path either way -- the only thing
// this env var stands in for is the click gesture itself, not any part of
// what happens after it.
bool debugForcePlayModeFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_FORCE_PLAY_MODE");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

// Phase 18g: ENGINE_DEBUG_SHADING_MODE=<rendered|solid|wireframe>
// (case-insensitive), unset by default -- same getenv-gated-value shape as
// ENGINE_DEBUG_SELECT/ENGINE_DEBUG_CREATE above, but for editShadingMode_
// instead. Closes the identical "no real mouse to click a toolbar button
// with" gap ENGINE_DEBUG_FORCE_PLAY_MODE closes for physicsRunning_ just
// above -- applied once, in the constructor, setting the exact same
// editShadingMode_ member a real "lighting"/"texture-mode" toolbar click
// would (via decideNextEditShadingMode(), shading_mode.hpp), so render()'s
// own effectiveShadingMode() gate runs the identical production code path
// either way. An unrecognized value is logged as a warning and ignored
// (stays at the ShadingMode::kRendered default), the same "don't silently
// misbehave on a typo'd env var" treatment every other value-carrying
// ENGINE_DEBUG_* var in this file already gets.
ShadingMode debugShadingModeFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_SHADING_MODE");
    if (value == nullptr || *value == '\0') {
        return ShadingMode::kRendered;
    }
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lowered == "rendered") {
        return ShadingMode::kRendered;
    }
    if (lowered == "solid") {
        return ShadingMode::kSolid;
    }
    if (lowered == "wireframe") {
        return ShadingMode::kWireframe;
    }
    LOG_WARN("ENGINE_DEBUG_SHADING_MODE=\"" + lowered + "\" is not one of rendered/solid/wireframe; ignored");
    return ShadingMode::kRendered;
}

// Phase 16: ENGINE_DEBUG_SIMULATE_ESCAPE=1, unset by default -- same
// getenv-gated-behavior pattern as every plain on/off flag above. Xvfb has
// no real keyboard at all (see this project's own established headless-
// verification limits, e.g. cameraDemoMode_'s own comment), so there is no
// way for a real physical Escape press to ever reach window_.isKeyPressed()
// under the verification harness -- meaning Escape's own NEW two-meanings
// precedence (decideCameraCapture(), camera_capture.hpp) would otherwise be
// provable only by tests/camera_capture_test.cpp's own isolated unit tests,
// never by the real running app's own run()/setCameraCaptured() wiring
// actually executing that branch. This closes that gap the same way
// ENGINE_DEBUG_FORCE_CAMERA_CAPTURE closes the analogous one for ENTERING
// capture: run() (see its own Phase 16 comment) simulates a synthetic
// Escape press HELD DOWN across kDebugSimulateEscapeHoldFrames consecutive
// polls, starting at kDebugSimulateEscapeFrame -- via pollInputState()'s own
// `forceEscapeDown` parameter (input.hpp), which enters at the same
// physical-key-query layer a real GLFW event would, driving the exact same
// InputActionMap::update()/justPressed() edge-detection and
// decideCameraCapture()/setCameraCaptured() production calls a real,
// held-for-many-frames Escape press would go through -- not a parallel,
// hand-rolled "pretend this happened" path.
//
// Post-review bug fix: originally injected a synthetic press for exactly
// ONE frame, which -- by construction -- behaves like an edge-triggered
// signal and therefore could never have caught the exact bug this var
// exists to guard against (see kDebugSimulateEscapeHoldFrames' own comment
// for the full incident writeup): a real physical key-press spans MANY
// consecutive polls, not one, and the original code fed
// decideCameraCapture() a LEVEL-triggered signal
// (`InputState::escapePressed`) that stayed true for every one of those
// polls -- correctly exiting capture on the first, then incorrectly quitting
// the app on the very next one, since a still-held key was indistinguishable
// from a brand-new press once capture had already turned off. A one-frame
// synthetic press can never reproduce "the key is still down on frame N+1"
// at all, so this exact regression shipped undetected. Now genuinely holds
// the synthetic press across multiple polls, exercising the real
// `InputActionMap::justPressed()` edge-detection this phase's fix now
// depends on, not just decideCameraCapture()'s own already-correct pure
// logic in isolation.
//
// Combined with ENGINE_DEBUG_FORCE_CAMERA_CAPTURE=1 in the same run, this
// proves the full "captured -> Escape held for several frames -> uncaptured
// on the first frame, then does NOT quit on any of the remaining held
// frames" sequence end-to-end in the real app; alone (not captured), it
// proves Escape's ORIGINAL "quit" meaning still fires exactly as it always
// has, on the very first held frame -- see README.md's own Phase 16 Verify
// section for both actual runs.
bool debugSimulateEscapeFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_SIMULATE_ESCAPE");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

// Phase 13e: same getenv-gated-behavior pattern as every env var above --
// true keeps the Phase 7b/10 procedural 6-face skybox instead of the new
// HDRI, so that path stays reachable/verifiable rather than only living on
// in git history (see this project's established "keep the old path as
// reference" convention, e.g. Material/basic.frag alongside
// PBRMaterial/pbr.frag since Phase 9).
bool proceduralSkyboxFromEnv() {
    const char* value = std::getenv("ENGINE_USE_PROCEDURAL_SKYBOX");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

// Phase 8b: same getenv-gated-behavior pattern as every env var above --
// true keeps the pre-Phase-8b hardcoded registry_.create()/addComponent<T>
// scene construction instead of loadScene()'ing kDefaultScenePath, so that
// path stays reachable/verifiable as a documented escape hatch rather than
// only living on in git history (the same "keep the old path as reference"
// convention proceduralSkyboxFromEnv()'s own comment above cites for
// ENGINE_USE_PROCEDURAL_SKYBOX) -- e.g. useful for isolating whether a
// rendering regression is scene-data-loading-related without needing to
// touch/revert assets/scenes/default.json itself.
//
// Deliberately still builds only Phase 8b's original single scene.obj
// entity -- it was never updated to also add Phase 8e's "falling_cube"
// RigidBody/Collider entity, since doing so would mean hardcoding in C++
// exactly the entity data assets/scenes/default.json already carries,
// defeating this flag's own "isolate scene-LOADING problems, not scene
// CONTENT problems" purpose. So this path still exercises input handling
// and the debug overlay identically to the default path (neither is
// scene-content-dependent), but stepPhysics() runs over zero RigidBody
// entities under it -- a real, harmless narrowing of what this escape hatch
// demonstrates, not a bug: nothing about Phase 8c/8d's own wiring lives in
// scene content, only Phase 8e's one demo entity does.
bool legacySceneFromEnv() {
    const char* value = std::getenv("ENGINE_LEGACY_SCENE");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

// Phase 8c: same getenv-gated-behavior pattern as every env var above --
// unset by default (debug UI off), so the default headless run's rendered
// screenshot stays pixel-identical to every prior phase's own (see
// DebugUI's own header comment on why "off" means debugUI_ never calls a
// single ImGui function, not just an invisible window).
bool showDebugUIFromEnv() {
    const char* value = std::getenv("ENGINE_SHOW_DEBUG_UI");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

// Phase 14d: ENGINE_DEBUG_SELECT=<entity name>, unset by default -- a debug/
// verification aid, the same "off unless explicitly requested" getenv-gated
// shape as every other ENGINE_* flag in this file, letting a headless run
// (no real mouse under Xvfb to click a Scene Hierarchy row with) pre-select
// an entity at startup so the selection outline (Phase 14d's now-removed 2D
// rectangle; Phase 18d's real 3D silhouette that replaced it) can be
// screenshot-verified without needing real mouse input. Unlike every
// boolean flag above, this one carries a *value* (the target entity's
// NameComponent string, e.g. "falling_cube") rather than being a plain on/
// off switch -- see the constructor's own use of this for how an unknown
// name is handled (logged and ignored, not a hard failure). Kept in the
// shipped binary (not `#ifdef`'d out) rather than removed after this
// phase's own verification, matching this project's own precedent of
// keeping every other verification-oriented env var (ENGINE_CAMERA_DEMO,
// ENGINE_FRUSTUM_CULL_DEMO, ENGINE_CLUSTER_DEBUG, ...) permanently available
// rather than deleting it once a phase's own review is done -- a real,
// reusable "prove the selection outline still tracks the right entity" tool
// for any later phase's own regression check, not a one-off scaffold.
std::string debugSelectEntityNameFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_SELECT");
    return value != nullptr ? std::string(value) : std::string();
}

// Phase 14e: ENGINE_DEBUG_FORCE_STATIC=<entity name> / ENGINE_DEBUG_FORCE_DYNAMIC=
// <entity name>, unset by default -- the same getenv-gated-value shape as
// ENGINE_DEBUG_SELECT just above (a target entity's NameComponent string,
// not a plain on/off switch), but for setEntityStatic() (physics.hpp)
// instead of selection. Exists specifically so a headless run can prove the
// Inspector's "Static (Immovable)" toggle's actual EFFECT on the physics
// simulation -- not just that the checkbox renders in the right state --
// without needing a real mouse to click it: applied once, in the
// constructor, after the scene has finished loading, exactly mirroring
// ENGINE_DEBUG_SELECT's own resolution. Both can be set at once (they name
// independent entities in every verification run this phase actually uses),
// so there's no ordering conflict to resolve between them the way
// cameraDemoMode_/frustumCullDemoMode_ have to pick one when both are set.
std::string debugForceStaticEntityNameFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_FORCE_STATIC");
    return value != nullptr ? std::string(value) : std::string();
}
std::string debugForceDynamicEntityNameFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_FORCE_DYNAMIC");
    return value != nullptr ? std::string(value) : std::string();
}

// Phase 18c: ENGINE_DEBUG_FORCE_VELOCITY=<entity name>:<vx>,<vy>,<vz>, unset
// by default -- the debug-env-var counterpart to a scene's own "rigidBody"
// "velocity" authoring (see scene_serialization.hpp's own comment on that
// block) for giving an entity a real, nonzero HORIZONTAL initial velocity at
// startup with no scene file edit needed. Ground friction (this phase's own
// new physics.hpp behavior) only has anything to decelerate if some entity
// actually has horizontal velocity to begin with -- nothing in
// assets/scenes/default.json's shipped entities does -- so this closes that
// gap the same way ENGINE_DEBUG_DROP_MODEL/_ASSIGN_TEXTURE close an
// equivalent "nothing in the default scene exercises this" gap for their own
// phases.
//
// Same colon-separated "<entity name>:<rest>" shape debugAssignTextureFromEnv()/
// debugDropTextureFromEnv() (Phase 15f/15g) already establish -- the entity
// name (before the FIRST ':') is resolved the same way ENGINE_DEBUG_SELECT/
// _FORCE_STATIC/_FORCE_DYNAMIC already are, via findEntityByName(), at this
// constructor's own call site below. `<vx>,<vy>,<vz>` (after the ':') is
// three comma-separated floats, parsed with std::stof; a missing separator or
// a non-numeric component is logged as a warning and the whole env var is
// ignored -- the identical "malformed debug env var is a warning, never a
// crash" contract debugAssignTextureFromEnv()'s own ':'-less case already
// establishes -- rather than std::stof's own uncaught exception taking down
// the entire run over what is, deliberately, a debug-only convenience.
struct DebugForceVelocity {
    std::string entityName;
    glm::vec3 velocity{0.0f};
};

std::optional<DebugForceVelocity> debugForceVelocityFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_FORCE_VELOCITY");
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    const std::string raw(value);
    const std::size_t colon = raw.find(':');
    if (colon == std::string::npos) {
        LOG_WARN("ENGINE_DEBUG_FORCE_VELOCITY=\"" + raw +
                  "\" is missing its ':' separator (expected \"<entity name>:<vx>,<vy>,<vz>\"); ignored");
        return std::nullopt;
    }
    const std::string name = raw.substr(0, colon);
    const std::string components = raw.substr(colon + 1);
    const std::size_t comma1 = components.find(',');
    const std::size_t comma2 = (comma1 == std::string::npos) ? std::string::npos : components.find(',', comma1 + 1);
    if (comma1 == std::string::npos || comma2 == std::string::npos) {
        LOG_WARN("ENGINE_DEBUG_FORCE_VELOCITY=\"" + raw +
                  "\" does not have exactly three ','-separated components (expected "
                  "\"<entity name>:<vx>,<vy>,<vz>\"); ignored");
        return std::nullopt;
    }
    try {
        const float vx = std::stof(components.substr(0, comma1));
        const float vy = std::stof(components.substr(comma1 + 1, comma2 - comma1 - 1));
        const float vz = std::stof(components.substr(comma2 + 1));
        return DebugForceVelocity{name, glm::vec3(vx, vy, vz)};
    } catch (const std::exception&) {
        LOG_WARN("ENGINE_DEBUG_FORCE_VELOCITY=\"" + raw + "\" has a non-numeric velocity component; ignored");
        return std::nullopt;
    }
}

// Phase 18e: ENGINE_DEBUG_GIZMO_DRAG=<entity name>, unset by default -- the
// gizmo's own headless verification hook, same getenv-gated-value shape as
// ENGINE_DEBUG_SELECT/ENGINE_DEBUG_FORCE_STATIC above. Xvfb has no physical
// pointer device at all (this project's own established headless-testing
// constraint -- see e.g. camera_capture.hpp's own header comment on why a
// real double-click gesture can't be reproduced there either), so there is
// no way to drive a real mouse-drag across a rendered gizmo handle under
// this project's own CI/dev headless target. This env var instead feeds a
// SCRIPTED sequence of synthetic screen-space mouse positions + a mouse-
// down/up state into the exact same production entry point a real mouse
// would (EditorUI::updateGizmo(), via its own setDebugMouseOverride()) --
// see that method's own editor_ui.hpp comment for exactly how, and why this
// is not a shortcut that bypasses hit-testing/axis-selection: only the raw
// "where is the mouse, is the button down" INPUT is substituted; every
// function downstream of that (screenPointToWorldRay(), hitTestGizmoAxes(),
// updateGizmoDrag(), and the resulting Transform mutation) is the same
// unmodified code a real mouse-driven drag runs through. The identical
// substitution shape ENGINE_DEBUG_SIMULATE_ESCAPE already established for
// keyboard input (a synthetic key STATE fed into the same real
// InputActionMap edge-detection, not a hand-rolled bypass of it).
std::string debugGizmoDragEntityNameFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_GIZMO_DRAG");
    return value != nullptr ? std::string(value) : std::string();
}

// Phase 18j: ENGINE_DEBUG_GIZMO_MODE=<translate|rotate>, unset (or
// "translate") by default -- the debug/test-only mechanism this phase's own
// brief calls for to reach the new rotate gizmo at all, standing in for the
// real move/rotate/scale UI switcher explicitly deferred to Phase 18k (once
// scale exists too, so it can be built once for all three tools). See
// GizmoMode's own gizmo.hpp header comment for the full "why debug-only for
// now" reasoning. An unrecognized non-empty value is logged as a warning and
// treated as "translate" -- the same "don't silently misbehave on a typo'd
// env var" treatment debugUndoOrRedoCountFromEnv() above already gives a
// different malformed value.
GizmoMode debugGizmoModeFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_GIZMO_MODE");
    if (value == nullptr || *value == '\0') {
        return GizmoMode::kTranslate;
    }
    const std::string raw(value);
    if (raw == "translate") {
        return GizmoMode::kTranslate;
    }
    if (raw == "rotate") {
        return GizmoMode::kRotate;
    }
    LOG_WARN("ENGINE_DEBUG_GIZMO_MODE=\"" + raw + "\" is not \"translate\" or \"rotate\"; defaulting to translate");
    return GizmoMode::kTranslate;
}

// Phase 18j: ENGINE_DEBUG_GIZMO_ROTATE_DRAG=<entity name>, unset by
// default -- the rotate gizmo's own headless verification hook, the exact
// same "Xvfb has no physical pointer device, so feed a scripted sequence of
// synthetic mouse positions into the same production entry point a real
// drag would" shape debugGizmoDragEntityNameFromEnv()'s own comment above
// already establishes for the translate gizmo, reused here unmodified for
// rotation. Only has any effect when gizmoMode_ is ALSO kRotate
// (ENGINE_DEBUG_GIZMO_MODE=rotate) -- see debugGizmoRotateDragEntity_'s own
// application.hpp comment for the full "also needs ENGINE_DEBUG_SELECT"
// caveat.
std::string debugGizmoRotateDragEntityNameFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_GIZMO_ROTATE_DRAG");
    return value != nullptr ? std::string(value) : std::string();
}

// Phase 18h: ENGINE_DEBUG_UNDO=<count>/ENGINE_DEBUG_REDO=<count>, unset (0)
// by default -- undo/redo's own headless verification hook, same
// getenv-gated-value shape as every other ENGINE_DEBUG_* env var above, just
// parsing a small positive integer instead of a name/kind. update() (see its
// own Phase 18h comment) consumes whichever of debugUndoCount_/
// debugRedoCount_ this resolves to at a fixed scripted frame, calling
// Application::undo()/redo() -- the exact same production methods the
// toolbar's own real undo/redo buttons and Ctrl+Z/Ctrl+Y call -- that many
// times in a row, logging registry_'s resulting state after each call. A
// missing, non-numeric, or non-positive value is logged as a warning and
// treated as 0 (no scripted undo/redo at all), the same "don't silently
// misbehave on a typo'd env var" treatment every other value-carrying
// ENGINE_DEBUG_* var in this file already gets.
int debugUndoOrRedoCountFromEnv(const char* envVarName) {
    const char* value = std::getenv(envVarName);
    if (value == nullptr) {
        return 0;
    }
    try {
        const int count = std::stoi(value);
        if (count <= 0) {
            LOG_WARN(std::string(envVarName) + "=\"" + value + "\" must be a positive integer; ignored");
            return 0;
        }
        return count;
    } catch (const std::exception&) {
        LOG_WARN(std::string(envVarName) + "=\"" + value + "\" is not a valid integer; ignored");
        return 0;
    }
}

// Phase 14f: ENGINE_DEBUG_CREATE=<cube|sphere|plane|empty|pointlight|
// directionallight|camera> (case-insensitive; "pointlight" added Phase 15a,
// "directionallight" added Phase 15b, "camera" added Phase 15c), unset by
// default -- same
// getenv-gated-value shape as ENGINE_DEBUG_SELECT/
// ENGINE_DEBUG_FORCE_STATIC/_DYNAMIC above, but for
// Application::spawnEntityFromCreateMenu() (the Scene panel's own Create
// menu's real implementation) instead. Exists specifically so a headless run
// -- no real mouse to click "+ Create" -> "Cube" with -- can prove a Create
// action actually adds a correctly-named, visibly-positioned, correctly-
// meshed entity, by calling the EXACT SAME function the menu item's own
// click handler calls (see application.hpp's own Phase 14f comment on that
// function). An unrecognized value is logged as a warning and ignored
// (CreateEntityKind::kNone -- no entity created), the same "don't silently
// misbehave on a typo'd env var" treatment every other value-carrying
// ENGINE_DEBUG_* var above already gets.
CreateEntityKind debugCreateEntityKindFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_CREATE");
    if (value == nullptr || *value == '\0') {
        return CreateEntityKind::kNone;
    }
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lowered == "cube") {
        return CreateEntityKind::kCube;
    }
    if (lowered == "sphere") {
        return CreateEntityKind::kSphere;
    }
    if (lowered == "plane") {
        return CreateEntityKind::kPlane;
    }
    if (lowered == "empty") {
        return CreateEntityKind::kEmpty;
    }
    if (lowered == "pointlight") {
        return CreateEntityKind::kPointLight;
    }
    if (lowered == "directionallight") {
        return CreateEntityKind::kDirectionalLight;
    }
    if (lowered == "camera") {
        return CreateEntityKind::kCamera;
    }
    LOG_WARN("ENGINE_DEBUG_CREATE=\"" + std::string(value) +
              "\" is not one of cube/sphere/plane/empty/pointlight/directionallight/camera; ignoring it and "
              "creating nothing");
    return CreateEntityKind::kNone;
}

// Phase 14f: ENGINE_DEBUG_DELETE=<entity name>, unset by default -- the
// destroy-side counterpart to ENGINE_DEBUG_CREATE above, same
// getenv-gated-value shape as ENGINE_DEBUG_SELECT. Calls the exact same
// destroyEntityOrphaningChildren() (transform_hierarchy.hpp) the Inspector's
// real "Delete Object" button calls, so a headless run can prove a delete
// actually removes an entity (and correctly orphans its children, per this
// phase's own design -- see that function's own header comment) without a
// real mouse to click the button with.
std::string debugDeleteEntityNameFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_DELETE");
    return value != nullptr ? std::string(value) : std::string();
}

// Phase 15e: ENGINE_DEBUG_SAVE_SCENE=1, unset by default -- same plain
// on/off getenv-gated-behavior shape as ENGINE_SHOW_DEBUG_UI/
// ENGINE_LEGACY_SCENE above (not a value-carrying var like
// ENGINE_DEBUG_SELECT/_CREATE/_DELETE, since there is nothing to name --
// Save Scene always saves everything in registry_ to the one
// kDefaultScenePath). Exists specifically so a headless run -- no real
// Ctrl+S or File > Save Scene mouse click under Xvfb -- can prove a save
// actually happened, by calling the exact same Application::
// saveCurrentScene() a real keypress/menu click calls (the same "debug env
// var calls the real function the UI calls" precedent every other
// ENGINE_DEBUG_* var above already established). See README.md's own Phase
// 15e Verify section for the full save-then-reload proof this makes
// possible without a real keyboard.
bool debugSaveSceneFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_SAVE_SCENE");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

// Phase 18i: ENGINE_DEBUG_NEW_SCENE=1, unset by default -- same plain
// on/off getenv-gated-behavior shape as ENGINE_DEBUG_SAVE_SCENE immediately
// above (nothing to name -- New Scene always does the same thing regardless
// of what's currently loaded). Exists specifically so a headless run -- no
// real File > New Scene mouse click under Xvfb -- can prove Application::
// newScene() actually empties registry_, by calling the exact same function
// a real menu click calls (the same "debug env var calls the real function
// the UI calls" precedent every other ENGINE_DEBUG_* var here already
// establishes). See README.md's own Phase 18i Verify section for the full
// Save As -> New Scene -> Open Scene proof this makes possible.
bool debugNewSceneFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_NEW_SCENE");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

// Phase 18i: ENGINE_DEBUG_SAVE_SCENE_AS=<raw name>, unset (empty) by
// default -- the debug-env-var counterpart to the Save As popup's own text
// field, so a headless run can prove Application::saveSceneAs() actually
// writes a NEW file and repoints currentScenePath_ at it, without a real
// mouse/keyboard to type into that popup. Deliberately returns the RAW env
// var value, unsanitized -- sanitizeSceneName() (scene_file_ops.hpp) is run
// against it at this function's own call site (the constructor, below),
// the identical point a real popup's own text field would be sanitized at
// (EditorUI's own live preview, editor_ui.cpp), so this free function's own
// contract stays "just read the env var," matching every other
// debug*FromEnv() function in this file, none of which do domain validation
// of their own value either (e.g. debugAssignTextureFromEnv() just above
// splits on ':' but never checks the texture path actually exists).
std::string debugSaveSceneAsFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_SAVE_SCENE_AS");
    return value != nullptr ? std::string(value) : std::string();
}

// Phase 18i: ENGINE_DEBUG_OPEN_SCENE=<name>[,<name>...], unset (empty) by
// default -- the debug-env-var counterpart to the Open Scene popup's own
// clickable list. A COMMA-SEPARATED LIST, not a single name, unlike every
// other value-carrying ENGINE_DEBUG_* var in this file: this phase's own
// verification needs to prove a whole CHAIN of Open Scene actions in one
// run (loading a just-Saved-As scene back, then loading back to "default"),
// and unlike ENGINE_DEBUG_UNDO/_REDO's own repeated-call-of-the-SAME-action
// shape, each step here needs a DIFFERENT target name -- there is no
// single fixed "the" scene to open more than once. update()'s own scripted
// frames consume one entry per call, each kDebugOpenSceneFrameSpacing
// frames apart (see this file's own Phase 18i update() comment), so a
// single process launch can chain "open A, then open B" the way multiple
// separate process launches never could (New Scene/currentScenePath_/
// undoStack_ are all live, in-process state a fresh launch can't resume
// mid-sequence).
std::vector<std::string> debugOpenSceneNamesFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_OPEN_SCENE");
    std::vector<std::string> names;
    if (value == nullptr || *value == '\0') {
        return names;
    }
    const std::string raw(value);
    std::size_t start = 0;
    while (start <= raw.size()) {
        const std::size_t comma = raw.find(',', start);
        const std::string entry = raw.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!entry.empty()) {
            names.push_back(entry);
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return names;
}

// Phase 15f: ENGINE_DEBUG_ASSIGN_TEXTURE=<entity name>:<texture path>, unset
// by default -- the debug-env-var counterpart to the Material Inspector's
// own real "Browse..." popup (editor_ui.cpp), so a headless run (no real
// mouse to click a popup list entry with under Xvfb) can prove a
// MaterialOverride (material_override.hpp) actually reaches ONE entity's
// draw call, and -- critically -- that every OTHER entity sharing the same
// cached Model renders completely unaffected. See README.md's own Phase 15f
// Verify section for the exact sibling-non-corruption proof this makes
// possible (assigning a texture to just "falling_cube" while
// "parented_demo_cube", which loads the identical assets/models/
// falling_cube.obj, stays visually untouched).
// `<entity name>` is resolved the same way ENGINE_DEBUG_SELECT/
// _FORCE_STATIC/_FORCE_DYNAMIC/_DELETE already are, below (findEntityByName());
// `<texture path>` is the SAME relative, reloadable string form
// ModelComponent::path/MaterialOverride::diffuseTexturePath already use
// (e.g. "assets/textures/rusted_metal_albedo.png"), resolved via
// resolveAssetPath() at this constructor's own call site below, exactly
// like every other asset path this engine loads (paths.hpp). The FIRST ':'
// splits name from path -- an entity name authored by this project never
// contains one, and none of assets/textures/'s own filenames do either, so
// this is unambiguous for every name/path this engine's own scenes actually
// use. Returns a pair of empty strings (the unset/malformed case) rather
// than std::optional<std::pair<...>> -- the caller below already treats "an
// empty entity name" as "nothing to do," the identical empty-string-means-
// absent convention every other ENGINE_DEBUG_* string var here already
// uses (debugDeleteEntityNameFromEnv() immediately above, ENGINE_DEBUG_SELECT
// below), so a second optional wrapper would add nothing this call site
// doesn't already check for on its own.
std::pair<std::string, std::string> debugAssignTextureFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_ASSIGN_TEXTURE");
    if (value == nullptr || *value == '\0') {
        return {};
    }
    const std::string raw(value);
    const std::size_t colon = raw.find(':');
    if (colon == std::string::npos) {
        LOG_WARN("ENGINE_DEBUG_ASSIGN_TEXTURE=\"" + raw +
                  "\" is missing its ':' separator (expected \"<entity name>:<texture path>\"); ignored");
        return {};
    }
    return {raw.substr(0, colon), raw.substr(colon + 1)};
}

// Phase 15g: ENGINE_DEBUG_DROP_MODEL=<assets/-relative model path>, unset by
// default -- headless mice don't exist, and Xvfb has no real cursor to drag
// one with, so this is the debug-env-var counterpart to the Viewport panel's
// own real "drop a model asset from the Assets panel" drop target
// (editor_ui.cpp's renderDockspaceShell(), Application::
// handleViewportAssetDrop() below). Calls the exact same
// spawnEntityFromDroppedModel() a real drop's own
// AssetDropCategory::kModel branch calls (application.hpp's own comment on
// that method has the full design) -- this is the real production entity-
// creation code path, not a parallel hand-rolled one, the identical
// precedent ENGINE_DEBUG_CREATE already set for spawnEntityFromCreateMenu().
// No validation of the path's own shape here (unlike
// debugAssignTextureFromEnv()'s ':'-split above) -- a single bare path
// string has nothing to validate the shape OF; whether it actually resolves
// to a loadable model is spawnEntityFromDroppedModel()'s own try/catch's job,
// exercised identically whether this env var or a real drop supplied the
// path.
std::string debugDropModelFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_DROP_MODEL");
    if (value == nullptr || *value == '\0') {
        return {};
    }
    return std::string(value);
}

// Phase 15g: ENGINE_DEBUG_DROP_TEXTURE=<entity name>:<texture path>, unset by
// default -- the debug-env-var counterpart to the Viewport panel's own real
// "drop a texture asset onto the currently selected entity" drop target.
// Same "<name>:<path>", first-':'-splits shape as debugAssignTextureFromEnv()
// immediately above -- deliberately its own separate env var rather than
// reusing that one: this exercises a genuinely different trigger path (the
// Viewport's new BeginDragDropTarget() handling reached through
// Application::handleViewportAssetDrop()/assignDroppedTextureOverride()
// below, not the Inspector's "Browse..." popup), even though both
// ultimately install the identical MaterialOverride component -- the same
// "two independent triggers, one shared underlying mechanism" relationship
// ENGINE_DEBUG_CREATE and the real Scene panel's own Create menu already
// have for spawnEntityFromCreateMenu().
std::pair<std::string, std::string> debugDropTextureFromEnv() {
    const char* value = std::getenv("ENGINE_DEBUG_DROP_TEXTURE");
    if (value == nullptr || *value == '\0') {
        return {};
    }
    const std::string raw(value);
    const std::size_t colon = raw.find(':');
    if (colon == std::string::npos) {
        LOG_WARN("ENGINE_DEBUG_DROP_TEXTURE=\"" + raw +
                  "\" is missing its ':' separator (expected \"<entity name>:<texture path>\"); ignored");
        return {};
    }
    return {raw.substr(0, colon), raw.substr(colon + 1)};
}

// Phase 14d: linear search over the NameComponent pool for ENGINE_DEBUG_SELECT's
// own lookup, above -- this engine's whole scene is a handful of entities
// (three, see assets/scenes/default.json), so there's no reason for anything
// fancier than each<NameComponent>() plus a string compare. Returns an
// invalid EntityId (EntityId::valid() == false) if no entity's NameComponent
// matches `name` exactly.
EntityId findEntityByName(EntityRegistry& registry, const std::string& name) {
    EntityId found;
    registry.each<NameComponent>([&](EntityId id, NameComponent& nameComponent) {
        if (!found.valid() && nameComponent.name == name) {
            found = id;
        }
    });
    return found;
}

// Phase 14f: builds a NEVER-colliding entity name for spawnEntityFromCreateMenu()
// below out of `baseName` ("Cube", "Sphere", ...): `baseName` itself if no
// existing entity's NameComponent already equals it, else "`baseName` (1)",
// then "`baseName` (2)", and so on until an unused one is found. Simple
// linear "does any entity already have this exact name" scan, run again for
// each candidate -- O(entities^2) in the worst case, same complexity
// findEntityByName() above already accepts for the same reason: this
// engine's own scenes hold a handful of entities, not thousands, and this
// project's own established convention (Phase 8b's own review, cited
// verbatim in this phase's own brief) already tolerates duplicate
// NameComponent strings elsewhere -- this function only exists to make a
// freshly created entity's name not look like a confusing duplicate of an
// existing one in the Scene Hierarchy tree by default, not to guarantee
// global uniqueness under every possible future mutation.
std::string uniqueEntityName(EntityRegistry& registry, const std::string& baseName) {
    const auto nameIsTaken = [&](const std::string& candidate) {
        bool taken = false;
        registry.each<NameComponent>([&](EntityId, NameComponent& nameComponent) {
            if (nameComponent.name == candidate) {
                taken = true;
            }
        });
        return taken;
    };

    if (!nameIsTaken(baseName)) {
        return baseName;
    }
    for (int suffix = 1;; ++suffix) {
        std::string candidate = baseName + " (" + std::to_string(suffix) + ")";
        if (!nameIsTaken(candidate)) {
            return candidate;
        }
    }
}

// Phase 8d: display-only helpers for renderDebugUI()'s "Input Bindings"
// readout below -- not used anywhere rebinding actually happens (there is
// no rebinding UI in this phase; see README.md's own Phase 8d section for
// why). GLFW has no single "name this key" API that covers every key this
// engine binds by default (glfwGetKeyName() returns null for non-printable
// keys like Escape/Space/Shift/F1, which is exactly the set this engine's
// own defaults lean on), so this is a small explicit table over just the
// keys InputActionMap's default bindings actually use, falling back to
// glfwGetKeyName() for anything else a future rebind might introduce.
const char* keyName(int glfwKey) {
    switch (glfwKey) {
        case GLFW_KEY_SPACE:
            return "Space";
        case GLFW_KEY_ESCAPE:
            return "Escape";
        case GLFW_KEY_LEFT_SHIFT:
            return "Left Shift";
        case GLFW_KEY_F1:
            return "F1";
        default:
            break;
    }
    const char* name = glfwGetKeyName(glfwKey, 0);
    return name != nullptr ? name : "?";
}

const char* actionName(InputAction action) {
    switch (action) {
        case InputAction::MoveForward:
            return "Move Forward";
        case InputAction::MoveBackward:
            return "Move Backward";
        case InputAction::MoveLeft:
            return "Move Left";
        case InputAction::MoveRight:
            return "Move Right";
        case InputAction::MoveUp:
            return "Move Up";
        case InputAction::MoveDown:
            return "Move Down";
        case InputAction::Quit:
            return "Quit";
        case InputAction::ToggleDebugUI:
            return "Toggle Debug UI";
    }
    return "?";
}

// Phase 13e: builds skybox_ from either the new HDRI (default) or the old
// 6-PNG procedural cubemap (ENGINE_USE_PROCEDURAL_SKYBOX), returning it by
// value (Skybox is move-only, not copyable -- see skybox.hpp) so this can
// live in Application's member-initializer list as a single expression,
// same "factory function returns the value a move-only member is
// initialized from" shape ClusterLightCuller/IBLProbe's own constructors
// don't need but a *conditional* construction like this one does.
Skybox buildSkybox(Shader& equirectToCubemapShader) {
    if (proceduralSkyboxFromEnv()) {
        LOG_INFO(
            "ENGINE_USE_PROCEDURAL_SKYBOX set: using the Phase 7b/10 procedural 6-face cubemap instead of "
            "the Phase 13e HDRI");
        return Skybox(kSkyboxFacePaths);
    }
    unsigned int hdrCubemap =
        loadHdrEquirectangularAsCubemap(kHdriPath, equirectToCubemapShader, kHdriCubemapFaceSize);
    return Skybox(hdrCubemap);
}

// Phase 13d: converts this file's own kPointLights/kSpotLights tables (see
// their own comment above) into the plain world-position/color/attenuation
// form ClusterLightCuller::cullLights() needs -- it doesn't care about a
// spot light's direction/cone angle (only basic.frag/pbr.frag's actual
// shading math does, unchanged from before this phase), just enough to
// test a light's position/reach against each cluster's AABB.
template <typename LightTable>
std::array<ClusterLightInput, std::tuple_size<LightTable>::value> toClusterLightInputs(const LightTable& lights) {
    std::array<ClusterLightInput, std::tuple_size<LightTable>::value> inputs{};
    for (std::size_t i = 0; i < lights.size(); ++i) {
        inputs[i] = ClusterLightInput{lights[i].position, lights[i].color, lights[i].constant, lights[i].linear,
                                       lights[i].quadratic};
    }
    return inputs;
}

// Phase 15a: the same conversion as toClusterLightInputs() above, but for a
// runtime std::vector<PointLightSample> -- render()'s own per-frame
// kPointLights-plus-ECS point light list (see its own Phase 15a comment)
// isn't a compile-time-sized std::array any more, so the template above
// (whose return size is std::tuple_size<LightTable>::value, a compile-time
// constant) can't be reused for it. A distinct name, not an overload of
// toClusterLightInputs(), specifically so overload resolution between the
// two never has to be reasoned about at a call site -- the two are used in
// deliberately different places (this one only for the ECS-aware point
// light list, the template above only for kSpotLights, still a fixed
// compile-time table) and a distinct name makes that visible at the call
// site itself, not just from the parameter type.
std::vector<ClusterLightInput> pointLightSamplesToClusterInputs(const std::vector<PointLightSample>& lights) {
    std::vector<ClusterLightInput> inputs;
    inputs.reserve(lights.size());
    for (const PointLightSample& light : lights) {
        inputs.push_back(
            ClusterLightInput{light.position, light.color, light.constant, light.linear, light.quadratic});
    }
    return inputs;
}

// Phase 13b: render() logs one combined "N/M culled" line every this-many
// frames (not every frame) -- frequent enough to see the count actually
// change as ENGINE_CAMERA_DEMO's waypoints step (every 20 frames, see
// update()'s kFramesPerStep) or across an ENGINE_FRUSTUM_CULL_DEMO run,
// without flooding the log with a near-identical line every single frame.
constexpr std::uint64_t kCullLogFrameInterval = 15;

// Phase 14e: how often update() logs ENGINE_DEBUG_FORCE_STATIC/_DYNAMIC's
// target entity's y position -- see that log call's own comment. 10 frames
// (not 15, like kCullLogFrameInterval above) purely so a short
// ENGINE_MAX_FRAMES=60 verification run still yields several data points
// (frames 0, 10, 20, ...) rather than just three or four.
constexpr std::uint64_t kPhysicsVerifyLogFrameInterval = 10;

// Phase 16: which frame ENGINE_DEBUG_SIMULATE_ESCAPE (see that env var's own
// comment below) starts holding its synthetic Escape press down on -- a
// small, fixed frame number (not the very first, frame 0) purely so a
// headless run's log clearly shows a few ordinary frames passing
// beforehand, then the press starting, then (if not captured) the loop
// actually stopping short of ENGINE_MAX_FRAMES, or (if captured, via
// ENGINE_DEBUG_FORCE_CAMERA_CAPTURE) several more ordinary frames
// continuing normally afterward -- both cases need to be visibly
// distinguishable from "the loop ended for some other reason," which frame
// 0 wouldn't demonstrate as clearly.
constexpr std::uint64_t kDebugSimulateEscapeFrame = 3;

// Post-review bug fix: how many CONSECUTIVE frames ENGINE_DEBUG_SIMULATE_ESCAPE
// holds its synthetic Escape press down for, starting at
// kDebugSimulateEscapeFrame above -- 5, deliberately more than one. The
// original version of this debug var (pre-fix) injected a synthetic press
// for exactly ONE frame, which behaves like an edge-triggered signal by
// construction and therefore could never have caught the exact bug it
// existed to catch: a REAL physical Escape press spans many consecutive
// ~16ms polls (kFrameThrottle), not one, and decideCameraCapture()
// (camera_capture.hpp) fed a LEVEL-triggered "is escape down" signal across
// several such polls would exit capture on the first held frame, then
// incorrectly quit the whole app on the very next one, since the key was
// still physically "down" and indistinguishable from a brand-new press. A
// multi-frame hold is what actually exercises that failure mode -- see
// run()'s own Phase 16 comment for how this now drives the real
// InputActionMap edge-detection machinery (via pollInputState()'s
// `forceEscapeDown`, input.hpp) across all 5 of these polls, and README.md's
// own Phase 16 Verify section for the exact log proof this produces.
constexpr std::uint64_t kDebugSimulateEscapeHoldFrames = 5;

// Phase 18e: ENGINE_DEBUG_GIZMO_DRAG's own scripted frame schedule -- see
// debugGizmoDragEntityNameFromEnv()'s own comment for the full design. 15
// (not 0/3, unlike kDebugSimulateEscapeFrame) so a headless run's log
// clearly shows a few ordinary settled frames first, then the scripted grab
// + drag beginning, giving a screenshot taken just before frame 15 something
// meaningful to diff against a screenshot taken mid-drag.
constexpr std::uint64_t kDebugGizmoDragStartFrame = 15;

// The world-space offset (along the fixed +X axis this debug script always
// drags, kDebugGizmoDragAxis below) requested at each successive scripted
// frame, starting from the target entity's own position at frame
// kDebugGizmoDragStartFrame (the grab itself, which anchors the drag
// without moving anything yet, matching updateGizmoDrag()'s own documented
// "the grab frame produces no position update" contract). Five entries
// (grab + four move steps) means the scripted drag spans frames
// [kDebugGizmoDragStartFrame, kDebugGizmoDragStartFrame + 5) inclusive of
// the grab frame; the very next frame after that (index 5, out of bounds of
// this array) is where the script releases the mouse button -- see this
// env var's own update() call site for exactly how this array's bounds
// drive that.
//
// Deliberately starts at 0.4, not exactly 0.0 -- a grab aimed EXACTLY at the
// gizmo's own origin (offset 0) is where all three axis handles physically
// meet, so hitTestGizmoAxes() would have to pick a "closest" axis among
// three genuinely tied (distance-0) candidates, decided only by
// floating-point noise in each axis's own closestPointsBetweenLines() call
// -- confirmed by an earlier run of this exact script starting at 0.0, which
// grabbed kY instead of kX purely from that tie. Starting the grab itself
// safely away from the origin, on a point unambiguously closest to the X
// axis specifically, is what a real hand aiming a mouse at a visible arrow
// (never at the exact pixel where three arrows converge) would naturally do
// anyway.
constexpr float kDebugGizmoDragOffsets[] = {0.4f, 0.9f, 1.4f, 1.9f, 2.4f};
constexpr std::size_t kDebugGizmoDragStepCount = sizeof(kDebugGizmoDragOffsets) / sizeof(kDebugGizmoDragOffsets[0]);

// This debug script always drags along world +X -- picking one fixed axis
// (rather than making the axis itself configurable via the env var) keeps
// this hook's own contract simple and its expected result trivial to state
// and verify by hand (final x == start x + kDebugGizmoDragOffsets' own last
// entry, y/z unchanged); the underlying updateGizmoDrag()/hitTestGizmoAxes()
// machinery itself is fully axis-generic (see tests/gizmo_test.cpp, which
// exercises all three), so this is a scope choice for the DEBUG SCRIPT only,
// not a limitation this phase is smuggling into the real feature.
constexpr GizmoAxis kDebugGizmoDragAxis = GizmoAxis::kX;

// Phase 18j: ENGINE_DEBUG_GIZMO_ROTATE_DRAG's own scripted frame schedule --
// see debugGizmoRotateDragEntityNameFromEnv()'s own comment for the full
// design. Reuses frame 15, the same starting frame kDebugGizmoDragStartFrame
// above uses -- a separate, independently named constant (not a shared one)
// because the two scripts are never meant to run in the SAME process (a
// verification run tests one gizmo mode at a time, via
// ENGINE_DEBUG_GIZMO_MODE), so there is no timeline-overlap concern the way
// kDebugUndoFrame/kDebugRedoFrame's own comment has to reason about for
// features that genuinely can coexist in one run.
constexpr std::uint64_t kDebugGizmoRotateDragStartFrame = 15;

// The ABSOLUTE angle (degrees, around the ring's own gizmoRingPlaneBasis()
// convention -- gizmo.hpp) targeted at each successive scripted frame,
// starting from the grab itself (index 0, which only anchors the drag
// without rotating anything yet, matching updateGizmoRotateDrag()'s own
// documented "the grab frame produces no delta" contract -- the identical
// shape kDebugGizmoDragOffsets above already establishes for translate,
// just measured in degrees-around-a-ring instead of world-units-along-a-
// line). Five entries (grab + four rotate steps), each 30 degrees apart --
// a deliberately round number so every intermediate log line's expected
// rotation is trivial to verify by hand (each step composes exactly +30
// degrees around the X axis onto whatever rotation is already live, via
// Transform::rotate() -- see updateGizmoRotateDrag()'s own header comment
// for why this is INCREMENTAL, not an absolute target the way translate's
// own newPosition is). Starts at 20, not 0 -- avoiding the ring's own
// angle-zero seam purely for the same "aim somewhere a real hand naturally
// would, not at a boundary value" instinct kDebugGizmoDragOffsets' own
// comment already documents for a different reason (there, avoiding the
// three-axis-handles-converge-at-the-origin ambiguity; a ring has no
// analogous ambiguity at its own angle 0, but starting off of it keeps this
// script's own log output free of any coincidental-looking round number at
// the very first step).
constexpr float kDebugGizmoRotateDragAngleOffsets[] = {20.0f, 50.0f, 80.0f, 110.0f, 140.0f};
constexpr std::size_t kDebugGizmoRotateDragStepCount =
    sizeof(kDebugGizmoRotateDragAngleOffsets) / sizeof(kDebugGizmoRotateDragAngleOffsets[0]);

// This debug script always drags the X ring -- the identical "fixed one
// axis, keep the script's own expected result trivial to state and verify
// by hand" scope choice kDebugGizmoDragAxis above already documents; the
// underlying updateGizmoRotateDrag()/hitTestGizmoRings() machinery itself is
// fully axis-generic (see tests/gizmo_test.cpp, which exercises all three).
constexpr GizmoAxis kDebugGizmoRotateDragAxis = GizmoAxis::kX;

// Phase 18h: ENGINE_DEBUG_UNDO/ENGINE_DEBUG_REDO's own scripted frames --
// see debugUndoOrRedoCountFromEnv()'s own comment for the full design. Both
// well after kDebugGizmoDragStartFrame's own scripted drag has fully
// finished (frame kDebugGizmoDragStartFrame + kDebugGizmoDragStepCount + 1 =
// 21, see that constant's own comment) and spaced five frames apart from
// each other, so a headless run combining ENGINE_DEBUG_CREATE/_DELETE/
// _GIZMO_DRAG/_UNDO/_REDO in one process produces a clean, easy-to-read log
// timeline: every debug action settles before the next one's own frame
// arrives, and a screenshot taken right before/after either frame has
// something meaningful, unambiguous to diff against.
constexpr std::uint64_t kDebugUndoFrame = 25;
constexpr std::uint64_t kDebugRedoFrame = 30;

// Phase 18i: ENGINE_DEBUG_SAVE_SCENE_AS/ENGINE_DEBUG_NEW_SCENE/
// ENGINE_DEBUG_OPEN_SCENE's own scripted frames -- spaced comfortably after
// kDebugRedoFrame above (rather than interleaved with it) so a headless run
// combining this phase's own debug vars with Phase 18h's ENGINE_DEBUG_UNDO/
// _REDO still produces a clean, easy-to-read timeline where every earlier
// action has fully settled before this phase's own scene-transition
// sequence begins -- New Scene in particular wipes out everything any
// earlier CREATE/DELETE/UNDO/REDO action touched, so running it any EARLIER
// than kDebugRedoFrame would make those other hooks' own proof meaningless
// (there would be nothing left in registry_ for them to act on). Ten frames
// apart from each other (rather than five, like kDebugUndoFrame/
// kDebugRedoFrame's own spacing) -- a scene transition involves real
// GL-adjacent work (loadScene()'s own resources_.getModel() calls), so a
// slightly wider settle window keeps a screenshot taken right after one of
// these frames unambiguous, the identical reasoning kDebugUndoFrame/
// kDebugRedoFrame's own comment already gives for its own five-frame gap,
// just scaled up for heavier per-step work.
constexpr std::uint64_t kDebugSaveSceneAsFrame = 40;
constexpr std::uint64_t kDebugNewSceneFrame = 50;
// ENGINE_DEBUG_OPEN_SCENE's own list is consumed one entry per frame,
// `kDebugOpenSceneFrameSpacing` frames apart starting at this base -- see
// debugOpenSceneNamesFromEnv()'s own comment for why a LIST, not a single
// name, is what this one env var carries.
constexpr std::uint64_t kDebugOpenSceneBaseFrame = 60;
constexpr std::uint64_t kDebugOpenSceneFrameSpacing = 10;

}  // namespace

Application::Application(int width, int height, const std::string& title, std::uint64_t maxFrames, bool maximized,
                          bool decorated)
    : window_(width, height, title, maximized, decorated),
      shader_(resources_.getShader(kVertexShaderPath, kFragmentShaderPath)),
      shadowShader_(resources_.getShader(kShadowVertexShaderPath, kShadowFragmentShaderPath)),
      skyboxShader_(resources_.getShader(kSkyboxVertexShaderPath, kSkyboxFragmentShaderPath)),
      postProcessShader_(resources_.getShader(kPostProcessVertexShaderPath, kPostProcessFragmentShaderPath)),
      bloomExtractShader_(resources_.getShader(kPostProcessVertexShaderPath, kBloomExtractFragmentShaderPath)),
      blurShader_(resources_.getShader(kPostProcessVertexShaderPath, kBlurFragmentShaderPath)),
      // Phase 13f: SSAO's three programs -- see this class's own Phase 13f
      // header comment.
      gbufferShader_(resources_.getShader(kGBufferVertexShaderPath, kGBufferFragmentShaderPath)),
      ssaoShader_(resources_.getShader(kPostProcessVertexShaderPath, kSSAOFragmentShaderPath)),
      ssaoBlurShader_(resources_.getShader(kPostProcessVertexShaderPath, kSSAOBlurFragmentShaderPath)),
      // Phase 18d: the selection mask pass's own program -- see this class's
      // own Phase 18d header comment.
      selectionMaskShader_(resources_.getShader(kSelectionMaskVertexShaderPath, kSelectionMaskFragmentShaderPath)),
      // Phase 18e: the translate gizmo's own program -- see this class's own
      // Phase 18e header comment.
      gizmoShader_(resources_.getShader(kGizmoVertexShaderPath, kGizmoFragmentShaderPath)),
      pbrShader_(resources_.getShader(kPBRVertexShaderPath, kPBRFragmentShaderPath)),
      irradianceShader_(resources_.getShader(kCubemapCaptureVertexShaderPath, kIrradianceFragmentShaderPath)),
      prefilterShader_(resources_.getShader(kCubemapCaptureVertexShaderPath, kPrefilterFragmentShaderPath)),
      brdfShader_(resources_.getShader(kPostProcessVertexShaderPath, kBrdfLutFragmentShaderPath)),
      // Phase 13e: pairs the existing cubemap_capture.vert with the new
      // equirect_to_cubemap.frag -- see buildSkybox()'s use of this just
      // below (via skybox_'s own initializer).
      equirectToCubemapShader_(
          resources_.getShader(kCubemapCaptureVertexShaderPath, kEquirectToCubemapFragmentShaderPath)),
      // Phase 13d: constructed directly from its two compute shader source
      // paths (not through resources_ -- see this member's own
      // application.hpp comment); only needs window_'s GL context to
      // exist, already true by this point in the initializer list.
      clusterLightCuller_(kClusterAABBComputeShaderPath, kClusterCullComputeShaderPath),
      // Phase 13c: kCascadeCount independent ShadowMap instances, all the
      // same fixed resolution (see kShadowMapWidth/Height's own Phase 13c
      // comment) -- std::array<ShadowMap, N>'s usual aggregate
      // initialization, each element move-constructed from its own
      // temporary ShadowMap(width, height).
      shadowCascades_{{ShadowMap(kShadowMapWidth, kShadowMapHeight), ShadowMap(kShadowMapWidth, kShadowMapHeight),
                       ShadowMap(kShadowMapWidth, kShadowMapHeight)}},
      // Phase 7b: sized from viewportWidth_/viewportHeight_ (already
      // constructed at this point -- both are in-class-default-initialized
      // from window_'s own real framebuffer size, see this header's
      // declaration-order comment) rather than the constructor's width/
      // height parameters directly.
      //
      // Phase 14c: no longer the window's real framebuffer size for the
      // rest of this class's life -- resizeViewportTargetsIfNeeded() (called
      // from the top of every render()) rebuilds this and every Framebuffer
      // member below it at editorUI_'s own Viewport-panel-reported size the
      // moment that differs from what's here. This constructor's own
      // initial size only matters for the handful of frames before that
      // panel size is first known (see viewportWidth_/viewportHeight_'s own
      // header comment).
      //
      // MSAA HDR framebuffer bug fix: constructed multisampled now, at
      // engine::kRequestedMsaaSamples (window.hpp) -- the same count
      // Window requests for the default framebuffer, for consistency (see
      // application.hpp's own MSAA bug-fix comment) -- rather than the
      // default (0, single-sample) every other Framebuffer in this class
      // still uses. Framebuffer itself clamps this to what the driver
      // actually grants and logs both values (see framebuffer.cpp).
      hdrFramebuffer_(viewportWidth_, viewportHeight_, kRequestedMsaaSamples),
      // MSAA HDR framebuffer bug fix: a same-size, single-sample sibling
      // hdrFramebuffer_ resolves into every frame (see render()) -- default
      // (0) sample count, same as every pre-existing Framebuffer instance
      // below.
      //
      // Phase 13g bug fix: mipmappedColor = true (depthAsTexture stays its
      // default false -- this target's depth renderbuffer is still never
      // sampled) -- see Framebuffer's own constructor comment on that flag,
      // and renderSSRComposite()'s comment on the specific aliasing bug this
      // fixes. No other Framebuffer instance below requests this: SSR's
      // ray-marched, per-fragment-varying UV is the only reader in this
      // engine of any Framebuffer's color texture that isn't a fixed,
      // 1:1-with-the-target fullscreen-quad UV.
      hdrResolveFramebuffer_(viewportWidth_, viewportHeight_, /*samples=*/0,
                              /*depthAsTexture=*/false, /*mipmappedColor=*/true),
      // Phase 11: bloom's own off-screen targets, sized at
      // 1/kBloomDownsampleFactor of the viewport's own render resolution --
      // see this header's Phase 11 comment on brightFramebuffer_/
      // pingpongFramebuffer0_/1_ for why half-res. `/ 2` (not e.g. rounding
      // up) matches every dimension this engine actually runs at (800x600
      // and other common even sizes); a stray odd input resolution would
      // round down by one texel here, which has no visible consequence for
      // a soft blur target.
      //
      // Phase 14c bug-review fix: goes through clampedDownsampleDimension()
      // (this file, above) rather than dividing viewportWidth_/
      // viewportHeight_ directly -- a small enough initial window (e.g.
      // ENGINE_WINDOW_WIDTH=1, which main.cpp's own validation accepts) can
      // still reach 0 here even though viewportWidth_/viewportHeight_
      // themselves are always >= 1; see that helper's own comment for the
      // concrete crash this fixes.
      brightFramebuffer_(clampedDownsampleDimension(viewportWidth_, kBloomDownsampleFactor),
                          clampedDownsampleDimension(viewportHeight_, kBloomDownsampleFactor)),
      pingpongFramebuffer0_(clampedDownsampleDimension(viewportWidth_, kBloomDownsampleFactor),
                             clampedDownsampleDimension(viewportHeight_, kBloomDownsampleFactor)),
      pingpongFramebuffer1_(clampedDownsampleDimension(viewportWidth_, kBloomDownsampleFactor),
                             clampedDownsampleDimension(viewportHeight_, kBloomDownsampleFactor)),
      // Phase 13f: SSAO's own three render targets, all sized at
      // 1/kSSAODownsampleFactor of the viewport's own render resolution --
      // see that constant's own comment for why. Only ssaoGBuffer_ needs
      // depthAsTexture = true (see framebuffer.hpp); ssaoRaw_/ssaoBlurred_
      // are ordinary single-sample Framebuffers, their own (harmless,
      // unused) depth renderbuffer the same accepted waste
      // brightFramebuffer_/pingpongFramebuffer0_/1_ above already carry.
      // Phase 14c bug-review fix: same clampedDownsampleDimension() fix as
      // brightFramebuffer_ above, same reason.
      ssaoGBuffer_(clampedDownsampleDimension(viewportWidth_, kSSAODownsampleFactor),
                   clampedDownsampleDimension(viewportHeight_, kSSAODownsampleFactor), /*samples=*/0,
                   /*depthAsTexture=*/true),
      ssaoRaw_(clampedDownsampleDimension(viewportWidth_, kSSAODownsampleFactor),
               clampedDownsampleDimension(viewportHeight_, kSSAODownsampleFactor)),
      ssaoBlurred_(clampedDownsampleDimension(viewportWidth_, kSSAODownsampleFactor),
                   clampedDownsampleDimension(viewportHeight_, kSSAODownsampleFactor)),
      // Phase 18d: the selection mask pass's own render target -- full
      // viewport resolution (unlike ssaoGBuffer_/ssaoRaw_/ssaoBlurred_ just
      // above), single-sample, no special flags -- see this member's own
      // application.hpp comment.
      selectionMaskFramebuffer_(viewportWidth_, viewportHeight_),
      // Phase 14c: the final tonemap/postprocess pass's own render target,
      // full viewport resolution, single-sample, no special flags -- see
      // this member's own application.hpp comment.
      viewportColorFramebuffer_(viewportWidth_, viewportHeight_),
      // Phase 13f: SSAO's hemisphere kernel + tileable rotation-noise
      // texture -- see ssao.hpp.
      ssaoKernel_(kSSAOKernelSize, kSSAONoiseDim),
      // Phase 13e: built from the new HDRI by default, or the old 6-PNG
      // procedural cubemap under ENGINE_USE_PROCEDURAL_SKYBOX -- see
      // buildSkybox() above. Must come after equirectToCubemapShader_
      // (declared earlier in application.hpp), which buildSkybox() may use.
      skybox_(buildSkybox(*equirectToCubemapShader_)),
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
      // Phase 18e: the translate gizmo's shared arrow mesh -- see mesh.hpp's
      // makeGizmoArrow() and this class's own renderGizmo() header comment.
      gizmoArrowMesh_(makeGizmoArrow()),
      // Phase 18j: the rotate gizmo's shared ring mesh -- see mesh.hpp's
      // makeGizmoRing() and this class's own renderGizmo() header comment.
      // Reuses gizmoShader_ (no second shader program) and the identical
      // per-axis rotate/scale model-matrix loop gizmoArrowMesh_ above
      // already uses -- see renderGizmo()'s own updated comment.
      gizmoRingMesh_(makeGizmoRing()),
      // Phase 9: the PBR sphere grid's shared geometry -- one Mesh, reused
      // (with a different PBRMaterial + Transform) by every sphere in
      // sphereInstances_ (built below, in the constructor body, since it
      // needs *pbrShader_ already constructed -- see this class's Phase 9
      // header note on declaration order).
      sphereMesh_(makeUVSphere(32, 32, kSphereRadius)),
      camera_(kDefaultCameraPosition),
      // Phase 8c: initial enabled/disabled state from ENGINE_SHOW_DEBUG_UI.
      // Phase 14a: no longer takes window_.handle() -- this no longer
      // creates or owns anything ImGui-related itself (see debug_ui.hpp's
      // own Phase 14a comment); it's now a plain state initializer.
      debugUI_(showDebugUIFromEnv()),
      // Phase 14a: window_.handle() already exists (window_ is the first
      // member constructed) -- always constructed, unconditionally, unlike
      // debugUI_ above (see editor_ui.hpp's own header comment for why
      // this is the one that owns the shared ImGui context/backends).
      editorUI_(window_.handle()),
      maxFrames_(maxFrames),
      cameraDemoMode_(cameraDemoModeFromEnv()),
      frustumCullDemoMode_(frustumCullDemoModeFromEnv()),
      clusterDebugMode_(clusterDebugModeFromEnv()),
      ssaoDisabled_(ssaoDisabledFromEnv()),
      ssaoDebugMode_(ssaoDebugModeFromEnv()),
      ssrDisabled_(ssrDisabledFromEnv()),
      debugSimulateEscape_(debugSimulateEscapeFromEnv()),
      // Phase 18g: same getenv-gated-value-shape initialization as every
      // other member above -- see debugShadingModeFromEnv()'s own comment.
      // Declared/initialized AFTER physicsRunning_ (which has no
      // initializer-list entry at all, see that member's own comment) to
      // match application.hpp's own declaration order, avoiding a
      // -Wreorder warning under this project's -Wall -Wextra build.
      editShadingMode_(debugShadingModeFromEnv()) {
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

    // Phase 13d: build every cluster's view-space AABB once here -- see
    // clusterLightCuller_'s own application.hpp comment for why that's
    // correct (a cluster's AABB is a pure function of the projection matrix
    // + screen size in pixels; only light *culling* against those fixed
    // AABBs needs to happen every frame, since the view matrix changes
    // whenever the camera moves).
    //
    // Phase 14c: factored out into recomputeClusterAABBs() (declared with
    // resizeViewportTargetsIfNeeded() below) because it's no longer only a
    // constructor-time, run-exactly-once thing -- the projection/screen size
    // it depends on now tracks the Viewport panel's own size
    // (viewportWidth_/viewportHeight_), which can change (in practice: once,
    // between this constructor's own placeholder initial value and
    // editorUI_'s first real reported panel size -- see viewportWidth_'s own
    // header comment) after this constructor has already run. Called here
    // for the same reason it always was (something must seed the AABBs
    // before the first frame's own cullLights() call reads them), and again
    // from resizeViewportTargetsIfNeeded() every time viewportWidth_/
    // viewportHeight_ actually change thereafter.
    recomputeClusterAABBs();

    // Phase 13f: ssaoKernel_'s hemisphere samples are fixed for this
    // engine's whole run (see ssao.hpp/ssao.cpp) -- uploaded once here,
    // rather than every frame in renderSSAO(), since GL uniform state
    // persists on a program object across frames (the same reasoning
    // render()'s own per-frame uniform uploads rely on, just applied to a
    // uniform that -- unlike view/projection/lighting -- never actually
    // changes after this constructor runs).
    {
        ssaoShader_->use();
        const std::vector<glm::vec3>& kernel = ssaoKernel_.samples();
        for (std::size_t i = 0; i < kernel.size(); ++i) {
            ssaoShader_->setVec3("uSamples[" + std::to_string(i) + "]", kernel[i]);
        }
    }

    // Phase 8b: the scene's entities (still just the one Transform +
    // ModelComponent pair Phase 8a's own comment below describes) now come
    // from assets/scenes/default.json via loadScene() by default, instead
    // of the hardcoded registry_.create()/addComponent<T> call sequence
    // Phase 8a introduced -- see scene_serialization.hpp for the file
    // format/why, and this constructor's own ENGINE_LEGACY_SCENE check
    // below for the escape hatch back to that hardcoded path. loadScene()
    // throws std::runtime_error (after LOG_ERROR'ing specifics) on a
    // missing/malformed scene file or an unloadable model reference,
    // propagating out of this constructor exactly like every other
    // resource-load failure already does (Shader/Texture/Model's own
    // constructors) -- caught by main()'s top-level try/catch, which prints
    // it and exits cleanly rather than this engine crashing or silently
    // running with an empty/broken scene.
    if (legacySceneFromEnv()) {
        // Pre-Phase-8b behavior, unchanged: one registry_ entity wrapping
        // the same Phase 5 model (assets/models/scene.obj), loaded through
        // resources_ instead of constructed directly. A small fixed
        // rotation is applied to its Transform component (rather than
        // identity), for the same reason Phase 2-4 fixed cubeTransform_'s
        // rotation: proving the composition (entity transform * accumulated
        // parent node transform * node's own local transform, see
        // Model::drawNode()) is actually being applied, not just compiling,
        // regardless of which frame a headless screenshot lands on. 12
        // degrees is small enough that scene.obj's three objects
        // (deliberately laid out to fit within Phase 3/4's unchanged camera
        // framing) stay comfortably in frame after the rotation.
        //
        // Phase 8a: this used to be `Entity sceneEntity(...);
        // entities_.push_back(...)` (see this class's Phase 6 header
        // comment); now it's registry_.create() plus two addComponent<T>
        // calls -- a Transform component and a ModelComponent -- registered
        // separately rather than bundled as one Entity's two fixed fields.
        // See ecs.hpp for why.
        //
        // Phase 8e: deliberately NOT extended with a second, RigidBody/
        // Collider-carrying entity mirroring assets/scenes/default.json's
        // "falling_cube" -- see legacySceneFromEnv()'s own comment above for
        // why. stepPhysics() (called from update() whenever physicsRunning_
        // is true -- see Phase 18b) still runs the same way under this path;
        // it just iterates zero RigidBody entities, exactly as harmlessly as
        // it would for any entity that simply never opted into that
        // component.
        LOG_INFO("ENGINE_LEGACY_SCENE set: building the scene from hardcoded C++ instead of " + kDefaultScenePath);
        const EntityId sceneEntity = registry_.create();
        registry_.addComponent<NameComponent>(sceneEntity, NameComponent{"scene"});
        Transform& sceneTransform = registry_.addComponent<Transform>(sceneEntity);
        sceneTransform.setRotation(glm::angleAxis(glm::radians(12.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
        registry_.addComponent<ModelComponent>(
            sceneEntity, ModelComponent{resources_.getModel(kScenePath, *shader_), "assets/models/scene.obj"});
    } else {
        // Phase 15e: `loadedActiveDirectionalLight` is loadScene()'s own
        // out-parameter (see scene_serialization.hpp's own "Active
        // directional light" comment) -- an invalid EntityId means the file
        // had no DirectionalLight record with "active": true (every scene
        // before this phase, including the one shipped in
        // assets/scenes/default.json today), which correctly leaves
        // activeDirectionalLight_ at its own std::nullopt default below,
        // exactly matching this phase's own "a scene with zero Directional
        // Light entities renders exactly as it did before Phase 15b"
        // baseline (see this class's own activeDirectionalLight_ comment).
        EntityId loadedActiveDirectionalLight;
        loadScene(registry_, kDefaultScenePath, resources_, *shader_, &loadedActiveDirectionalLight);
        if (loadedActiveDirectionalLight.valid()) {
            activeDirectionalLight_ = loadedActiveDirectionalLight;
        }
    }

    // Phase 18i: currentScenePath_ always starts as kDefaultScenePath here,
    // regardless of which branch above actually ran -- byte-identical to
    // this engine's entire pre-Phase-18i behavior, whether or not
    // ENGINE_LEGACY_SCENE was set (see saveCurrentScene()'s own updated
    // comment for why the legacy-scene branch also targets kDefaultScenePath
    // for saving, exactly as it silently already did before this phase: a
    // legacy-built scene has no scene FILE it was "loaded from" at all, but
    // Save Scene still needs somewhere real to write, and kDefaultScenePath
    // -- the one file this whole engine already treats as its "the" scene
    // file everywhere else -- is the only sensible default).
    currentScenePath_ = kDefaultScenePath;

    // Phase 14f: ENGINE_DEBUG_CREATE -- see debugCreateEntityKindFromEnv()'s
    // own comment above for why this exists. Applied here, right after the
    // scene above finishes loading and BEFORE the ENGINE_DEBUG_SELECT block
    // just below -- deliberately in that order, so a single verification run
    // can set ENGINE_DEBUG_CREATE=cube together with
    // ENGINE_DEBUG_SELECT=Cube and see the freshly created entity both
    // appear in the Scene Hierarchy AND show up pre-selected in the
    // Inspector in the very same screenshot, without needing two separate
    // runs. Calls the exact same spawnEntityFromCreateMenu() the Scene
    // panel's own Create menu items call (see that method's own
    // application.hpp comment) -- this is the real production entity-
    // creation code path, not a parallel hand-rolled one.
    {
        const CreateEntityKind debugCreateKind = debugCreateEntityKindFromEnv();
        if (debugCreateKind != CreateEntityKind::kNone) {
            spawnEntityFromCreateMenu(debugCreateKind);
        }
    }

    // Phase 15g: ENGINE_DEBUG_DROP_MODEL -- see debugDropModelFromEnv()'s own
    // comment above for why this exists. Applied right alongside
    // ENGINE_DEBUG_CREATE immediately above, for the identical reason: before
    // ENGINE_DEBUG_SELECT below, so one verification run can drop a model AND
    // see it pre-selected/inspected in the very same screenshot.
    {
        const std::string dropModelPath = debugDropModelFromEnv();
        if (!dropModelPath.empty()) {
            spawnEntityFromDroppedModel(dropModelPath);
        }
    }

    // Phase 14d: ENGINE_DEBUG_SELECT -- see that env var's own comment above
    // for why this exists. Resolved once, here, after the scene above has
    // finished populating registry_ (so every entity's NameComponent -- and
    // therefore findEntityByName()'s own lookup -- actually exists by the
    // time this runs); an unset env var leaves selectedEntity_ at its
    // default std::nullopt (this phase's own "no selection at startup"
    // state), and a set-but-unmatched name is logged as a warning and
    // ALSO leaves selectedEntity_ unset, rather than either crashing or
    // silently pointing selectedEntity_ at an invalid EntityId a later
    // registry_.getComponent() call would just as silently find nothing for.
    {
        const std::string debugSelectName = debugSelectEntityNameFromEnv();
        if (!debugSelectName.empty()) {
            const EntityId found = findEntityByName(registry_, debugSelectName);
            if (found.valid()) {
                selectedEntity_ = found;
                LOG_INFO("ENGINE_DEBUG_SELECT=\"" + debugSelectName + "\": pre-selecting entity " +
                          std::to_string(found.index()));
            } else {
                LOG_WARN("ENGINE_DEBUG_SELECT=\"" + debugSelectName +
                          "\" does not match any entity's name; starting with no selection");
            }
        }
    }

    // Phase 14e: ENGINE_DEBUG_FORCE_STATIC / ENGINE_DEBUG_FORCE_DYNAMIC --
    // see debugForceStaticEntityNameFromEnv()'s own comment above for why
    // this exists. Applied once, here, after the scene above has finished
    // populating registry_ (same reasoning as ENGINE_DEBUG_SELECT
    // immediately above) via setEntityStatic() (physics.hpp) -- the exact
    // same function the Inspector's "Static (Immovable)" checkbox calls, so
    // this exercises the real production code path, not a parallel
    // hand-rolled toggle. physicsVerifyEntity_ records whichever one
    // resolved (an unset/unmatched env var leaves it at std::nullopt) so
    // update() can log its Transform::position().y periodically -- see that
    // function's own Phase 14e comment.
    //
    // Post-14e bug-review, checked rather than assumed: if both name the
    // SAME entity, this block's own fixed order (FORCE_STATIC applied,
    // then FORCE_DYNAMIC applied second) means FORCE_DYNAMIC always wins --
    // setEntityStatic()'s own idempotent/well-defined-on-any-state contract
    // (physics.hpp) makes that a harmless, deterministic "last one written
    // wins" rather than any kind of conflict, but it IS FORCE_DYNAMIC that
    // wins, not FORCE_STATIC, however the two env vars happen to be ordered
    // on the command line -- confirmed by running both set to
    // "falling_cube" and observing its logged y strictly decrease exactly
    // as the FORCE_DYNAMIC-only run does. physicsVerifyEntity_ itself has
    // the same last-write-wins behavior when the two name DIFFERENT
    // entities (whichever block runs second overwrites it) -- both
    // entities' toggles still apply correctly and independently either way,
    // there just isn't periodic y-logging for both at once, only whichever
    // one this variable ends up pointing at.
    {
        const std::string forceStaticName = debugForceStaticEntityNameFromEnv();
        if (!forceStaticName.empty()) {
            const EntityId found = findEntityByName(registry_, forceStaticName);
            if (found.valid()) {
                setEntityStatic(registry_, found, /*makeStatic=*/true);
                physicsVerifyEntity_ = found;
                LOG_INFO("ENGINE_DEBUG_FORCE_STATIC=\"" + forceStaticName + "\": forced entity " +
                          std::to_string(found.index()) + " static (RigidBody removed, Collider ensured)");
            } else {
                LOG_WARN("ENGINE_DEBUG_FORCE_STATIC=\"" + forceStaticName + "\" does not match any entity's name");
            }
        }

        const std::string forceDynamicName = debugForceDynamicEntityNameFromEnv();
        if (!forceDynamicName.empty()) {
            const EntityId found = findEntityByName(registry_, forceDynamicName);
            if (found.valid()) {
                setEntityStatic(registry_, found, /*makeStatic=*/false);
                physicsVerifyEntity_ = found;
                LOG_INFO("ENGINE_DEBUG_FORCE_DYNAMIC=\"" + forceDynamicName + "\": forced entity " +
                          std::to_string(found.index()) + " dynamic (RigidBody added, gravity on)");
            } else {
                LOG_WARN("ENGINE_DEBUG_FORCE_DYNAMIC=\"" + forceDynamicName + "\" does not match any entity's name");
            }
        }
    }

    // Phase 18e: ENGINE_DEBUG_GIZMO_DRAG -- see
    // debugGizmoDragEntityNameFromEnv()'s own comment above for the full
    // design. Resolved once, here, after the scene has finished populating
    // registry_, the identical "resolve a debug target entity name to a real
    // EntityId once at startup" shape ENGINE_DEBUG_SELECT/_FORCE_STATIC/
    // _FORCE_DYNAMIC immediately above already establish. update() is what
    // actually drives the scripted drag frame by frame once this is set --
    // see that method's own Phase 18e comment.
    //
    // Important, and worth calling out explicitly: this alone does NOT
    // select the entity -- EditorUI's own updateGizmo() only ever hit-tests
    // against whatever `selectedEntity_` currently is (exactly like a real
    // gizmo only ever appears for the ALREADY-selected entity, never this
    // env var's own target). A verification run therefore also needs
    // ENGINE_DEBUG_SELECT set to the SAME entity name for the scripted drag
    // to actually hit anything -- every example in this phase's own README
    // section sets both together for exactly this reason.
    {
        const std::string gizmoDragName = debugGizmoDragEntityNameFromEnv();
        if (!gizmoDragName.empty()) {
            const EntityId found = findEntityByName(registry_, gizmoDragName);
            if (found.valid()) {
                debugGizmoDragEntity_ = found;
                LOG_INFO("ENGINE_DEBUG_GIZMO_DRAG=\"" + gizmoDragName + "\": will script a synthetic gizmo drag "
                          "against entity " + std::to_string(found.index()) + " starting frame " +
                          std::to_string(kDebugGizmoDragStartFrame));
            } else {
                LOG_WARN("ENGINE_DEBUG_GIZMO_DRAG=\"" + gizmoDragName + "\" does not match any entity's name");
            }
        }
    }

    // Phase 18j: ENGINE_DEBUG_GIZMO_MODE -- see debugGizmoModeFromEnv()'s
    // own comment above for the full design. Applied here, in the
    // constructor body (not gizmoMode_'s own in-class default), the same
    // "apply a debug env-var override in the body" placement every other
    // constructor-time debug flag in this class already follows (see e.g.
    // debugForcePlayModeFromEnv()'s own call site comment for why). Only
    // logged when it actually changes anything from the default -- an unset/
    // "translate" run (every pre-18j behavior) stays silent here, matching
    // this class's own "don't log a no-op" instinct elsewhere.
    gizmoMode_ = debugGizmoModeFromEnv();
    if (gizmoMode_ == GizmoMode::kRotate) {
        LOG_INFO("ENGINE_DEBUG_GIZMO_MODE=rotate: the rotate gizmo (not the translate gizmo) is now the active "
                  "gizmo tool");
    }

    // Phase 18j: ENGINE_DEBUG_GIZMO_ROTATE_DRAG -- the identical resolve-
    // once-at-startup shape the ENGINE_DEBUG_GIZMO_DRAG block immediately
    // above already establishes, just for the rotate gizmo's own scripted
    // drag. See debugGizmoRotateDragEntityNameFromEnv()'s own comment for
    // why this alone does nothing without ALSO setting
    // ENGINE_DEBUG_GIZMO_MODE=rotate (and ENGINE_DEBUG_SELECT, the same
    // "also needs SELECT" caveat the translate hook's own comment already
    // documents).
    {
        const std::string gizmoRotateDragName = debugGizmoRotateDragEntityNameFromEnv();
        if (!gizmoRotateDragName.empty()) {
            const EntityId found = findEntityByName(registry_, gizmoRotateDragName);
            if (found.valid()) {
                debugGizmoRotateDragEntity_ = found;
                LOG_INFO("ENGINE_DEBUG_GIZMO_ROTATE_DRAG=\"" + gizmoRotateDragName +
                          "\": will script a synthetic rotate-gizmo drag against entity " +
                          std::to_string(found.index()) + " starting frame " +
                          std::to_string(kDebugGizmoRotateDragStartFrame));
            } else {
                LOG_WARN("ENGINE_DEBUG_GIZMO_ROTATE_DRAG=\"" + gizmoRotateDragName +
                          "\" does not match any entity's name");
            }
        }
    }

    // Phase 18c: ENGINE_DEBUG_FORCE_VELOCITY -- see
    // debugForceVelocityFromEnv()'s own comment above for why this exists.
    // Applied after FORCE_STATIC/FORCE_DYNAMIC above so it can target an
    // entity either of those just made dynamic in this same run; ensures the
    // target actually has a RigidBody first via setEntityStatic(...,
    // /*makeStatic=*/false) -- the IDENTICAL production call
    // ENGINE_DEBUG_FORCE_DYNAMIC itself uses just above (idempotent/
    // well-defined if the entity already has one, per setEntityStatic()'s
    // own physics.hpp contract) -- then overwrites that RigidBody's
    // velocity directly. Also records physicsVerifyEntity_ (overwriting
    // whichever of FORCE_STATIC/FORCE_DYNAMIC set it last, the same
    // documented last-write-wins behavior physicsVerifyEntity_ already has),
    // so update()'s own periodic physics-verify log (see that call site's
    // own comment below) picks up this entity's horizontal velocity/
    // position too, with no separate logging code needed for this env var.
    {
        const std::optional<DebugForceVelocity> forceVelocity = debugForceVelocityFromEnv();
        if (forceVelocity.has_value()) {
            const EntityId found = findEntityByName(registry_, forceVelocity->entityName);
            if (found.valid()) {
                setEntityStatic(registry_, found, /*makeStatic=*/false);
                RigidBody* body = registry_.getComponent<RigidBody>(found);
                if (body != nullptr) {
                    body->velocity = forceVelocity->velocity;
                    physicsVerifyEntity_ = found;
                    LOG_INFO("ENGINE_DEBUG_FORCE_VELOCITY=\"" + forceVelocity->entityName + "\": set entity " +
                              std::to_string(found.index()) + " velocity to (" +
                              std::to_string(forceVelocity->velocity.x) + ", " +
                              std::to_string(forceVelocity->velocity.y) + ", " +
                              std::to_string(forceVelocity->velocity.z) + ")");
                }
            } else {
                LOG_WARN("ENGINE_DEBUG_FORCE_VELOCITY=\"" + forceVelocity->entityName + "\" does not match any entity's name");
            }
        }
    }

    // Phase 14f: ENGINE_DEBUG_DELETE -- see debugDeleteEntityNameFromEnv()'s
    // own comment above for why this exists. Applied last among this
    // constructor's ENGINE_DEBUG_* entity-name lookups (after CREATE/SELECT/
    // FORCE_STATIC/FORCE_DYNAMIC above), so it can delete an entity any of
    // those earlier steps just named/created/selected/force-toggled within
    // the SAME verification run if needed, and so it reads as "the last
    // word" -- whatever this names is gone by the time the first frame
    // renders.
    //
    // Phase 18h: now calls deleteEntity() (this class's own new method)
    // instead of destroyEntityOrphaningChildren() directly -- the exact
    // same production code path the Inspector's real "Delete Object" button
    // now goes through too (via render()'s own deleteEntityRequested
    // handling), so this debug hook also exercises (and this phase's own
    // headless verification can prove) that the deletion got captured onto
    // undoStack_, not just that the entity is gone. deleteEntity() itself
    // still clears selectedEntity_ if it named the same entity, the
    // identical behavior this block used to implement inline.
    {
        const std::string debugDeleteName = debugDeleteEntityNameFromEnv();
        if (!debugDeleteName.empty()) {
            const EntityId found = findEntityByName(registry_, debugDeleteName);
            if (found.valid()) {
                deleteEntity(found);
                LOG_INFO("ENGINE_DEBUG_DELETE=\"" + debugDeleteName + "\": destroyed entity " +
                          std::to_string(found.index()) + " (children, if any, orphaned to root)");
            } else {
                LOG_WARN("ENGINE_DEBUG_DELETE=\"" + debugDeleteName + "\" does not match any entity's name");
            }
        }
    }

    // Phase 18h: ENGINE_DEBUG_UNDO/ENGINE_DEBUG_REDO -- see
    // debugUndoOrRedoCountFromEnv()'s own comment above for the full design.
    // Resolved once, here (after every other constructor-time debug hook
    // above has had a chance to push something onto undoStack_), so
    // update()'s own scripted frames (kDebugUndoFrame/kDebugRedoFrame) know
    // how many times to call undo()/redo().
    debugUndoCount_ = debugUndoOrRedoCountFromEnv("ENGINE_DEBUG_UNDO");
    debugRedoCount_ = debugUndoOrRedoCountFromEnv("ENGINE_DEBUG_REDO");

    // Phase 18i: ENGINE_DEBUG_NEW_SCENE/ENGINE_DEBUG_SAVE_SCENE_AS/
    // ENGINE_DEBUG_OPEN_SCENE -- see those three env vars' own
    // debugNewSceneFromEnv()/debugSaveSceneAsFromEnv()/
    // debugOpenSceneNamesFromEnv() comments above for the full design.
    // debugSaveSceneAsName_ is sanitized HERE, once, via the exact same
    // sanitizeSceneName() (scene_file_ops.hpp) a real Save As popup's own
    // live preview runs its text field through -- an env var whose value
    // doesn't sanitize to anything usable (e.g. unset, or entirely
    // punctuation) is silently treated as "unset" (LOG_WARN'd, left empty),
    // the identical tolerance debugAssignTextureFromEnv()'s own malformed-
    // input handling already shows elsewhere in this file, rather than
    // this constructor itself crashing/throwing over a malformed debug var.
    {
        const std::string rawSaveAsName = debugSaveSceneAsFromEnv();
        if (!rawSaveAsName.empty()) {
            const std::optional<std::string> sanitized = sanitizeSceneName(rawSaveAsName);
            if (sanitized.has_value()) {
                debugSaveSceneAsName_ = *sanitized;
            } else {
                LOG_WARN("ENGINE_DEBUG_SAVE_SCENE_AS=\"" + rawSaveAsName +
                          "\" does not sanitize to a usable scene name; ignored");
            }
        }
    }
    debugNewSceneRequested_ = debugNewSceneFromEnv();
    debugOpenSceneNames_ = debugOpenSceneNamesFromEnv();

    // Phase 15f: ENGINE_DEBUG_ASSIGN_TEXTURE -- see
    // debugAssignTextureFromEnv()'s own comment above for why this exists.
    // Applied after DELETE (so it can target an entity CREATE/SELECT/
    // FORCE_STATIC/FORCE_DYNAMIC/DELETE just acted on within this same run)
    // and before SAVE_SCENE (so one headless run can assign an override AND
    // prove it survives a save -- SAVE_SCENE's own comment already explains
    // why it, in turn, needs to run last of all).
    {
        const auto [assignName, assignTexturePath] = debugAssignTextureFromEnv();
        if (!assignName.empty()) {
            const EntityId found = findEntityByName(registry_, assignName);
            if (found.valid()) {
                // resources_.getTexture() (like every Texture load in this
                // engine, see resource_manager.cpp) takes an already-
                // resolved path; resolveAssetPath() here mirrors exactly
                // what loadScene()/spawnEntityFromCreateMenu() already do
                // for model paths (see either's own comment) -- resolve
                // once, right before the GL-touching call, while storing
                // the ORIGINAL relative form in diffuseTexturePath below so
                // it stays a reloadable reference (see
                // material_override.hpp's own MaterialOverride comment).
                const std::string resolvedTexturePath = resolveAssetPath(assignTexturePath);
                try {
                    MaterialOverride& materialOverride =
                        registry_.addComponent<MaterialOverride>(found, MaterialOverride{});
                    materialOverride.diffuseTexture = resources_.getTexture(resolvedTexturePath);
                    materialOverride.diffuseTexturePath = assignTexturePath;
                    LOG_INFO("ENGINE_DEBUG_ASSIGN_TEXTURE: entity \"" + assignName +
                              "\" now overrides its diffuse texture with \"" + assignTexturePath + "\"");
                } catch (const std::exception& e) {
                    LOG_WARN("ENGINE_DEBUG_ASSIGN_TEXTURE: failed to load texture \"" + assignTexturePath +
                              "\" for entity \"" + assignName + "\": " + std::string(e.what()));
                }
            } else {
                LOG_WARN("ENGINE_DEBUG_ASSIGN_TEXTURE=\"" + assignName + ":" + assignTexturePath +
                          "\" does not match any entity's name");
            }
        }
    }

    // Phase 15g: ENGINE_DEBUG_DROP_TEXTURE -- see debugDropTextureFromEnv()'s
    // own comment above for why this exists. Applied right alongside
    // ENGINE_DEBUG_ASSIGN_TEXTURE immediately above (after DELETE, before
    // SAVE_SCENE), for the identical reasoning that block's own comment
    // already gives. Calls the exact same assignDroppedTextureOverride() a
    // real Viewport texture drop's own AssetDropCategory::kTexture branch
    // calls (application.hpp's own comment on that method has the full
    // design) -- this is the real production code path this phase's own
    // drag-and-drop target uses, not a parallel hand-rolled one.
    {
        const auto [dropTexEntityName, dropTexturePath] = debugDropTextureFromEnv();
        if (!dropTexEntityName.empty()) {
            const EntityId found = findEntityByName(registry_, dropTexEntityName);
            if (found.valid()) {
                assignDroppedTextureOverride(found, dropTexEntityName, dropTexturePath);
            } else {
                LOG_WARN("ENGINE_DEBUG_DROP_TEXTURE=\"" + dropTexEntityName + ":" + dropTexturePath +
                          "\" does not match any entity's name");
            }
        }
    }

    // Phase 15e: ENGINE_DEBUG_SAVE_SCENE -- see debugSaveSceneFromEnv()'s own
    // comment above for why this exists. Applied last of all this
    // constructor's ENGINE_DEBUG_* blocks (after CREATE/SELECT/
    // FORCE_STATIC/FORCE_DYNAMIC/DELETE above), the same "reads as the last
    // word" ordering ENGINE_DEBUG_DELETE's own comment already explains --
    // so a single headless run can create/select/force/delete entities and
    // then prove Save Scene actually persists whatever registry_ ends up
    // holding by the time this constructor returns, all in one process.
    // Calls the exact same saveCurrentScene() a real Ctrl+S press or File >
    // Save Scene click calls.
    if (debugSaveSceneFromEnv()) {
        saveCurrentScene();
    }

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

    // Phase 18g: log the resolved shading mode only when it's not the
    // default -- editShadingMode_ was already set from
    // debugShadingModeFromEnv() in the initializer list above; this just
    // makes a non-default startup choice visible in a headless run's own
    // log, the same "log a non-default debug-env-var outcome" precedent
    // ENGINE_DEBUG_FORCE_PLAY_MODE's own LOG_INFO (just below) establishes.
    if (editShadingMode_ != ShadingMode::kRendered) {
        LOG_INFO(std::string("ENGINE_DEBUG_SHADING_MODE set: starting Edit mode in ") +
                  (editShadingMode_ == ShadingMode::kSolid ? "Solid" : "Wireframe") + " shading mode");
    }

    // Phase 16: ENGINE_DEBUG_FORCE_CAMERA_CAPTURE -- see
    // debugForceCameraCaptureFromEnv()'s own comment above for why this
    // exists and what it can/can't prove headlessly. Applied here (after
    // the scene/ENGINE_DEBUG_CREATE/SELECT/etc. blocks above, which this
    // flag has no interaction with either way) rather than in the
    // member-initializer list, since setCameraCaptured() needs window_
    // already fully constructed to call window_.setCursorCaptured() -- the
    // same "apply in the body, not the initializer list" reasoning every
    // other constructor-time side effect in this class already follows.
    if (debugForceCameraCaptureFromEnv()) {
        setCameraCaptured(true);
        LOG_INFO("ENGINE_DEBUG_FORCE_CAMERA_CAPTURE set: starting already captured");
    }

    // Phase 18b: ENGINE_DEBUG_FORCE_PLAY_MODE -- see
    // debugForcePlayModeFromEnv()'s own comment above for why this exists.
    // No setter call needed the way setCameraCaptured() is above --
    // physicsRunning_ has no side effect of its own to keep in sync (unlike
    // cameraCaptured_, which also has to flip the OS cursor's own
    // hidden/visible state via window_), so a direct assignment here is
    // already the entire effect a real Play-button click has (see this
    // member's own application.hpp comment).
    if (debugForcePlayModeFromEnv()) {
        physicsRunning_ = true;
        LOG_INFO("ENGINE_DEBUG_FORCE_PLAY_MODE set: starting already in Play mode (physics running)");
    }

    if (clusterDebugMode_) {
        LOG_INFO(
            "ENGINE_CLUSTER_DEBUG set: tinting fragments by their cluster's light count to visualize clustered "
            "light culling");
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

// Phase 16: see this method's own application.hpp comment for the full
// contract (every real side effect a capture-state transition needs, and
// which of run()/render()/the constructor calls it for which trigger).
void Application::setCameraCaptured(bool captured) {
    if (captured == cameraCaptured_) {
        // Already in the requested state -- every call site above is free to
        // call this unconditionally with whatever decideCameraCapture()
        // computed (which may legitimately be "no change," the ordinary
        // per-frame case) without its own separate "did this actually
        // change" guard.
        return;
    }
    cameraCaptured_ = captured;
    window_.setCursorCaptured(captured);
    // Discards Camera's own tracked last cursor position on EITHER
    // transition -- see this method's own application.hpp comment for why
    // both directions, not just entry.
    camera_.resetMouseTracking();
    LOG_INFO(captured ? "Camera capture entered: cursor hidden, WASD + mouse-look now drive the camera"
                       : "Camera capture exited: cursor restored, WASD + mouse-look no longer drive the camera");
}

void Application::update(double deltaTime, const InputState& input) {
    totalTime_ += deltaTime;

    // Phase 8d: InputAction::ToggleDebugUI (default F1), handled up front
    // and independent of the demo-mode branches below -- the overlay
    // should be toggleable regardless of which camera-driving path is
    // active. toggleDebugUIPressed is edge-triggered (see input.hpp/
    // input_action_map.hpp), so this fires exactly once per physical F1
    // press rather than flipping back and forth every frame the key
    // happens to be held; under headless Xvfb there's no real keypress,
    // so this is always false there and debugUI_'s state never changes
    // from whatever ENGINE_SHOW_DEBUG_UI set at construction -- the
    // existing headless verification path is unaffected.
    if (input.toggleDebugUIPressed) {
        debugUI_.setEnabled(!debugUI_.enabled());
    }

    // Phase 8e: gravity + ground-plane collision for every registry_ entity
    // that has a RigidBody (see physics.hpp/physics.cpp) -- the
    // assets/scenes/default.json "falling_cube" entity today, but opt-in
    // per entity like every other component, so any future entity with its
    // own "rigidBody"/"collider" scene blocks gets this for free. Reuses
    // this frame's own deltaTime (already computed once by run(), passed in
    // here) rather than a second, separately-tracked clock -- clamped to
    // kMaxPhysicsTimestep first (see that constant's own comment above for
    // why: this project's own headless verification runs Debug-build
    // frames far slower than a real 60 FPS budget, which would otherwise
    // make gravity integrate implausibly large jumps per frame). Placed
    // before the camera-driving branches below since physics doesn't read
    // or depend on camera_ at all, and before render() is called (later
    // this same frame, from run()) so the object's Transform is already at
    // its new position by the time this frame actually draws it.
    //
    // Phase 18b: now gated behind physicsRunning_ -- from Phase 8e through
    // Phase 18a this call ran UNCONDITIONALLY, every single frame, meaning
    // gravity/collision were always live, even while the user was only
    // trying to select/inspect an entity in the editor (the project owner's
    // own explicit complaint: "the selection to have physics enabled seems
    // extremely bad the way it acts not new user friendly"). Standard
    // editor convention (Unity/Unreal/Godot) is that simulation only runs in
    // "Play mode"; the scene stays frozen in "Edit mode" so it can be freely
    // arranged. physicsRunning_ defaults false (Edit mode) -- see that
    // member's own application.hpp comment for why false, not true, is the
    // correct default here -- so stepPhysics() now simply does not run at
    // all until the user clicks the Viewport toolbar's own Play button
    // (editor_ui.cpp's renderViewportToolbar()): no gravity integration, no
    // collision resolution, every entity's Transform stays exactly where it
    // was left. Deliberately NOT done this phase: any snapshot/restore of
    // scene state across a Play->Stop transition (Unity's own "changes made
    // during Play don't persist" behavior) -- this phase only gates the
    // physics STEP itself; a Transform any physics step (or a manual edit
    // made while playing) moved stays moved after Pause, the same as it
    // always has. Also deliberately NOT done: any keyboard shortcut for
    // Play/Pause (toolbar-button-only, matching every other toolbar button
    // today) and any friction/damping change to the simulation itself (the
    // actual gravity/collision math in physics.hpp is untouched byte-for-
    // byte by this phase -- only WHETHER it runs changes, never HOW).
    if (physicsRunning_) {
        stepPhysics(registry_, std::min(static_cast<float>(deltaTime), kMaxPhysicsTimestep), kGroundY);
    }

    // Phase 14e: numeric proof that ENGINE_DEBUG_FORCE_STATIC/_DYNAMIC's
    // setEntityStatic() call actually changed how stepPhysics() (just above)
    // treats this entity -- not just that it flipped a component flag. Logs
    // every kPhysicsVerifyLogFrameInterval frames (same periodic-log shape
    // as kCullLogFrameInterval elsewhere in this file) rather than every
    // frame, so a headless run's log stays readable while still showing a
    // clear trajectory over time: a forced-static entity's y should read
    // exactly the same every time; a forced-dynamic entity's y should read
    // strictly lower each time, exactly like physics_test.cpp's own
    // hand-computed free-fall expectations. Silent (no log line at all)
    // unless one of those two env vars actually resolved to a real entity --
    // this must never fire in an ordinary run.
    if (physicsVerifyEntity_.has_value() && frameCount_ % kPhysicsVerifyLogFrameInterval == 0) {
        const Transform* transform = registry_.getComponent<Transform>(*physicsVerifyEntity_);
        if (transform != nullptr) {
            // Phase 18c: also logs x/z position and the RigidBody's own
            // horizontal (X/Z) speed, when this entity has one -- the
            // numeric proof ENGINE_DEBUG_FORCE_VELOCITY's own comment above
            // needs to show ground friction actually decelerating horizontal
            // motion over time (x should move less each interval, speed
            // should strictly decrease then hold at exactly zero), on top of
            // the pre-existing y-only trajectory Phase 14e's
            // ENGINE_DEBUG_FORCE_STATIC/_DYNAMIC verification already
            // relies on -- that y-only case is completely unaffected, since
            // this only adds fields to the same log line, changing nothing
            // about when it fires or what it says about y.
            std::string line = "Phase 14e physics-verify: frame " + std::to_string(frameCount_) + ", entity " +
                                std::to_string(physicsVerifyEntity_->index()) + " y = " +
                                std::to_string(transform->position().y);
            const RigidBody* body = registry_.getComponent<RigidBody>(*physicsVerifyEntity_);
            if (body != nullptr) {
                const float horizontalSpeed = glm::length(glm::vec2(body->velocity.x, body->velocity.z));
                line += ", x = " + std::to_string(transform->position().x) + ", z = " + std::to_string(transform->position().z) +
                        ", horizontalSpeed = " + std::to_string(horizontalSpeed);
            }
            LOG_INFO(line);
        }
    }

    // Phase 18e: ENGINE_DEBUG_GIZMO_DRAG's own scripted synthetic drag --
    // see debugGizmoDragEntityNameFromEnv()'s own comment for the full
    // design/why. This block only ever decides WHAT synthetic mouse input to
    // feed EditorUI this frame (via setDebugMouseOverride()) -- it never
    // touches the entity's Transform directly; EditorUI::updateGizmo(),
    // called later this SAME frame from render(), is what actually runs the
    // real hit-test/drag-state-machine/Transform-mutation code against that
    // input, exactly as it would for a real mouse-driven drag. Every
    // position logged below is therefore this frame's value as of the START
    // of update() -- i.e. the result of whatever the PREVIOUS frame's own
    // scripted step already applied, not this frame's (which EditorUI hasn't
    // run yet) -- the same one-frame latency this class documents everywhere
    // else a per-frame read precedes the ImGui pass that might change it
    // (selectedEntity_'s own header comment).
    if (debugGizmoDragEntity_.has_value()) {
        Transform* transform = registry_.getComponent<Transform>(*debugGizmoDragEntity_);
        if (transform != nullptr) {
            if (frameCount_ == kDebugGizmoDragStartFrame) {
                debugGizmoDragStartPosition_ = transform->position();
            }

            if (frameCount_ >= kDebugGizmoDragStartFrame &&
                frameCount_ < kDebugGizmoDragStartFrame + kDebugGizmoDragStepCount) {
                const std::size_t step = static_cast<std::size_t>(frameCount_ - kDebugGizmoDragStartFrame);
                const glm::vec3 targetWorldPoint = debugGizmoDragStartPosition_ +
                                                    gizmoAxisDirection(kDebugGizmoDragAxis) * kDebugGizmoDragOffsets[step];
                const float aspect = viewportHeight_ != 0
                                          ? static_cast<float>(viewportWidth_) / static_cast<float>(viewportHeight_)
                                          : 1.0f;
                const std::optional<glm::vec2> screenPoint =
                    worldPointToScreenPoint(targetWorldPoint, static_cast<float>(viewportWidth_),
                                             static_cast<float>(viewportHeight_), camera_.getViewMatrix(),
                                             camera_.getProjectionMatrix(aspect));
                if (screenPoint.has_value()) {
                    editorUI_.setDebugMouseOverride(*screenPoint, /*mouseDown=*/true,
                                                     /*mousePressedThisFrame=*/step == 0);
                } else {
                    // Behind the camera this frame (shouldn't happen with
                    // this engine's own default camera pose/scene, but see
                    // worldPointToScreenPoint()'s own "no meaningful result"
                    // contract) -- skip this one step's override rather than
                    // feeding a garbage screen position; the drag simply
                    // doesn't advance this one frame.
                    LOG_WARN("ENGINE_DEBUG_GIZMO_DRAG: target world point projects behind the camera at step " +
                              std::to_string(step) + "; skipping");
                }
                LOG_INFO("Phase 18e gizmo-drag-verify: frame " + std::to_string(frameCount_) + " step " +
                          std::to_string(step) + ", entity " + std::to_string(debugGizmoDragEntity_->index()) +
                          " position = (" + std::to_string(transform->position().x) + ", " +
                          std::to_string(transform->position().y) + ", " + std::to_string(transform->position().z) +
                          ")");
            } else if (frameCount_ == kDebugGizmoDragStartFrame + kDebugGizmoDragStepCount) {
                // One frame past the last scripted move step -- release the
                // mouse button, ending the drag the same way a real
                // mouse-button-up would (see updateGizmoDrag()'s own
                // release-branch contract, gizmo.hpp).
                editorUI_.setDebugMouseOverride(std::nullopt, /*mouseDown=*/false, /*mousePressedThisFrame=*/false);
                LOG_INFO("Phase 18e gizmo-drag-verify: frame " + std::to_string(frameCount_) +
                          " release, entity " + std::to_string(debugGizmoDragEntity_->index()) +
                          " final position = (" + std::to_string(transform->position().x) + ", " +
                          std::to_string(transform->position().y) + ", " + std::to_string(transform->position().z) +
                          ")");
            }
        }
        // else: defensive only -- every entity this env var can resolve to
        // (findEntityByName(), constructor) has a Transform by construction.
    }

    // Phase 18j: ENGINE_DEBUG_GIZMO_ROTATE_DRAG's own scripted synthetic
    // rotate drag -- the identical shape the ENGINE_DEBUG_GIZMO_DRAG block
    // immediately above already establishes (this block only ever decides
    // WHAT synthetic mouse input to feed EditorUI this frame; the real
    // hit-test/drag-state-machine/Transform-mutation code runs later this
    // SAME frame, inside EditorUI's own updateGizmoRotate(), called from
    // render() -- only when gizmoMode_ == kRotate). Every rotation logged
    // below is therefore this frame's value as of the START of update(),
    // the identical one-frame latency the translate block above documents.
    //
    // Unlike the translate script, this one does NOT need to cache the
    // entity's own start position/rotation across frames -- the ring's
    // plane is defined purely by the entity's CURRENT world position
    // (gizmoOrigin, re-read fresh every frame below, exactly like
    // EditorUI::updateGizmoRotate() itself does -- see
    // updateGizmoRotateDrag()'s own gizmo.hpp header comment for why a
    // rotation's own plane has no "already moving out from under the drag"
    // problem the translate gizmo's fixed axis LINE has), and a target
    // ANGLE (kDebugGizmoRotateDragAngleOffsets) needs no anchor of its own
    // to be computed relative to.
    if (debugGizmoRotateDragEntity_.has_value()) {
        Transform* transform = registry_.getComponent<Transform>(*debugGizmoRotateDragEntity_);
        if (transform != nullptr) {
            if (frameCount_ >= kDebugGizmoRotateDragStartFrame &&
                frameCount_ < kDebugGizmoRotateDragStartFrame + kDebugGizmoRotateDragStepCount) {
                const std::size_t step = static_cast<std::size_t>(frameCount_ - kDebugGizmoRotateDragStartFrame);
                // Same root-entity "local position IS world position"
                // simplification gizmo.hpp's own header comment documents
                // for the translate gizmo -- every entity this env var can
                // target in this engine's own demo scene is unparented.
                const glm::vec3 gizmoOrigin = transform->position();
                const float distanceToCamera = glm::length(camera_.position() - gizmoOrigin);
                const float ringRadius = gizmoAxisLength(distanceToCamera);
                const RingPlaneBasis basis = gizmoRingPlaneBasis(kDebugGizmoRotateDragAxis);
                const float angleRad = glm::radians(kDebugGizmoRotateDragAngleOffsets[step]);
                const glm::vec3 targetWorldPoint =
                    gizmoOrigin + ringRadius * (std::cos(angleRad) * basis.u + std::sin(angleRad) * basis.v);
                const float aspect = viewportHeight_ != 0
                                          ? static_cast<float>(viewportWidth_) / static_cast<float>(viewportHeight_)
                                          : 1.0f;
                const std::optional<glm::vec2> screenPoint =
                    worldPointToScreenPoint(targetWorldPoint, static_cast<float>(viewportWidth_),
                                             static_cast<float>(viewportHeight_), camera_.getViewMatrix(),
                                             camera_.getProjectionMatrix(aspect));
                if (screenPoint.has_value()) {
                    editorUI_.setDebugMouseOverride(*screenPoint, /*mouseDown=*/true,
                                                     /*mousePressedThisFrame=*/step == 0);
                } else {
                    LOG_WARN("ENGINE_DEBUG_GIZMO_ROTATE_DRAG: target world point projects behind the camera at "
                              "step " + std::to_string(step) + "; skipping");
                }
                const glm::vec3 rotationDeg = glm::degrees(glm::eulerAngles(transform->rotation()));
                LOG_INFO("Phase 18j gizmo-rotate-drag-verify: frame " + std::to_string(frameCount_) + " step " +
                          std::to_string(step) + ", entity " + std::to_string(debugGizmoRotateDragEntity_->index()) +
                          " rotation (deg) = (" + std::to_string(rotationDeg.x) + ", " +
                          std::to_string(rotationDeg.y) + ", " + std::to_string(rotationDeg.z) + ")");
            } else if (frameCount_ == kDebugGizmoRotateDragStartFrame + kDebugGizmoRotateDragStepCount) {
                // One frame past the last scripted rotate step -- release
                // the mouse button, ending the drag the same way a real
                // mouse-button-up would (updateGizmoRotateDrag()'s own
                // release-branch contract, gizmo.hpp).
                editorUI_.setDebugMouseOverride(std::nullopt, /*mouseDown=*/false, /*mousePressedThisFrame=*/false);
                const glm::vec3 rotationDeg = glm::degrees(glm::eulerAngles(transform->rotation()));
                LOG_INFO("Phase 18j gizmo-rotate-drag-verify: frame " + std::to_string(frameCount_) +
                          " release, entity " + std::to_string(debugGizmoRotateDragEntity_->index()) +
                          " final rotation (deg) = (" + std::to_string(rotationDeg.x) + ", " +
                          std::to_string(rotationDeg.y) + ", " + std::to_string(rotationDeg.z) + ")");
            }
        }
        // else: defensive only -- every entity this env var can resolve to
        // (findEntityByName(), constructor) has a Transform by construction.
    }

    // Phase 18h: ENGINE_DEBUG_UNDO/ENGINE_DEBUG_REDO's own scripted frames --
    // see debugUndoOrRedoCountFromEnv()'s own comment for the full design.
    // Fires exactly once each (frameCount_ is monotonically increasing, so
    // `== kDebugUndoFrame` can never match twice), calling the exact same
    // undo()/redo() a real Ctrl+Z/Ctrl+Y press or toolbar button click
    // would, `debugUndoCount_`/`debugRedoCount_` times in a row, logging
    // registry_'s resulting entity count + every named entity's own
    // position after EACH individual call -- not just the final state --
    // so a headless run's own log is a step-by-step proof the command stack
    // is unwinding/rewinding correctly, not just a single before/after
    // snapshot.
    if (frameCount_ == kDebugUndoFrame && debugUndoCount_ > 0) {
        LOG_INFO("ENGINE_DEBUG_UNDO=" + std::to_string(debugUndoCount_) + ": calling undo() " +
                  std::to_string(debugUndoCount_) + " time(s) at frame " + std::to_string(frameCount_));
        for (int step = 1; step <= debugUndoCount_; ++step) {
            undo();
            std::size_t entityCount = 0;
            std::string positions;
            registry_.each<NameComponent>([&](EntityId id, NameComponent& nameComponent) {
                ++entityCount;
                const Transform* t = registry_.getComponent<Transform>(id);
                if (t != nullptr) {
                    positions += "; " + nameComponent.name + "=(" + std::to_string(t->position().x) + ", " +
                                 std::to_string(t->position().y) + ", " + std::to_string(t->position().z) + ")";
                }
            });
            LOG_INFO("  after undo() #" + std::to_string(step) + ": " + std::to_string(entityCount) +
                      " named entities" + positions);
        }
    }
    if (frameCount_ == kDebugRedoFrame && debugRedoCount_ > 0) {
        LOG_INFO("ENGINE_DEBUG_REDO=" + std::to_string(debugRedoCount_) + ": calling redo() " +
                  std::to_string(debugRedoCount_) + " time(s) at frame " + std::to_string(frameCount_));
        for (int step = 1; step <= debugRedoCount_; ++step) {
            redo();
            std::size_t entityCount = 0;
            std::string positions;
            registry_.each<NameComponent>([&](EntityId id, NameComponent& nameComponent) {
                ++entityCount;
                const Transform* t = registry_.getComponent<Transform>(id);
                if (t != nullptr) {
                    positions += "; " + nameComponent.name + "=(" + std::to_string(t->position().x) + ", " +
                                 std::to_string(t->position().y) + ", " + std::to_string(t->position().z) + ")";
                }
            });
            LOG_INFO("  after redo() #" + std::to_string(step) + ": " + std::to_string(entityCount) +
                      " named entities" + positions);
        }
    }

    // Phase 18i: ENGINE_DEBUG_SAVE_SCENE_AS/ENGINE_DEBUG_NEW_SCENE/
    // ENGINE_DEBUG_OPEN_SCENE's own scripted frames -- see
    // kDebugSaveSceneAsFrame/kDebugNewSceneFrame/kDebugOpenSceneBaseFrame's
    // own comment above for the exact timeline, and each function's own
    // application.hpp comment for what it does. `logSceneState` mirrors the
    // ENGINE_DEBUG_UNDO/_REDO blocks' own per-entity position log just
    // above (a small local lambda, not a fourth near-identical copy of it),
    // logged after EVERY one of this phase's own scripted steps so a
    // headless run's log is a genuine step-by-step trace -- entity count AND
    // currentScenePath_ at each transition -- not just a final snapshot.
    const auto logSceneState = [this](const std::string& label) {
        std::size_t entityCount = 0;
        std::string names;
        registry_.each<NameComponent>([&](EntityId /*id*/, NameComponent& nameComponent) {
            ++entityCount;
            names += "; " + nameComponent.name;
        });
        LOG_INFO("  " + label + ": " + std::to_string(entityCount) + " named entities" + names +
                  "; currentScenePath_=\"" + currentScenePath_ + "\"");
    };

    if (frameCount_ == kDebugSaveSceneAsFrame && !debugSaveSceneAsName_.empty()) {
        LOG_INFO("ENGINE_DEBUG_SAVE_SCENE_AS=\"" + debugSaveSceneAsName_ + "\": calling saveSceneAs() at frame " +
                  std::to_string(frameCount_));
        saveSceneAs(debugSaveSceneAsName_);
        logSceneState("after saveSceneAs()");
    }
    if (frameCount_ == kDebugNewSceneFrame && debugNewSceneRequested_) {
        LOG_INFO("ENGINE_DEBUG_NEW_SCENE: calling newScene() at frame " + std::to_string(frameCount_));
        newScene();
        logSceneState("after newScene()");
    }
    for (std::size_t i = 0; i < debugOpenSceneNames_.size(); ++i) {
        const std::uint64_t stepFrame = kDebugOpenSceneBaseFrame + kDebugOpenSceneFrameSpacing * i;
        if (frameCount_ == stepFrame) {
            LOG_INFO("ENGINE_DEBUG_OPEN_SCENE: calling openScene(\"" + debugOpenSceneNames_[i] +
                      "\") at frame " + std::to_string(frameCount_) + " (entry " + std::to_string(i + 1) + "/" +
                      std::to_string(debugOpenSceneNames_.size()) + ")");
            openScene(debugOpenSceneNames_[i]);
            logSceneState("after openScene(\"" + debugOpenSceneNames_[i] + "\")");
        }
    }

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
    } else if (cameraCaptured_) {
        // Phase 16: real free-fly input -- WASD + Space/Shift (or E/Q) move
        // the camera, scaled by deltaTime so speed is frame-rate
        // independent; mouse-look reads the absolute cursor position each
        // frame and lets Camera derive its own delta. `input` is the
        // InputState run() already polled from window_ once this frame (see
        // input.hpp) -- Camera itself no longer touches window_ directly.
        //
        // Now gated on cameraCaptured_ -- the fix this whole phase exists
        // for. Before this phase, this call ran UNCONDITIONALLY every frame,
        // which was the actual reported bug: WASD/mouse moved the camera
        // even when the user was clicking elsewhere in the window or just
        // passing the mouse over the app with no intent to fly the camera at
        // all. Camera input is now inactive by default; a Viewport
        // double-click (editor_ui.cpp) or ENGINE_DEBUG_FORCE_CAMERA_CAPTURE
        // (this class's own constructor) is what sets cameraCaptured_ true,
        // via setCameraCaptured() -- see that method's own application.hpp
        // comment. Under Xvfb there's still no real input device driving
        // any of this even when captured -- every InputState flag is false
        // and the cursor position never changes -- so
        // ENGINE_DEBUG_FORCE_CAMERA_CAPTURE alone still leaves the camera at
        // its constructor-set default pose during headless verification,
        // exactly as every prior phase's own uncaptured baseline already
        // did; it proves this call path RUNS without crashing, not that it
        // visibly moves the camera (see README.md's own Phase 16 section for
        // exactly what headless verification could and couldn't cover here).
        camera_.processMovement(input, static_cast<float>(deltaTime));
        camera_.processMouseInput(input.cursorX, input.cursorY);
    }
    // else: not captured (the default state) and neither demo mode is
    // active -- camera input is simply inactive this frame, the documented
    // "do nothing" case this whole phase's brief asks for. No call to
    // resetMouseTracking() is needed here on every uncaptured frame -- only
    // exactly once, at the moment capture actually toggles (see
    // setCameraCaptured()) -- calling it every uncaptured frame would be
    // harmless but pointless churn for a per-frame hot path.
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

        registry_.each<ModelComponent>([&](EntityId id, ModelComponent& mc) {
            if (mc.model) {
                // Phase 14b: a parented entity's shadow must be cast from
                // its resolved WORLD position, not its raw local one (see
                // transform_hierarchy.hpp) -- otherwise a child would cast
                // its shadow as if it sat at its own local offset from the
                // scene origin, ignoring wherever its parent actually put
                // it.
                const glm::mat4 modelMatrix = resolveWorldMatrix(registry_, id);
                mc.model->drawDepthOnly(*shadowShader_, modelMatrix);
            }
        });

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

void Application::renderSSAO(const glm::mat4& view, const glm::mat4& projection) {
    // ssaoGBuffer_/ssaoRaw_/ssaoBlurred_ are all the same size (see this
    // class's Phase 13f header comment / kSSAODownsampleFactor's own
    // comment) -- that's the right size for uNoiseScale/uTexelSize below,
    // read from ssaoGBuffer_ itself (Framebuffer::width()/height()) rather
    // than re-deriving window_.getSize() / kSSAODownsampleFactor by hand.
    const int ssaoWidth = ssaoGBuffer_.width();
    const int ssaoHeight = ssaoGBuffer_.height();

    // 1) Geometry pre-pass: view-space normal + a real depth texture, single
    // sample, at ssaoGBuffer_'s own (downsampled) resolution -- see
    // kSSAODownsampleFactor's own comment for why not the window's full
    // resolution. Everything the main color pass draws is drawn again here
    // -- entities_, the ground plane, the PBR sphere grid -- exactly the
    // same "every drawable needs to appear in this auxiliary pass too"
    // situation renderShadowPass() above already has, just with a
    // normal+depth shader instead of a depth-only one.
    ssaoGBuffer_.bindForWriting();
    // Color-clears to a flat +Z-ish normal rather than black: any pixel this
    // pass's geometry never covers (i.e. the skybox/background, drawn by a
    // completely separate pass -- see render()) would otherwise read back
    // as a zero vector, which normalize() in ssao.frag would turn into NaN.
    // In practice ssao.frag never reaches that branch for those pixels at
    // all (its own depth >= 1.0 background check returns early first -- see
    // that shader), but clearing to a valid unit vector here costs nothing
    // and removes any dependence on that ordering staying that way.
    GL_CHECK(glClearColor(0.0f, 0.0f, 1.0f, 1.0f));
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    gbufferShader_->use();
    gbufferShader_->setMat4("uView", view);
    gbufferShader_->setMat4("uProjection", projection);

    registry_.each<ModelComponent>([&](EntityId id, ModelComponent& mc) {
        if (mc.model) {
            // Phase 14b: same resolved-WORLD-matrix requirement as the
            // shadow pass above -- the SSAO g-buffer's normal/depth for a
            // parented entity has to land at its actual world position, or
            // SSAO would sample occlusion for the wrong place entirely.
            const glm::mat4 modelMatrix = resolveWorldMatrix(registry_, id);
            mc.model->drawNormalDepth(*gbufferShader_, modelMatrix);
        }
    });

    {
        // The ground plane: identity model matrix, same as its main-pass
        // draw in render() below.
        const glm::mat4 groundModel(1.0f);
        const glm::mat3 groundNormalMatrix = glm::inverseTranspose(glm::mat3(groundModel));
        gbufferShader_->setMat4("uModel", groundModel);
        gbufferShader_->setMat3("uNormalMatrix", groundNormalMatrix);
        groundMesh_.bind();
        groundMesh_.draw();
    }

    sphereMesh_.bind();
    for (const SphereInstance& instance : sphereInstances_) {
        const glm::mat4 sphereModel = instance.transform.getModelMatrix();
        const glm::mat3 sphereNormalMatrix = glm::inverseTranspose(glm::mat3(sphereModel));
        gbufferShader_->setMat4("uModel", sphereModel);
        gbufferShader_->setMat3("uNormalMatrix", sphereNormalMatrix);
        sphereMesh_.draw();
    }

    // 2) Kernel pass: samples ssaoGBuffer_'s normal + depth, writes a raw,
    // per-pixel-noisy occlusion factor into ssaoRaw_ -- see ssao.frag.
    ssaoRaw_.bindForWriting();
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    ssaoShader_->use();
    ssaoGBuffer_.bindColorTexture(0);
    ssaoShader_->setInt("uNormalMap", 0);
    ssaoGBuffer_.bindDepthTexture(1);
    ssaoShader_->setInt("uDepthMap", 1);
    ssaoKernel_.bindNoiseTexture(2);
    ssaoShader_->setInt("uNoiseMap", 2);
    ssaoShader_->setMat4("uProjection", projection);
    ssaoShader_->setMat4("uInvProjection", glm::inverse(projection));
    // Tiles ssaoKernel_'s own kSSAONoiseDim x kSSAONoiseDim noise texture
    // across the SSAO kernel pass's own (downsampled) target -- see
    // ssao.frag's uNoiseMap sampling comment.
    ssaoShader_->setVec2("uNoiseScale", glm::vec2(static_cast<float>(ssaoWidth) / static_cast<float>(kSSAONoiseDim),
                                                    static_cast<float>(ssaoHeight) / static_cast<float>(kSSAONoiseDim)));
    ssaoShader_->setFloat("uRadius", kSSAORadius);
    ssaoShader_->setFloat("uBias", kSSAOBias);
    postProcessQuad_.bind();
    postProcessQuad_.draw();

    // 3) Blur pass: smooths ssaoRaw_'s per-pixel noise into ssaoBlurred_ --
    // what basic.frag/pbr.frag actually sample while shading (see render()).
    ssaoBlurred_.bindForWriting();
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    ssaoBlurShader_->use();
    ssaoRaw_.bindColorTexture(0);
    ssaoBlurShader_->setInt("uSSAOMap", 0);
    ssaoBlurShader_->setVec2("uTexelSize",
                              glm::vec2(1.0f / static_cast<float>(ssaoWidth), 1.0f / static_cast<float>(ssaoHeight)));
    // Half the noise texture's own tile size -- see ssao_blur.frag's own
    // comment on why this exactly cancels that texture's periodicity.
    ssaoBlurShader_->setInt("uBlurRadius", kSSAONoiseDim / 2);
    postProcessQuad_.bind();
    postProcessQuad_.draw();
}

void Application::renderSelectionMask(const glm::mat4& view, const glm::mat4& projection) {
    // Phase 18d: nothing selected, or the selected entity has no
    // ModelComponent to draw a silhouette from -- leave
    // selectionMaskFramebuffer_ completely untouched (still whatever it held
    // last frame it WAS drawn into, if ever) rather than clearing it here.
    // render()'s own uHasSelection uniform upload (postprocess.frag) is what
    // actually stops anything from ever sampling this possibly-stale buffer
    // in that case -- see this method's own application.hpp comment.
    if (!selectedEntity_.has_value()) {
        return;
    }
    const ModelComponent* selectedModel = registry_.getComponent<ModelComponent>(*selectedEntity_);
    if (selectedModel == nullptr || !selectedModel->model) {
        return;
    }

    selectionMaskFramebuffer_.bindForWriting();
    // Cleared to 0 every frame this pass actually runs -- selection_mask.frag
    // only ever writes 1.0 (never 0), so "0 unless explicitly marked
    // selected-and-visible" is exactly this clear's job, the same role
    // ssaoGBuffer_'s own clear plays for its pass just above.
    GL_CHECK(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    selectionMaskShader_->use();
    selectionMaskShader_->setMat4("uView", view);
    selectionMaskShader_->setMat4("uProjection", projection);
    // ssaoGBuffer_'s own depth texture -- already this frame's whole opaque
    // scene depth, from this same view/projection -- is the occlusion
    // reference selection_mask.frag discards against; see that shader's own
    // uSceneDepth comment for why reusing it (rather than a second, redundant
    // full-scene depth pre-pass) is both correct and free, exactly like SSR's
    // own ray march already reuses it (renderSSRComposite() below).
    ssaoGBuffer_.bindDepthTexture(0);
    selectionMaskShader_->setInt("uSceneDepth", 0);
    selectionMaskShader_->setVec2("uViewportSize",
                                   glm::vec2(static_cast<float>(selectionMaskFramebuffer_.width()),
                                             static_cast<float>(selectionMaskFramebuffer_.height())));
    selectionMaskShader_->setFloat("uDepthBias", kSelectionMaskDepthBias);

    // Phase 14b: the same resolved-WORLD-matrix requirement renderShadowPass()/
    // renderSSAO() already have for a parented entity -- this mask has to
    // land at the selected entity's actual DRAWN world position, or the
    // resulting outline would visibly disagree with where the entity is
    // actually rendered.
    const glm::mat4 worldMatrix = resolveWorldMatrix(registry_, *selectedEntity_);
    selectedModel->model->drawDepthOnly(*selectionMaskShader_, worldMatrix);
}

void Application::renderGizmo(const glm::mat4& view, const glm::mat4& projection) {
    // Phase 18e: no selection, or the selection has no Transform to place a
    // gizmo at -- draw nothing, matching this engine's established "no
    // selection => no [feature]" convention (the old 2D outline, Phase 18d's
    // real one, renderSelectionMask() just above). Unlike that mask pass,
    // this does NOT also require a ModelComponent -- an Empty entity (no
    // mesh, Transform + NameComponent only, e.g. Scene panel's Create >
    // Empty) is still a real, selectable, movable object in this engine, and
    // the project owner's own request ("move it from inside the scene") is
    // about position, which every selectable entity has regardless of
    // whether it renders a mesh.
    if (!selectedEntity_.has_value()) {
        return;
    }
    if (registry_.getComponent<Transform>(*selectedEntity_) == nullptr) {
        return;
    }

    // Phase 14b: the gizmo's own visual origin is the entity's actual
    // rendered WORLD position (resolveWorldMatrix()), not its bare local
    // Transform::position() -- identical for an unparented entity, and what
    // makes the gizmo visually sit ON a parented entity exactly where it's
    // actually drawn, the same reasoning renderSelectionMask()'s own Phase
    // 14b comment already gives for its own mask draw. EditorUI's own
    // updateGizmo() (editor_ui.cpp) independently derives this SAME world
    // position for hit-testing, so the two always agree on where the
    // pickable geometry actually is -- see gizmo.hpp's own header comment on
    // why a drag's resulting DELTA is still applied to the entity's LOCAL
    // position, only this rendering/hit-test origin uses the resolved world
    // one.
    const glm::mat4 worldMatrix = resolveWorldMatrix(registry_, *selectedEntity_);
    const glm::vec3 gizmoOrigin = glm::vec3(worldMatrix[3]);

    // Phase 18g: the eye position is recovered from `view` itself
    // (glm::inverse(view)'s own translation column) rather than read
    // straight from camera_ -- `view` is whichever camera value actually
    // rendered THIS frame (camera_'s own free-fly pose, or a scene Camera
    // entity's resolved pose during Play mode -- see render()'s own Phase
    // 18g comment), so deriving it from `view` keeps the gizmo's own
    // distance-based scaling correct under either, with no second camera
    // reference threaded into this method's own signature.
    const glm::vec3 eyePosition(glm::inverse(view)[3]);
    const float distanceToCamera = glm::length(eyePosition - gizmoOrigin);
    const float axisLength = gizmoAxisLength(distanceToCamera);

    // Editor chrome, not scene content -- always renders on top, regardless
    // of what else in the scene would otherwise occlude it (see this
    // method's own application.hpp comment for why, and gizmo.frag's own
    // comment for why this whole pass is flat/unlit like every real DCC
    // tool's own manipulation gizmo). Depth testing is explicitly disabled
    // for these three draws, not merely irrelevant: viewportColorFramebuffer_'s
    // own depth buffer, at this point in the pipeline, holds only the
    // postprocess fullscreen quad's own uniform near-plane depth
    // (postprocess.vert emits a fixed clip-space z of 0 for every fragment)
    // -- leaving depth testing ON here would fail nearly every gizmo
    // fragment against that flat plane rather than usefully self-occluding
    // against real scene geometry, none of which remains in this buffer by
    // this point (it is already the fully composited 2D image). Restored to
    // enabled immediately after -- this engine's own constructor-established
    // default (`glEnable(GL_DEPTH_TEST)`) every other pass, this frame and
    // every frame after it, relies on.
    GL_CHECK(glDisable(GL_DEPTH_TEST));

    gizmoShader_->use();
    gizmoShader_->setMat4("uView", view);
    gizmoShader_->setMat4("uProjection", projection);

    // Phase 18j: which of the two gizmo tools' shared geometry this frame's
    // three draws use -- gizmoRingMesh_ (mesh.hpp's makeGizmoRing()) needs
    // no rotation table of its own beyond the one already built for
    // gizmoArrowMesh_ below, since it deliberately lies in the local Y-Z
    // plane (its own normal along local +X), the identical local-+X
    // convention makeGizmoArrow() already uses -- see this method's own
    // updated application.hpp comment.
    Mesh& gizmoMesh = (gizmoMode_ == GizmoMode::kRotate) ? gizmoRingMesh_ : gizmoArrowMesh_;
    gizmoMesh.bind();

    struct GizmoAxisDraw {
        GizmoAxis axis;
        glm::vec3 color;
    };
    // Fixed X/Y/Z draw order -- with depth testing disabled (above), the
    // three handles are not self-occlusion-correct against each other from
    // every possible camera angle (a real concern only when looking nearly
    // straight down one axis, where that axis's own arrow/ring is
    // foreshortened to almost nothing on screen anyway); a gizmo needing
    // full mutual self-occlusion between its own three handles is real,
    // separate polish neither this phase nor Phase 18e takes on.
    const GizmoAxisDraw axisDraws[3] = {
        {GizmoAxis::kX, kGizmoAxisColorX},
        {GizmoAxis::kY, kGizmoAxisColorY},
        {GizmoAxis::kZ, kGizmoAxisColorZ},
    };
    for (const GizmoAxisDraw& axisDraw : axisDraws) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), gizmoOrigin);
        // Rotates makeGizmoArrow()'s/makeGizmoRing()'s own local +X onto
        // this axis's world direction (gizmo.hpp's gizmoAxisDirection()) --
        // hand-verified: rotating (1,0,0) by +90 deg about +Z gives
        // (0,1,0); by -90 deg about +Y gives (0,0,1); kX itself needs no
        // rotation at all. For the ring mesh specifically, this rotation
        // does exactly what its own local Y-Z-plane construction needs: the
        // X ring (no rotation) stays in the Y-Z plane; the Y ring (rotated
        // +90 about Z) ends up in the X-Z plane; the Z ring (rotated -90
        // about Y) ends up in the X-Y plane -- each one perpendicular to
        // its own axis, exactly the standard rotate-gizmo appearance.
        switch (axisDraw.axis) {
            case GizmoAxis::kY:
                model = glm::rotate(model, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
                break;
            case GizmoAxis::kZ:
                model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                break;
            case GizmoAxis::kX:
            case GizmoAxis::kNone:
            default:
                break;
        }
        // makeGizmoArrow()'s/makeGizmoRing()'s own default shapes are both
        // unit-scaled along/around local +X -- uniform-scaling by
        // axisLength here is what makes this frame's actual drawn geometry
        // agree with gizmoAxisLength()'s own value, the same one EditorUI's
        // updateGizmo()/updateGizmoRotate() use to build the pickable
        // geometry's own extent.
        model = glm::scale(model, glm::vec3(axisLength));

        gizmoShader_->setMat4("uModel", model);
        gizmoShader_->setVec3("uColor", axisDraw.color);
        gizmoMesh.draw();
    }

    GL_CHECK(glEnable(GL_DEPTH_TEST));
}

void Application::renderSSRComposite(const glm::mat4& view, const glm::mat4& projection) {
    // Redraws the PBR sphere grid a second time, into hdrFramebuffer_ --
    // the SAME target render()'s ordinary per-frame draw loop just finished
    // writing -- WITHOUT clearing it first, so every pixel this pass
    // doesn't touch (entities_, the ground plane, the skybox, and any
    // sphere pixel a later fade factor leaves unchanged) keeps exactly the
    // color the first pass already gave it. See this class's own Phase 13g
    // header comment for why a second pass is needed at all: pbr.frag's
    // screen-space ray march needs to sample a fully-rendered scene color
    // buffer, and hdrResolveFramebuffer_ now is one, having just been
    // resolved from the first pass by render()'s own call site right before
    // this method runs.
    //
    // GL_LEQUAL (not this engine's usual GL_LESS default) lets this pass's
    // fragments pass the depth test against the exact depth values the
    // first pass already wrote for this same geometry (identical vertices,
    // identical uModel/uView/uProjection, so identical gl_Position.z) --
    // the same "needs LEQUAL to redraw over what's already there" reason
    // skybox_.draw() uses it too (see skybox.cpp), for an unrelated reason
    // (there, matching a freshly-cleared 1.0 depth buffer). Restored to
    // GL_LESS immediately after, so it doesn't leak into next frame's
    // shadow/main passes, both of which rely on GL_LESS's ordinary
    // nearer-wins behavior.
    hdrFramebuffer_.bindForWriting();
    GL_CHECK(glDepthFunc(GL_LEQUAL));

    pbrShader_->use();
    // Every other scene-level uniform this program needs (projection,
    // lighting, shadow maps, IBL maps, screen size/cluster params) is still
    // live on pbrShader_ from render()'s own first-pass upload just above --
    // GL uniform state lives on the program object and isn't disturbed by
    // anything in between (see render()'s own comment on this same fact for
    // shader_/pbrShader_). Only this pass's own new inputs need uploading:
    // the view matrix (pbr.frag had no reason to work in view space before
    // this phase), the two SSR-specific textures, and the toggle itself.
    pbrShader_->setMat4("uView", view);
    hdrResolveFramebuffer_.bindColorTexture(kSSRColorBufferTextureUnit);
    pbrShader_->setInt("uSSRColorBuffer", static_cast<int>(kSSRColorBufferTextureUnit));
    ssaoGBuffer_.bindDepthTexture(kSSRDepthMapTextureUnit);
    pbrShader_->setInt("uSSRDepthMap", static_cast<int>(kSSRDepthMapTextureUnit));
    pbrShader_->setMat4("uSSRInvProjection", glm::inverse(projection));
    pbrShader_->setInt("uSSREnabled", 1);

    // Rebuilt here rather than threaded through as a parameter -- see this
    // method's own application.hpp comment for why (matches
    // renderShadowPass()/renderSSAO()'s "just the matrices it needs" shape).
    // Deterministically identical to render()'s own frustum this same
    // frame, so a sphere the first pass culled is culled here too.
    const glm::mat4 viewProjection = projection * view;
    const Frustum frustum(viewProjection);

    sphereMesh_.bind();
    for (const SphereInstance& instance : sphereInstances_) {
        const glm::mat4 sphereModel = instance.transform.getModelMatrix();
        const BoundingSphere sphereWorldSphere = sphereMesh_.boundingSphere().transformed(sphereModel);
        if (!frustum.intersects(sphereWorldSphere.center, sphereWorldSphere.radius)) {
            continue;
        }

        const glm::mat3 sphereNormalMatrix = glm::inverseTranspose(glm::mat3(sphereModel));
        pbrShader_->setMat4("uModel", sphereModel);
        pbrShader_->setMat3("uNormalMatrix", sphereNormalMatrix);
        instance.material.bind();
        sphereMesh_.draw();
    }

    // Defensive only -- render()'s own per-frame upload above already sets
    // uSSREnabled to 0 unconditionally at the top of every frame's first PBR
    // draw, before this method ever runs again next frame. Left explicitly
    // off here too so nothing could observe it still set to 1 if a later
    // phase ever adds another pbrShader_ draw call after this one within the
    // same frame.
    pbrShader_->setInt("uSSREnabled", 0);

    GL_CHECK(glDepthFunc(GL_LESS));
}

void Application::recomputeClusterAABBs() {
    const float aspect =
        viewportHeight_ != 0 ? static_cast<float>(viewportWidth_) / static_cast<float>(viewportHeight_) : 1.0f;
    const glm::mat4 projection = camera_.getProjectionMatrix(aspect);
    clusterLightCuller_.computeClusterAABBs(
        projection, camera_.nearPlane(), glm::vec2(static_cast<float>(viewportWidth_), static_cast<float>(viewportHeight_)));
}

void Application::resizeViewportTargetsIfNeeded() {
    // Phase 14c: editorUI_'s own last-recorded Viewport panel content-region
    // size (see editor_ui.hpp's own header comment on why this reads *last*
    // frame's size, not this frame's). 0 before renderDockspaceShell() has
    // ever run once -- in that case (and the much rarer case of a user
    // dragging a panel divider all the way shut mid-run), keep whatever
    // viewportWidth_/viewportHeight_ already held (seeded at window_'s own
    // initial size by this class's in-class default member initializers,
    // see application.hpp) rather than ever shrinking a render target to
    // 0x0 -- a 0-sized FBO is undefined/invalid in OpenGL. This is the
    // documented choice for that degenerate case: keep rendering at the
    // last known-good size (nothing visibly changes in the Viewport panel
    // that frame) rather than clamping to a jarring 1x1 image or skipping
    // the 3D render entirely and showing nothing.
    const int reportedWidth = editorUI_.viewportWidth();
    const int reportedHeight = editorUI_.viewportHeight();
    if (reportedWidth > 0 && reportedHeight > 0) {
        viewportWidth_ = reportedWidth;
        viewportHeight_ = reportedHeight;
    }
    // Defensive floor even after the above -- guards both the "never
    // reported yet" 0 case (if this class's own initial window size were
    // ever 0, which main.cpp already refuses) and any stray future caller
    // that assigns viewportWidth_/viewportHeight_ directly.
    viewportWidth_ = std::max(1, viewportWidth_);
    viewportHeight_ = std::max(1, viewportHeight_);

    // hdrFramebuffer_ is this class's own "are we already built at the
    // right size" reference point -- every Framebuffer in this resize group
    // is always rebuilt together (see application.hpp's own comment on this
    // group), so checking just the first one is exactly as reliable as
    // checking all of them and far cheaper than doing so every frame.
    if (hdrFramebuffer_.width() == viewportWidth_ && hdrFramebuffer_.height() == viewportHeight_) {
        return;
    }

    LOG_INFO("Viewport resized: rebuilding offscreen render targets at " + std::to_string(viewportWidth_) + "x" +
              std::to_string(viewportHeight_) + " (was " + std::to_string(hdrFramebuffer_.width()) + "x" +
              std::to_string(hdrFramebuffer_.height()) + ")");

    // Move-assigns a freshly constructed Framebuffer over each existing one
    // -- Framebuffer's own move-assignment operator (framebuffer.hpp) frees
    // the old GL handles before taking ownership of the new ones, so this
    // is exactly as safe as the constructor's own one-time construction,
    // just repeated. Same per-target size ratios as the constructor's own
    // initializer list above; downsampled dimensions go through the same
    // clampedDownsampleDimension() helper the constructor's own initializer
    // list now uses (this file, near kSSAODownsampleFactor) since
    // viewportWidth_/viewportHeight_ are only guaranteed >= 1 themselves,
    // and e.g. 1 / kBloomDownsampleFactor would otherwise floor to 0 -- a
    // live-resized value can legitimately reach this if a user shrinks the
    // Viewport panel down far enough.
    hdrFramebuffer_ = Framebuffer(viewportWidth_, viewportHeight_, kRequestedMsaaSamples);
    hdrResolveFramebuffer_ = Framebuffer(viewportWidth_, viewportHeight_, /*samples=*/0, /*depthAsTexture=*/false,
                                          /*mipmappedColor=*/true);
    const int bloomWidth = clampedDownsampleDimension(viewportWidth_, kBloomDownsampleFactor);
    const int bloomHeight = clampedDownsampleDimension(viewportHeight_, kBloomDownsampleFactor);
    brightFramebuffer_ = Framebuffer(bloomWidth, bloomHeight);
    pingpongFramebuffer0_ = Framebuffer(bloomWidth, bloomHeight);
    pingpongFramebuffer1_ = Framebuffer(bloomWidth, bloomHeight);
    const int ssaoWidth = clampedDownsampleDimension(viewportWidth_, kSSAODownsampleFactor);
    const int ssaoHeight = clampedDownsampleDimension(viewportHeight_, kSSAODownsampleFactor);
    ssaoGBuffer_ = Framebuffer(ssaoWidth, ssaoHeight, /*samples=*/0, /*depthAsTexture=*/true);
    ssaoRaw_ = Framebuffer(ssaoWidth, ssaoHeight);
    ssaoBlurred_ = Framebuffer(ssaoWidth, ssaoHeight);
    // Phase 18d: full viewport resolution, like viewportColorFramebuffer_
    // just below -- not downsampled, see this member's own application.hpp
    // comment.
    selectionMaskFramebuffer_ = Framebuffer(viewportWidth_, viewportHeight_);
    viewportColorFramebuffer_ = Framebuffer(viewportWidth_, viewportHeight_);

    // The cluster grid's AABBs are a pure function of the projection matrix
    // + screen size in pixels (see cluster_light_culler.hpp) -- both just
    // changed, so they must be rebuilt now, not left stale until whenever
    // this method next runs.
    recomputeClusterAABBs();
}

void Application::render() {
    // Phase 14c: reads/updates viewportWidth_/viewportHeight_ and rebuilds
    // every viewport-sized render target if they changed since the last
    // call -- must run before anything below that depends on either (the
    // aspect ratio just below, every renderX() call, every glViewport()/
    // Framebuffer::bindForWriting() this function makes). See this method's
    // own application.hpp comment.
    resizeViewportTargetsIfNeeded();

    const float aspect =
        viewportHeight_ != 0 ? static_cast<float>(viewportWidth_) / static_cast<float>(viewportHeight_) : 1.0f;

    // Phase 15b: this frame's actual directional-light direction/color --
    // resolveActiveDirectionalLight() (light.hpp) returns
    // activeDirectionalLight_'s own DirectionalLight component when one
    // genuinely exists (and its direction isn't degenerate), else
    // kLightDirection/kLightColor completely unchanged -- see that
    // function's own comment for exactly why/when each case applies, and
    // application.hpp's own activeDirectionalLight_ comment for the "most
    // recently created" rule that decides which entity (if any) gets to be
    // `activeDirectionalLight_` in the first place. Resolved once here,
    // BEFORE the cascades this same light drives are built below, and reused
    // by every uLightDirection/uLightColor upload further down this same
    // render() call (shader_, pbrShader_) -- so the shadow frustum and the
    // shading math can never disagree about which light this frame is
    // actually using, the same "compute once, reuse everywhere this frame"
    // discipline pointLightSamples (further down) already follows for point
    // lights.
    const DirectionalLight activeLight = resolveActiveDirectionalLight(
        registry_, activeDirectionalLight_.value_or(EntityId()), DirectionalLight{kLightDirection, kLightColor});

    // Phase 18g: this frame's active Camera entity (if any), resolved fresh
    // every frame -- see camera_component.hpp's own resolveActiveCamera()
    // comment for why this needs no persisted "which one" member the way
    // activeDirectionalLight_ does. `ignoredCount` is only ever nonzero for
    // a hand-edited/loaded scene JSON containing more than one Camera
    // entity (the Create menu's own Phase 18g change prevents this through
    // ordinary use, see editor_ui.cpp's own renderCreateEntityMenuItems()
    // comment) -- edge-triggered LOG_WARN, identical discipline to
    // pointLightOverflowActive_'s own collectPointLights()-overflow warning
    // above, rather than warning every single frame the condition persists.
    const ActiveCameraResolution activeCameraResolution = resolveActiveCamera(registry_);
    const bool cameraOverflowThisFrame = activeCameraResolution.ignoredCount > 0;
    if (cameraOverflowThisFrame && !cameraOverflowActive_) {
        LOG_WARN("Scene has " + std::to_string(activeCameraResolution.ignoredCount) +
                  " extra Camera entit" + (activeCameraResolution.ignoredCount == 1 ? "y" : "ies") +
                  " beyond the one this engine actually uses; only the entity with index " +
                  std::to_string(activeCameraResolution.active.index()) +
                  " (the first found by registry iteration order) is treated as the scene's active camera -- "
                  "the rest are ignored.");
    }
    cameraOverflowActive_ = cameraOverflowThisFrame;

    // Phase 18g: edge-triggered proof, in the log, that the Create menu's
    // "Camera" item is genuinely BeginDisabled()'d -- see
    // createMenuCameraItemDisabled_'s own application.hpp comment for why
    // this exists at all (a headless run has no other way to observe a
    // disabled popup MenuItem() without a real mouse to open the popup
    // with). `hasActiveCamera` here is the exact same value passed to
    // editorUI_.renderDockspaceShell() further down this same render() call
    // -- see that call site's own comment.
    const bool hasActiveCamera = activeCameraResolution.active.valid();
    if (hasActiveCamera != createMenuCameraItemDisabled_) {
        LOG_INFO(hasActiveCamera
                      ? "Camera entity present (index " + std::to_string(activeCameraResolution.active.index()) +
                            "): the Scene panel's Create menu \"Camera\" item is now disabled."
                      : "No Camera entity present: the Scene panel's Create menu \"Camera\" item is enabled again.");
        createMenuCameraItemDisabled_ = hasActiveCamera;
    }

    // Phase 18g: Play mode (physicsRunning_) viewing through a scene Camera
    // entity replaces camera_ (the free-fly camera) as this frame's actual
    // render viewpoint -- see application.hpp's own updated camera_ comment
    // for the full precedence rule, and camera_component.hpp's own header
    // comment for why Edit mode is completely unaffected regardless of
    // whether a Camera entity exists. `activeCameraComponent` is looked up
    // once here and reused below (rather than a second getComponent() call)
    // -- non-null only when `physicsRunning_` even bothers to ask, since
    // Edit mode never needs it at all.
    const CameraComponent* activeCameraComponent =
        (physicsRunning_ && hasActiveCamera) ? registry_.getComponent<CameraComponent>(activeCameraResolution.active)
                                              : nullptr;
    const bool usingSceneCamera = activeCameraComponent != nullptr;

    // Phase 18g: while Play mode is actively viewing through a scene Camera
    // entity, there is no free-fly camera to fly -- the Phase 16 double-
    // click-to-capture gesture is disabled for the duration (this engine's
    // own confirmed precedent: Unity's Game view only ever shows what the
    // game camera sees, with no free-look). Releasing an EXISTING capture
    // the instant this becomes true (rather than merely refusing new capture
    // requests going forward) avoids leaving the OS cursor hidden/locked
    // with no camera left for it to actually control -- setCameraCaptured()
    // is a safe no-op when cameraCaptured_ is already false, so this costs
    // nothing on the overwhelmingly common frame this condition ISN'T newly
    // true. New capture requests are refused further down, where
    // cameraCaptureRequestPending_ is latched for next frame's
    // decideCameraCapture() call (see that call site's own Phase 18g
    // comment) -- not here, since EditorUI's own Viewport double-click
    // detection has already run further down in this SAME render() call by
    // the time `usingSceneCamera` is known for certain this frame, so there
    // is nothing to gate yet at this point.
    if (usingSceneCamera && cameraCaptured_) {
        setCameraCaptured(false);
        LOG_INFO("Play mode is now viewing through a scene Camera entity -- releasing free-fly camera capture "
                  "(there is no free-fly camera to fly while the game view is locked to the scene camera).");
    }

    // Phase 18g: `renderCamera` is what every "which viewpoint is this frame
    // actually rendered from" call below reads from -- camera_ unless
    // `usingSceneCamera`, in which case a fresh, temporary Camera value
    // built from the active entity's own resolved world pose +
    // CameraComponent optics. Building a REAL Camera value (rather than
    // threading a second view/projection pair by hand through every
    // render()/renderShadowPass()/computeCascades() call site below) is what
    // lets every one of those call sites stay completely unmodified from
    // Phase 18e -- computeCascades() already takes `const Camera&`, exactly
    // this type, so this is a drop-in substitution, not a parallel code
    // path. Camera::setPositionLookingAt() (camera.hpp, unchanged since
    // Phase 3) is what actually turns `pose.position`/`pose.lookTarget`
    // into the yaw/pitch pair Camera's own getViewMatrix() needs --
    // reusing it here is exactly what
    // resolveCameraWorldPose()'s own header comment already explains
    // `lookTarget` (a point, not a bare direction) was shaped for.
    //
    // One accepted limitation, matching engine::Camera's own established
    // "no roll" design (camera.hpp's own header comment): a Camera entity's
    // world rotation ROLL (rotation around its own forward axis) is not
    // representable by a yaw/pitch Camera and is silently dropped here,
    // exactly as it already would be if a user tried to roll the free-fly
    // camera_ itself -- there is no roll anywhere in this engine's camera
    // model, Play mode included.
    Camera sceneRenderCamera(camera_.position());
    if (usingSceneCamera) {
        const glm::mat4 activeCameraWorldMatrix = resolveWorldMatrix(registry_, activeCameraResolution.active);
        const CameraWorldPose pose = resolveCameraWorldPose(activeCameraWorldMatrix);
        sceneRenderCamera.setPositionLookingAt(pose.position, pose.lookTarget);
        sceneRenderCamera.setFov(activeCameraComponent->fovYDeg);
        sceneRenderCamera.setClipPlanes(activeCameraComponent->nearPlane, activeCameraComponent->farPlane);
    }
    const Camera& renderCamera = usingSceneCamera ? sceneRenderCamera : camera_;

    // Phase 18g: this frame's actual shading mode -- editShadingMode_ in
    // Edit mode, unconditionally ShadingMode::kRendered whenever
    // physicsRunning_ is true -- see application.hpp's own editShadingMode_
    // comment and shading_mode.hpp's own effectiveShadingMode() for why this
    // one call is the entirety of "Play mode always forces Rendered,
    // restoring Edit's prior choice on return" with no save/restore step of
    // its own. `wireframeThisFrame` gates the polygon-mode switch around the
    // main color pass below; `skipHeavyPassesThisFrame` gates SSAO/SSR (both
    // Wireframe and Solid) and the selection-mask pass (which depends on
    // SSAO's own depth pre-pass, see renderSelectionMask()'s own comment) --
    // see this function's own further-down comments at each gated call site
    // for exactly why each one is or isn't skipped.
    const ShadingMode effectiveModeThisFrame = effectiveShadingMode(physicsRunning_, editShadingMode_);
    const bool wireframeThisFrame = effectiveModeThisFrame == ShadingMode::kWireframe;
    const bool skipHeavyPassesThisFrame = effectiveModeThisFrame != ShadingMode::kRendered;

    // Phase 13c: the camera's own aspect ratio (computed above) feeds
    // computeCascades()'s per-cascade sub-frustum construction (see
    // Camera::getProjectionMatrix's near/far overload), so cascades must be
    // (re)computed here, fresh every frame, same as the frustum/view/
    // projection matrices below -- never cached across frames. Phase 15b:
    // built from activeLight.direction (above), not kLightDirection
    // directly any more -- the two are identical whenever no Directional
    // Light entity is active, so this is a no-op change for every scene that
    // doesn't use this phase's new feature (see this engine's own default
    // scene, still zero Directional Light entities). Phase 18g: built from
    // `renderCamera`, not `camera_` directly any more -- identical whenever
    // Play mode isn't viewing through a scene Camera entity (the two are the
    // same object in that case), so this too is a no-op change for every
    // frame that doesn't use this phase's new feature.
    const std::array<Cascade, kCascadeCount> cascades =
        computeCascades(renderCamera, aspect, glm::normalize(activeLight.direction));
    std::array<glm::mat4, kCascadeCount> lightSpaceMatrices{};
    for (int i = 0; i < kCascadeCount; ++i) {
        lightSpaceMatrices[static_cast<std::size_t>(i)] = cascades[static_cast<std::size_t>(i)].lightSpaceMatrix;
    }

    // The directional light's cascaded shadow maps are rendered first, into
    // their own depth-only FBOs/viewport; glViewport is restored to the
    // viewport's own render resolution immediately after (Phase 14c: no
    // longer the window's real framebuffer size -- see this function's own
    // Phase 14c comment above), since renderShadowPass() leaves it pointed
    // at whichever cascade it wrote to last (all the same, generally
    // different, resolution).
    //
    // Phase 18g: skipped entirely in Wireframe mode -- "no meaningful
    // surface data for SSAO/SSR/shadows" (this phase's own confirmed brief)
    // -- rather than run against a mode with no filled faces at all. Kept
    // for Solid mode (shadows are part of "basic lighting," which Solid
    // mode explicitly keeps -- see application.cpp's own Phase 18g comment
    // further down on the main color pass for the full Solid-mode rationale).
    if (!wireframeThisFrame) {
        renderShadowPass(lightSpaceMatrices);
        GL_CHECK(glViewport(0, 0, viewportWidth_, viewportHeight_));
    }

    const glm::mat4 view = renderCamera.getViewMatrix();
    const glm::mat4 projection = renderCamera.getProjectionMatrix(aspect);

    // Phase 13f: SSAO's own three screen-space passes -- run here (after the
    // shadow pass, before the main color pass) so shader_/pbrShader_ below
    // have a finished, blurred occlusion texture (ssaoBlurred_) ready to
    // sample while shading the scene. Needs this frame's own view/projection
    // (just moved above, ahead of where they used to be computed) the same
    // way the shadow pass above needs this frame's own light-space matrices.
    // renderSSAO() leaves the last-bound FBO pointed at ssaoBlurred_ (not
    // the window) and the viewport at ssaoBlurred_'s own (downsampled, see
    // kSSAODownsampleFactor) resolution -- hdrFramebuffer_.bindForWriting()
    // right below rebinds both correctly before anything draws into the
    // window's own framebuffer chain.
    //
    // Phase 18g: skipped entirely for Wireframe AND Solid mode
    // (`skipHeavyPassesThisFrame`) -- there is no meaningful surface data
    // for a screen-space technique to reconstruct occlusion from once the
    // scene isn't being drawn with its ordinary filled/textured faces (this
    // phase's own confirmed brief, for Wireframe explicitly; Solid mode
    // extends the same skip since this project's own established preference
    // is the SIMPLER choice when either is defensible -- see this project's
    // physics.hpp restraint-in-scope precedent this phase's own brief cites
    // -- rather than separately verifying SSAO looks correct against
    // untextured Solid-mode geometry). uSSAOEnabled (below) is what actually
    // stops shader_/pbrShader_ from ever SAMPLING ssaoBlurred_'s possibly-
    // stale contents in that case -- not skipping this call alone.
    if (!skipHeavyPassesThisFrame) {
        renderSSAO(view, projection);
    }

    // Phase 18d: the selection mask pass -- depends only on ssaoGBuffer_'s
    // depth texture (just finished above), not on hdrFramebuffer_'s own main
    // color pass, so it runs here, grouped with this frame's other pre-passes,
    // rather than after the main scene draw. Reads selectedEntity_ as it
    // stood BEFORE editorUI_.renderDockspaceShell() runs later this same
    // call -- the same one-frame latency selectedEntity_'s own
    // application.hpp comment already documents. Like renderSSAO() just
    // above, leaves the last-bound FBO pointed at selectionMaskFramebuffer_
    // (or, when this frame has no selection, doesn't touch FBO bindings at
    // all) -- hdrFramebuffer_.bindForWriting() right below rebinds correctly
    // either way.
    //
    // Phase 18g: also skipped whenever renderSSAO() (just above) was --
    // renderSelectionMask()'s own occlusion test reads ssaoGBuffer_'s depth
    // texture (Phase 18d's own design), which only renderSSAO() ever
    // refreshes; running this against a Wireframe/Solid frame's STALE
    // ssaoGBuffer_ (whatever a previous Rendered frame left behind) would be
    // exactly the "garbage/line-rasterized G-buffer content" this phase's
    // own brief warns against for SSAO/SSR, just one step removed. The
    // selection outline is therefore a Rendered-mode-only feature as of this
    // phase -- `hasSelectionOutline` further down in this same function
    // gates postprocess.frag's own sampling on the identical
    // `skipHeavyPassesThisFrame` condition, so a stale mask is never actually
    // composited even on the very first Wireframe/Solid frame.
    if (!skipHeavyPassesThisFrame) {
        renderSelectionMask(view, projection);
    }

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

    // Phase 15a: this frame's full point light list -- kPointLights' 3 fixed
    // entries (application.cpp's own hand-tuned table, completely unchanged
    // from every prior phase -- see that table's own comment) seeded first,
    // then any live PointLight ECS entities appended by collectPointLights()
    // (light.hpp), up to kMaxPointLights total. Rebuilt fresh every frame,
    // unlike kSpotLights below (still a real function-local `static` --
    // spot lights stay a fixed compile-time table this phase, see
    // editor_ui.cpp's own Phase 15a comment on why Directional Light/Camera,
    // and by extension spot lights, aren't part of this phase's scope):
    // ECS point lights CAN be created (Scene panel's Create menu) or deleted
    // mid-run, so caching this list once and reusing it every frame the way
    // the pre-Phase-15 code did for the now-immutable kPointLights would
    // silently miss every light created/deleted after the very first frame.
    // Declared here (not inside the cluster-culling block below) so both
    // the shader_/pbrShader_ upload loops further down in this same
    // render() call can reuse this exact same list too, rather than each
    // recomputing it (or the cluster culler and the shader uploads
    // silently disagreeing about which lights exist this frame).
    std::vector<PointLightSample> pointLightSamples(kPointLights.begin(), kPointLights.end());
    // collectPointLights() itself is pure logic with no memory of prior
    // calls (see light.cpp's own header comment) -- it only reports whether
    // THIS call had to skip an entity for being over kMaxPointLights. The
    // edge-detection (warn only the frame overflow is first entered, stay
    // silent every subsequent frame it persists, warn again if it clears
    // and later re-triggers) happens here instead, against
    // pointLightOverflowActive_ (application.hpp) -- this Application's own
    // registry_-scoped memory of the previous frame's result, replacing
    // what used to be a collectPointLights()-local static that was wrongly
    // shared across every registry any caller passed in, not just this
    // one's.
    const bool pointLightsOverflowedThisFrame = collectPointLights(registry_, kMaxPointLights, pointLightSamples);
    if (pointLightsOverflowedThisFrame && !pointLightOverflowActive_) {
        LOG_WARN("collectPointLights: more point lights exist than maxTotal (" + std::to_string(kMaxPointLights) +
                  ") supports; extras are not rendered");
    }
    pointLightOverflowActive_ = pointLightsOverflowedThisFrame;

    // Phase 13d: re-cull every light against every cluster's (already-built,
    // see the constructor) AABB using this frame's own view matrix --
    // must run every frame the camera might have moved (effectively always
    // in this engine, see clusterLightCuller_'s own header comment). The
    // spot light table is converted to ClusterLightCuller's plain
    // (position/color/attenuation) input form once (function-local static --
    // kSpotLights never changes, so there's no reason to redo this
    // conversion every frame); the point light list above is converted
    // fresh every frame instead, for the same "it can change mid-run" reason
    // its own comment just gave.
    {
        const std::vector<ClusterLightInput> pointLightInputs = pointLightSamplesToClusterInputs(pointLightSamples);
        static const std::array<ClusterLightInput, kSpotLights.size()> kSpotLightInputs =
            toClusterLightInputs(kSpotLights);
        clusterLightCuller_.cullLights(view, pointLightInputs.data(), pointLightInputs.size(),
                                        kSpotLightInputs.data(), kSpotLightInputs.size());

        // Phase 13d: periodic proof (not every frame -- a GPU->CPU
        // read-back is exactly the kind of stall a real per-frame hot path
        // should avoid) that the per-cluster light lists cullLights() just
        // built are actually varied, non-trivial data -- same log
        // frequency as Phase 13b's own frustum-culling summary line.
        if (frameCount_ % kCullLogFrameInterval == 0) {
            const ClusterOccupancyStats stats = clusterLightCuller_.readOccupancyStats();
            LOG_INFO("Clustered lighting: " + std::to_string(stats.occupiedClusters) + "/" +
                      std::to_string(stats.totalClusters) + " clusters occupied, avg " +
                      std::to_string(stats.averageLightsPerOccupiedCluster) + " lights/occupied cluster");
        }
    }

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
    // Phase 15b: activeLight.direction/.color (resolved above, before
    // cascades), not kLightDirection/kLightColor directly any more -- see
    // that resolution's own comment for why the two are identical whenever
    // no Directional Light entity is active.
    shader_->setVec3("uLightDirection", activeLight.direction);
    shader_->setVec3("uLightColor", activeLight.color);
    shader_->setVec3("uAmbientColor", kAmbientColor);
    // Phase 18g: renderCamera's own position, not camera_'s -- this feeds
    // every view-dependent lighting term below (specular halfway vectors,
    // Fresnel-adjacent terms in pbr.frag's own upload further down), so it
    // MUST match whichever eye position `view`/`projection` above were
    // actually built from, or every specular highlight would silently
    // reflect the free-fly camera_'s position while the image itself is
    // rendered from a completely different scene Camera entity.
    shader_->setVec3("uViewPos", renderCamera.position());

    // Phase 13d: clustered lighting's own per-frame uniforms -- basic.frag's
    // computeClusterIndex() needs the same screen size/near/far the compute
    // shaders built the cluster grid against (see
    // clusterLightCuller_.computeClusterAABBs()'s call site in
    // recomputeClusterAABBs()) to pick the same cluster for a given fragment
    // that its light list was actually culled against. Phase 14c: this is
    // viewportWidth_/viewportHeight_ now, not the window's real framebuffer
    // size -- the whole 3D pass below (and gl_FragCoord within it) runs at
    // the viewport's own resolution, matching what recomputeClusterAABBs()
    // itself now builds the cluster grid against.
    shader_->setVec2("uScreenSize", glm::vec2(static_cast<float>(viewportWidth_), static_cast<float>(viewportHeight_)));
    // Phase 18g: deliberately STILL camera_.nearPlane(), not
    // renderCamera.nearPlane() -- the cluster grid clusterLightCuller_ built
    // (recomputeClusterAABBs(), still built from camera_'s own projection --
    // see that method's own comment) has to stay self-consistent with
    // whatever near plane computeClusterIndex() (basic.frag) reconstructs a
    // fragment's cluster from, or the two would disagree about where each
    // cluster's depth slice boundaries fall. A scene Camera entity's own
    // near/far are used for its actual PROJECTION matrix above (`projection`)
    // and every shadow cascade split (computeCascades(), above); clustered
    // light culling accuracy under a scene Camera entity with different clip
    // planes than camera_'s is a known, minor, accepted limitation of this
    // phase -- this engine's fixed, small light count means a slightly
    // mismatched cluster lookup has no visible effect in practice, and
    // rebuilding the cluster grid itself per-frame for this one case would
    // be real, separate scope beyond what this phase's brief asks for.
    shader_->setFloat("uClusterNearPlane", camera_.nearPlane());
    shader_->setFloat("uClusterFarPlane", ClusterLightCuller::kClusterFarDistance);
    shader_->setInt("uClusterDebug", clusterDebugMode_ ? 1 : 0);

    // Phase 13f: SSAO's final blurred occlusion texture -- bound once here,
    // at a fixed unit both shader_ (this upload) and pbrShader_ (its own
    // identical upload below) point their own uSSAOMap sampler at, since
    // texture-unit bindings are global GL state, not per-program (same
    // reasoning the shadow cascades'/IBL maps' own single-bind-per-frame
    // uploads above already rely on). ssaoDisabled_ (ENGINE_SSAO_DISABLE)
    // forces uSSAOEnabled to 0 instead, making both shaders fall back to an
    // unconditional ssao = 1.0 -- see basic.frag/pbr.frag's own comment.
    // Phase 18g: `skipHeavyPassesThisFrame` forces the identical 0 -- renderSSAO()
    // itself didn't even run this frame in that case (see this function's
    // own comment at that call site), so ssaoBlurred_ holds stale contents
    // that must never actually be sampled.
    ssaoBlurred_.bindColorTexture(kSSAOMapTextureUnit);
    shader_->setInt("uSSAOMap", static_cast<int>(kSSAOMapTextureUnit));
    shader_->setInt("uSSAOEnabled", (ssaoDisabled_ || skipHeavyPassesThisFrame) ? 0 : 1);
    // Phase 18g: Solid mode's own "shaded, but no diffuse texture" flag --
    // see basic.frag's own uSolidShading comment. 0 (the ordinary, textured
    // Rendered-mode path) for every frame except an effective Solid one.
    shader_->setInt("uSolidShading", effectiveModeThisFrame == ShadingMode::kSolid ? 1 : 0);

    // Phase 7a: point/spot lights, uploaded as a live count + a fixed-size
    // array each frame (see basic.frag's uNumPointLights/uPointLights and
    // uNumSpotLights/uSpotLights) -- the standard forward-rendering
    // "fixed-size uniform array + count" pattern, so the shader only loops
    // over lights that actually exist rather than every array slot. Phase
    // 15: uPointLights now comes from this frame's own pointLightSamples
    // (kPointLights plus any live ECS point lights, see this function's
    // own Phase 15a comment above), not straight from kPointLights.
    shader_->setInt("uNumPointLights", static_cast<int>(pointLightSamples.size()));
    for (std::size_t i = 0; i < pointLightSamples.size(); ++i) {
        uploadPointLight(*shader_, i, pointLightSamples[i]);
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
    // entity's Transform component * (accumulated parent node transform) *
    // (node's own local transform), and binding + drawing each node's
    // mesh(es) with their own Material. Iterating registry_'s ModelComponent
    // pool (rather than drawing one hardcoded model_) is what lets this loop
    // draw however many entities registry_ actually holds -- two by default
    // as of Phase 8e (the static scene.obj model and the falling cube), one
    // under ENGINE_LEGACY_SCENE -- without this call site itself needing to
    // change as that count does. Phase 8a: previously `for (const Entity&
    // entity : entities_)`, see this class's own Phase 8a header comment and
    // ecs.hpp.
    //
    // Phase 18g: Wireframe mode switches GL_FILL to GL_LINE for exactly this
    // main color pass -- entities_/the ground plane/the PBR sphere grid
    // below, restored to GL_FILL again right after the sphere loop, BEFORE
    // skybox_.draw() -- the simplest standard technique for a real wireframe
    // view (this phase's own confirmed brief), and no new shader is needed
    // for it: shader_/pbrShader_ still run their ordinary fragment shaders
    // per rasterized LINE fragment instead of per filled triangle, so
    // Wireframe's edges are still lit/shadowed exactly like Rendered mode's
    // filled faces would be. Deliberately scoped narrowly to just this block
    // (not set earlier, before the shadow/SSAO pre-passes) -- those two are
    // skipped OUTRIGHT in Wireframe mode already (see this function's own
    // comments at each call site), so there is no "wireframe-rasterized
    // garbage pre-pass data" risk for a narrower scope to avoid; skybox_/the
    // postprocess tonemap quad/renderGizmo()'s own flat-shaded triangles
    // below must never be line-rasterized (a GL_LINE fullscreen quad would
    // draw only its two diagonal edges, breaking the entire tonemap
    // composite), which is exactly why the reset back to GL_FILL happens
    // immediately after the sphere loop, well before any of those run.
    if (wireframeThisFrame) {
        GL_CHECK(glPolygonMode(GL_FRONT_AND_BACK, GL_LINE));
    }
    registry_.each<ModelComponent>([&](EntityId id, ModelComponent& mc) {
        if (mc.model) {
            // Phase 14b: the main color pass draws every entity at its
            // resolved WORLD matrix, not its raw local one -- see
            // transform_hierarchy.hpp's own header comment for why a
            // parented entity's actual position is parentWorldMatrix *
            // thisEntity'sLocalMatrix, recursively up the chain, rather
            // than the child's local Transform alone. An entity with no
            // Parent component (every entity before this phase, and most
            // after it) resolves to exactly its own getModelMatrix(), so
            // this is behavior-preserving for the whole scene except the
            // one new parented demo entity assets/scenes/default.json adds.
            const glm::mat4 modelMatrix = resolveWorldMatrix(registry_, id);
            // Phase 15f: this is the ONE call site where a per-entity
            // MaterialOverride (material_override.hpp) actually reaches the
            // rendered frame -- resolveDiffuseTextureOverride() returns
            // non-null only for `id` itself (never for another entity that
            // happens to share `mc.model` with it, since that pool lookup is
            // keyed by `id`), so passing its result into THIS entity's own
            // draw() call cannot affect any other entity's own draw() call
            // later in this same each<ModelComponent>() loop -- see
            // material_override.hpp's own header comment for the full
            // "shared cache stays untouched" design this depends on.
            const Texture* diffuseOverride = resolveDiffuseTextureOverride(registry_, id);
            mc.model->draw(*shader_, modelMatrix, &frustum, &cullStats, diffuseOverride);
        }
    });

    // Phase 7a's ground plane: drawn directly (not through registry_/
    // ModelComponent, see this class's header comment) with an identity
    // model matrix, since
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
    // view position, every point/spot light, and the shadow map) are the
    // exact same values already uploaded to shader_ above; they're
    // re-uploaded here onto pbrShader_ because GL uniform state lives on
    // each program object independently -- switching the active program via
    // use() does not carry shader_'s uniform values over to pbrShader_. No
    // uAmbientColor upload here (unlike shader_ below) -- Phase 10 replaced
    // pbr.frag's flat ambient placeholder with real IBL, which doesn't read
    // it (see pbr.frag's own uIrradianceMap/uPrefilterMap/uBrdfLUT uploads
    // further down).
    pbrShader_->use();
    pbrShader_->setMat4("uView", view);
    pbrShader_->setMat4("uProjection", projection);
    uploadCascades(*pbrShader_, cascades);
    // Phase 15b: same activeLight.direction/.color shader_ already got above
    // -- see that upload's own comment.
    pbrShader_->setVec3("uLightDirection", activeLight.direction);
    pbrShader_->setVec3("uLightColor", activeLight.color);
    // Phase 18g: see shader_'s identical uViewPos upload above for why this
    // is renderCamera, not camera_.
    pbrShader_->setVec3("uViewPos", renderCamera.position());

    // Phase 13d: see shader_'s identical upload above (Phase 14c: also
    // viewportWidth_/viewportHeight_ now, same reasoning).
    pbrShader_->setVec2("uScreenSize",
                         glm::vec2(static_cast<float>(viewportWidth_), static_cast<float>(viewportHeight_)));
    // Phase 18g: deliberately still camera_.nearPlane() -- see shader_'s
    // identical upload above for why.
    pbrShader_->setFloat("uClusterNearPlane", camera_.nearPlane());
    pbrShader_->setFloat("uClusterFarPlane", ClusterLightCuller::kClusterFarDistance);
    pbrShader_->setInt("uClusterDebug", clusterDebugMode_ ? 1 : 0);

    // Phase 13f: same texture unit shader_'s own upload above already bound
    // ssaoBlurred_ to -- see that upload's comment. Phase 18g:
    // `skipHeavyPassesThisFrame` forces this off too -- see shader_'s own
    // identical upload above.
    pbrShader_->setInt("uSSAOMap", static_cast<int>(kSSAOMapTextureUnit));
    pbrShader_->setInt("uSSAOEnabled", (ssaoDisabled_ || skipHeavyPassesThisFrame) ? 0 : 1);
    // Phase 18g: same Solid-mode flag shader_'s own upload above already
    // sets -- see basic.frag's/pbr.frag's own uSolidShading comment.
    pbrShader_->setInt("uSolidShading", effectiveModeThisFrame == ShadingMode::kSolid ? 1 : 0);

    // Phase 13g: off during this, the ordinary once-per-frame PBR draw --
    // pbr.frag's IBL-only specular term is computed exactly as Phase 10 left
    // it here. Only renderSSRComposite()'s own second draw of the sphere
    // grid (later in render(), after the whole scene is resolved) turns
    // this on -- see that method and pbr.frag's own uSSREnabled comment.
    pbrShader_->setInt("uSSREnabled", 0);

    // Phase 15a: same pointLightSamples as shader_'s own upload above --
    // see this function's own Phase 15a comment.
    pbrShader_->setInt("uNumPointLights", static_cast<int>(pointLightSamples.size()));
    for (std::size_t i = 0; i < pointLightSamples.size(); ++i) {
        uploadPointLight(*pbrShader_, i, pointLightSamples[i]);
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

    // Phase 18g: restored to GL_FILL immediately after the main color pass
    // (entities_/ground/PBR spheres) finishes, BEFORE skybox_.draw() -- see
    // this function's own comment where GL_LINE was set, above, for exactly
    // why this reset can't wait any later.
    if (wireframeThisFrame) {
        GL_CHECK(glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));
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
    // Phase 13g bug fix: rebuilds hdrResolveFramebuffer_'s mip chain from the
    // base level resolveTo() just wrote, immediately before renderSSRComposite()
    // below reads this exact texture via a dynamic, per-fragment-varying UV
    // (see that method's own comment, and Framebuffer::generateColorMipmaps()'s).
    // Must happen every frame right here, not once at startup: each frame's
    // resolveTo() overwrites the base level with this frame's own scene, so
    // last frame's mip chain is stale the instant that happens.
    hdrResolveFramebuffer_.generateColorMipmaps();

    // Phase 13g: Screen-Space Reflections -- see this class's own Phase 13g
    // header comment. Must run after the resolveTo() just above (needs a
    // real, sampler2D-compatible single-sample color buffer to ray-march
    // against -- a still-multisample hdrFramebuffer_ isn't one, see
    // framebuffer.hpp) and after skybox_.draw() (so that buffer holds the
    // whole opaque scene, background included). Re-resolves hdrFramebuffer_
    // a SECOND time afterward so hdrResolveFramebuffer_ -- what bloom/the
    // final tonemap pass read below -- reflects this pass's own blended
    // output, not just the first pass's pre-SSR one.
    //
    // ENGINE_SSR_DISABLE (ssrDisabled_) skips this entirely, leaving
    // hdrResolveFramebuffer_ exactly as the first pass alone produced it
    // (pbr.frag's IBL-only specular term, unchanged) -- see that env var's
    // own comment for why this exists (isolating SSR's own contribution for
    // headless before/after verification). Phase 18g: `skipHeavyPassesThisFrame`
    // skips it too, for both Wireframe and Solid mode -- see this function's
    // own comment at the SSAO call site above for the identical reasoning
    // (no meaningful surface data to ray-march against once the scene wasn't
    // shaded with its ordinary filled/textured faces this frame).
    if (!ssrDisabled_ && !skipHeavyPassesThisFrame) {
        renderSSRComposite(view, projection);
        hdrFramebuffer_.resolveTo(hdrResolveFramebuffer_);
        // Bug fix: resolveTo() above only overwrites hdrResolveFramebuffer_'s
        // base mip level (glBlitFramebuffer never touches anything past mip
        // 0) -- the mip chain generateColorMipmaps() built a few lines above
        // (right after the FIRST resolveTo(), for traceSSR()'s own textureGrad
        // reads) is now stale everywhere except mip 0: every mip above it
        // still holds this frame's PRE-SSR image. bloom extraction below
        // reads this same texture through ordinary GL_LINEAR_MIPMAP_LINEAR
        // minification (its target, brightFramebuffer_, is half this
        // texture's resolution -- see kBloomDownsampleFactor), which is
        // exactly the kind of read that lets the GPU pick a non-zero implicit
        // LOD from screen-space derivatives -- so a bright SSR reflection
        // (e.g. a smooth sphere mirroring a point light or the sun) can
        // silently fail to bloom, or bloom at its dimmer pre-SSR brightness,
        // even though the tonemap pass right after (which samples mip 0 only,
        // at 1:1 resolution with no minification) shows it correctly. Must
        // rebuild the chain again here so bloom's own downsample sees the
        // same post-SSR image the rest of this frame does.
        hdrResolveFramebuffer_.generateColorMipmaps();
    }

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

    // Phase 7b: resolve hdrResolveFramebuffer_'s HDR color buffer via one
    // fullscreen tonemap + gamma-correct pass -- see assets/shaders/
    // postprocess.vert/.frag. Both buffers are cleared first (color:
    // nothing else draws here so any prior frame's leftover pixels must go;
    // depth: this pass's own fullscreen quad is depth-tested against
    // whatever this target's depth buffer last held, which is otherwise
    // stale/unrelated to this frame) so this draw can't be silently
    // rejected by a leftover depth value from a previous frame.
    //
    // Phase 14c: this pass's own render target is now
    // viewportColorFramebuffer_ (viewportWidth_ x viewportHeight_), not the
    // default framebuffer at the window's real size -- bindForWriting()
    // binds its FBO and sets the viewport to its own size in one call,
    // mirroring every other Framebuffer target this function already writes
    // into (hdrFramebuffer_ etc. above). EditorUI's Viewport panel displays
    // this target's color texture via ImGui::Image() (see this function's
    // own tail, below) instead of this pass ever touching the window's real
    // framebuffer directly.
    //
    // MSAA HDR framebuffer bug fix: reads hdrResolveFramebuffer_ (this
    // frame's already-resolved, single-sample HDR color) rather than
    // hdrFramebuffer_ directly -- see the resolveTo() call above.
    viewportColorFramebuffer_.bindForWriting();
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
    // Phase 13f: ENGINE_SSAO_DEBUG -- see postprocess.frag's own comment.
    // ssaoRaw_ (not ssaoBlurred_) is what this shows: the pre-blur buffer is
    // the more useful one to inspect for this technique's classic failure
    // modes (per-pixel noise/banding from the rotation-noise tiling) since
    // the blur pass exists specifically to hide those in the final
    // composited image.
    ssaoRaw_.bindColorTexture(2);
    postProcessShader_->setInt("uSSAOMap", 2);
    postProcessShader_->setInt("uSSAODebug", ssaoDebugMode_ ? 1 : 0);
    // Phase 18d: the selection outline's own edge-detection composite -- see
    // postprocess.frag's own comment. `hasSelectionOutline` mirrors
    // renderSelectionMask()'s own "is there actually a selected entity with
    // a Model to draw" gate exactly (this method's own Phase 18d comment
    // above) -- when false, selectionMaskFramebuffer_ is bound anyway (unit
    // 3 has to be bound to SOMETHING for a well-defined draw call) but
    // uHasSelection stops postprocess.frag from ever sampling it, so its
    // possibly-stale contents (whatever the last real selection left behind)
    // can never influence this frame's output. This is what guarantees the
    // "nothing selected => pixel-identical to before this feature existed"
    // default this phase's own brief requires.
    // Phase 18g: also requires !skipHeavyPassesThisFrame -- renderSelectionMask()
    // itself didn't even run this frame in Wireframe/Solid mode (see this
    // function's own comment at that call site), so selectionMaskFramebuffer_
    // holds stale contents from whenever a Rendered frame last wrote it;
    // this is what stops postprocess.frag from ever sampling that stale
    // buffer, the identical role this same flag already plays for
    // uHasSelection's own pre-existing "nothing selected" case just above.
    const bool hasSelectionOutline = !skipHeavyPassesThisFrame && selectedEntity_.has_value() &&
                                      registry_.getComponent<ModelComponent>(*selectedEntity_) != nullptr &&
                                      registry_.getComponent<ModelComponent>(*selectedEntity_)->model != nullptr;
    selectionMaskFramebuffer_.bindColorTexture(3);
    postProcessShader_->setInt("uSelectionMask", 3);
    postProcessShader_->setVec2("uSelectionMaskTexelSize",
                                 glm::vec2(1.0f / static_cast<float>(selectionMaskFramebuffer_.width()),
                                           1.0f / static_cast<float>(selectionMaskFramebuffer_.height())));
    postProcessShader_->setInt("uHasSelection", hasSelectionOutline ? 1 : 0);
    postProcessShader_->setVec3("uSelectionOutlineColor", kSelectionOutlineColor);
    postProcessQuad_.bind();
    postProcessQuad_.draw();

    // Phase 18e: the translate gizmo -- painted directly on top of
    // viewportColorFramebuffer_ (still bound from the postprocess draw just
    // above), AFTER the tonemap/bloom/selection-outline composite rather
    // than earlier in the pipeline, since the gizmo is editor chrome, not
    // scene content: it must never cast/receive shadows, feed SSAO, or be
    // tonemapped/bloomed the way an actual scene object is (see
    // renderGizmo()'s own application.hpp comment).
    renderGizmo(view, projection);

    // Phase 14c: the 3D pipeline is entirely done for this frame -- its
    // final tonemapped output now lives in viewportColorFramebuffer_ (just
    // above), not the default framebuffer. Rebind the default framebuffer at
    // the window's own *real* size (window_.getSize(), not viewportWidth_/
    // viewportHeight_ -- this is ImGui's own chrome now, which always covers
    // the whole real window, not just the Viewport panel's sub-region) and
    // clear it -- there is no other draw call left this frame that touches
    // it directly (editorUI_.render() below draws ImGui's own geometry,
    // including the ImGui::Image() the Viewport panel now shows, but never
    // clears first), so any of this buffer's own leftover pixels from a
    // previous frame need to go, same "nothing else clears this" reasoning
    // the old direct-to-window postprocess draw relied on before this
    // phase. The clear color is an arbitrary neutral dark gray -- visible
    // only in the (thin, if any) gaps between docked panels, since the
    // Scene/Assets/Viewport/Inspector panels' own opaque backgrounds cover
    // the rest.
    {
        const auto [windowWidth, windowHeight] = window_.getSize();
        GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
        GL_CHECK(glViewport(0, 0, windowWidth, windowHeight));
        GL_CHECK(glClearColor(0.08f, 0.08f, 0.09f, 1.0f));
        GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    }

    // Phase 8c/14a: last thing render() does -- so every ImGui widget (both
    // editorUI_'s always-on dockspace shell and, when enabled, debugUI_'s
    // diagnostic panel) lands on top of everything else this frame drew.
    // One shared ImGui frame per render() call (see editor_ui.hpp's own
    // header comment for why there's exactly one, not one per UI class):
    // editorUI_.newFrame() starts it, editorUI_.renderDockspaceShell()
    // submits the Scene/Assets/Viewport/Inspector panels -- Viewport's own
    // body now an ImGui::Image() of viewportColorFramebuffer_'s color
    // texture (Phase 14c; see editor_ui.cpp) -- already carrying (Phase 18d)
    // the selection outline's own edge-detection composite, baked directly
    // into that same texture by the postprocess pass above, so EditorUI has
    // no separate overlay of its own left to draw for it any more -- and
    // Scene's own body a real, click-to-select tree (Phase 14d) instead
    // of placeholder text -- renderDebugUI() additionally submits debugUI_'s
    // own panel content (still a no-op unless ENGINE_SHOW_DEBUG_UI/F1 have
    // enabled it -- entirely unchanged from Phase 8c/8d), and
    // editorUI_.render() rasterizes everything submitted this frame in one
    // ImGui::Render() + ImGui_ImplOpenGL3_RenderDrawData() pair.
    //
    // Phase 14f: renderDockspaceShell() now RETURNS a CreateEntityKind (see
    // editor_ui.hpp's own comment) -- kNone every frame except the one where
    // the Scene panel's own Create menu had a real item clicked. Acted on
    // immediately below, via spawnEntityFromCreateMenu() (this class's own
    // Phase 14f comment, application.hpp) -- but, like a newly-clicked
    // selectedEntity_ change (see this method's own comment above), a
    // freshly created entity only actually appears in the Scene Hierarchy
    // tree / renders in the Viewport starting NEXT frame: this frame's own
    // Scene panel (just submitted, above) and 3D render pass (already run,
    // earlier in this same render() call) both already reflect registry_'s
    // PRE-creation state. Same one-frame latency this class already
    // documents for viewportWidth_/viewportHeight_ and the selection
    // outline, for the identical underlying reason -- there is no point in
    // this frame's fixed pipeline order left to insert a new entity that
    // would also still make it into this frame's already-submitted Scene
    // tree/already-finished 3D pass.
    //
    // Phase 16: io.ConfigFlags's own ImGuiConfigFlags_NoMouse toggled HERE,
    // immediately before newFrame() (which is what actually calls
    // ImGui::NewFrame() -- the point every frame where Dear ImGui computes
    // this frame's own hovered-window/clicked-item state from io.MousePos/
    // io.MouseDown) -- not after. Researched directly against this project's
    // actual vendored ImGui (build/_deps/imgui-src, v1.92.9b-docking per
    // CMakeLists.txt's own GIT_TAG), not assumed from general/possibly-stale
    // ImGui knowledge, per this phase's own brief: imgui_impl_glfw.cpp's own
    // CursorPosCallback()/MouseButtonCallback() do NOT special-case
    // GLFW_CURSOR_DISABLED at all (a 2022-09-01 change that ONCE ignored
    // mouse data under GLFW_CURSOR_DISABLED was explicitly reverted
    // 2023-07-18, per that file's own changelog header -- "User may set
    // ImGuiConfigFLags_NoMouse if desired" is that revert's own suggested
    // replacement) -- meaning every relative-motion delta GLFW's own
    // "disabled" cursor mode reports while captured would otherwise still
    // feed io.MousePos, and GLFW's virtual cursor position under
    // GLFW_CURSOR_DISABLED is explicitly unbounded (not clamped to the
    // window), so that fed position can end up anywhere, including
    // coincidentally back over the Inspector/Scene/Assets panels --
    // ImGuiConfigFlags_NoMouse (imgui.h: "Instruct dear imgui to disable
    // mouse inputs and interactions") is what stops Dear ImGui computing ANY
    // hover/click for ANY panel while that's happening, so the Inspector
    // etc. cannot receive a spurious click while the user is only trying to
    // fly the camera. (The Viewport's OWN double-click-to-CAPTURE check,
    // editor_ui.cpp, still works correctly despite this: that check only
    // ever runs while cameraCaptured_ is still false, i.e. before this same
    // flag would ever be set for the frame in question -- see this method's
    // own render()-order comment on cameraCaptureRequestPending_ in
    // application.hpp.) Separately, this vendored backend's own
    // ImGui_ImplGlfw_UpdateMouseCursor() already special-cases
    // GLFW_CURSOR_DISABLED on its OWN (checked directly, not assumed either):
    // `if (... || glfwGetInputMode(bd->Window, GLFW_CURSOR) ==
    // GLFW_CURSOR_DISABLED) { ...; return; }` -- so once window_.
    // setCursorCaptured(true) (window.hpp) has put GLFW itself into
    // GLFW_CURSOR_DISABLED mode, the backend already leaves that alone
    // rather than fighting to restore a visible cursor shape underneath it,
    // confirmed by that file's own 2026-03-25 changelog entry ("Mouse cursor
    // is properly restored if changed by user app/code while using
    // glfwSetInputMode(..., GLFW_CURSOR_DISABLED)... Amend change from
    // 2025-12-10") -- meaning ImGuiConfigFlags_NoMouseCursorChange is NOT
    // needed here on top of NoMouse: this specific, current vendored version
    // already gets the cursor-shape half of this right on its own, unlike
    // some older/other configurations this phase's own brief flagged as a
    // possible risk to check for.
    ImGuiIO& io = ImGui::GetIO();
    if (cameraCaptured_) {
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    } else {
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }

    editorUI_.newFrame();
    bool saveSceneRequested = false;
    std::optional<std::string> textureAssignRequested;
    // Phase 15g: EditorUI's own new out-parameter -- non-nullopt exactly the
    // frame a drag from the Assets panel is released over the Viewport's own
    // ImGui::Image() (see editor_ui.hpp's own Phase 15g comment and this
    // method's own handling further below).
    std::optional<std::string> assetDropRequested;
    // Phase 16: EditorUI's own new out-parameter -- true exactly the frame a
    // Viewport double-click requests entering camera capture (see
    // editor_ui.hpp's own Phase 16 comment). Not acted on directly here --
    // stored into cameraCaptureRequestPending_ below for run()'s own next
    // iteration to consume alongside that frame's fresh escapeJustPressed, via
    // decideCameraCapture() -- see that member's own application.hpp comment
    // for exactly why this has to cross the render()/run() boundary this way
    // rather than being handled in this same call the way
    // saveSceneRequested/textureAssignRequested/assetDropRequested are.
    bool cameraCaptureRequested = false;
    // Phase 17c: `ssaoDisabled_`/`ssaoDebugMode_` passed by reference below
    // -- the Viewport panel's own new toolbar row (editor_ui.cpp's
    // renderViewportToolbar()) reads/writes these two members directly, the
    // SAME state renderDebugUI()'s own F1-overlay "Render Passes" checkboxes
    // already bind by address (see this method's own Phase 8c comment) --
    // not a second, parallel toggle that could disagree with the debug
    // overlay's. See editor_ui.hpp's own Phase 17c comment on
    // renderDockspaceShell() for why these two, specifically, are passed
    // straight through rather than routed via an out-parameter-plus-later-
    // handling pair the way saveSceneRequested/textureAssignRequested above
    // are.
    // Phase 18b: `physicsRunning_` passed by reference the identical way,
    // right alongside them -- the toolbar's own real Play/Pause buttons
    // (editor_ui.cpp) mutate this member directly on a click, and
    // update()'s own `if (physicsRunning_) { stepPhysics(...); }` gate (see
    // that call site's own comment) re-reads it next frame, the same "no
    // out-parameter-plus-later-handling needed" reasoning ssaoDisabled_/
    // ssaoDebugMode_ above already established.
    // Phase 17d: `showCustomTitleBar`/`windowMaximized`/`windowPos` are
    // read-only snapshots of window_'s own live state, queried fresh this
    // same frame -- the identical "EditorUI only ever DISPLAYS state it
    // doesn't own" shape `cameraCaptured`/`activeDirectionalLight` above
    // already use. `showCustomTitleBar` is `!window_.isDecorated()` -- see
    // that accessor's own window.hpp comment for why the custom title bar
    // must NOT draw at all when the OS's own native one is already on (the
    // ENGINE_WINDOW_DECORATED=1 escape hatch), rather than drawing a
    // redundant second one alongside it. `titleBarAction` is EditorUI's own
    // new out-parameter for the custom title bar (see TitleBarAction's own
    // editor_ui.hpp comment); handled just below, the same "call
    // renderDockspaceShell(), then act on whatever it reported" shape
    // createRequest/saveSceneRequested/etc. already establish.
    TitleBarAction titleBarAction;
    // Phase 18e: `view`/`projection` -- the SAME matrices this frame's own
    // shadow/SSAO/selection-mask/gizmo passes above already used (computed
    // once, near the top of this function, from renderCamera's state as of
    // the START of this frame) -- and `renderCamera.position()` (Phase 18g:
    // was `camera_.position()` -- see that member's own updated
    // application.hpp comment for why this must be whichever camera value
    // actually rendered THIS frame), passed straight through as EditorUI's
    // own new `cameraPosition`/`cameraView`/`cameraProjection` parameters so
    // its gizmo hit-test (updateGizmo(), editor_ui.cpp) reasons about the
    // exact same camera state Application::renderGizmo() just drew the
    // gizmo's own pickable geometry with -- see that method's own header
    // comment for why the two must agree.
    //
    // Phase 18g: two more arguments -- `hasActiveCamera` (computed near the
    // top of this function; Create menu's own "at most one Camera entity"
    // enforcement, see editor_ui.cpp's own renderCreateEntityMenuItems()
    // comment) and `editShadingMode_` (replacing the old `ssaoDisabled_`/
    // `ssaoDebugMode_` pair here -- those two members are UNCHANGED, still
    // real, still owned by the F1 debug overlay's own checkboxes; this call
    // simply stops being a second way to reach them, see editor_ui.hpp's own
    // updated renderDockspaceShell() comment for the full story).
    // Phase 18h: five more locals -- `deleteEntityRequested`/
    // `transformEditCommitted` mirror saveSceneRequested/
    // textureAssignRequested's own "out-parameter, handled right below"
    // shape; `undoStack_.canUndo()`/`canRedo()` are read-only snapshots
    // (the identical by-value shape `cameraCaptured_`/`hasActiveCamera`
    // above already use); `undoRequested`/`redoRequested` mirror
    // `cameraCaptureRequested`'s own "false unless clicked this frame"
    // shape. See renderDockspaceShell()'s own updated editor_ui.hpp comment
    // for what each means.
    std::optional<EntityId> deleteEntityRequested;
    std::optional<Command> transformEditCommitted;
    bool undoRequested = false;
    bool redoRequested = false;
    // Phase 18i: three more locals -- `newSceneRequested`/`saveAsRequested`/
    // `openSceneRequested` mirror saveSceneRequested/textureAssignRequested's
    // own "out-parameter, handled right below" shape. `currentScenePath_`
    // itself is passed straight through, read-only, the identical by-value
    // shape `activeDirectionalLight_`/`hasActiveCamera` above already use
    // for state EditorUI only ever DISPLAYS. See renderDockspaceShell()'s
    // own updated editor_ui.hpp comment for what each new parameter means.
    bool newSceneRequested = false;
    std::optional<std::string> saveAsRequested;
    std::optional<std::string> openSceneRequested;
    const CreateEntityKind createRequest = editorUI_.renderDockspaceShell(
        viewportColorFramebuffer_.colorTextureId(), registry_, selectedEntity_, activeDirectionalLight_,
        hasActiveCamera, saveSceneRequested, textureAssignRequested, assetDropRequested, cameraCaptured_,
        cameraCaptureRequested, editShadingMode_, physicsRunning_, !window_.isDecorated(), window_.isMaximized(),
        window_.getWindowPos(), titleBarAction, renderCamera.position(), view, projection, deleteEntityRequested,
        transformEditCommitted, undoStack_.canUndo(), undoStack_.canRedo(), undoRequested, redoRequested,
        newSceneRequested, saveAsRequested, currentScenePath_, openSceneRequested, gizmoMode_);
    if (createRequest != CreateEntityKind::kNone) {
        spawnEntityFromCreateMenu(createRequest);
    }
    // Phase 18i: the File menu's own three new real trigger paths -- see
    // newScene()/saveSceneAs()/openScene()'s own application.hpp comments.
    // Order matters only in that these three are mutually exclusive within
    // a single ImGui frame (a user can click at most one menu item/popup
    // button per frame), so there's no meaningful interaction between them
    // to get wrong here.
    if (newSceneRequested) {
        newScene();
    }
    if (saveAsRequested.has_value()) {
        saveSceneAs(*saveAsRequested);
    }
    if (openSceneRequested.has_value()) {
        openScene(*openSceneRequested);
    }
    // Phase 18h: the Inspector's "Delete Object" button's own real trigger
    // path -- see editor_ui.hpp's own updated comment for why EditorUI only
    // ever reports which entity was clicked rather than destroying it
    // itself. deleteEntity() (this class's own new method) is what actually
    // captures the undo record and destroys it.
    if (deleteEntityRequested.has_value()) {
        deleteEntity(*deleteEntityRequested);
    }
    // Phase 18h: a completed Inspector Transform-field edit OR a completed
    // gizmo drag -- both report through this one out-parameter (see that
    // parameter's own editor_ui.hpp comment for why). The registry mutation
    // itself already happened live, inside renderDockspaceShell() (the same
    // "EditorUI mutates registry_'s Transform directly" pattern this whole
    // feature predates -- see gizmo.hpp's own header comment); this is only
    // ever the UNDO-HISTORY bookkeeping for an edit that already took
    // effect.
    if (transformEditCommitted.has_value()) {
        undoStack_.push(*transformEditCommitted);
    }
    // Phase 18h: the toolbar's own real undo/redo buttons -- the exact same
    // undo()/redo() methods Ctrl+Z/Ctrl+Y-or-Ctrl+Shift+Z (run()) call.
    if (undoRequested) {
        undo();
    }
    if (redoRequested) {
        redo();
    }
    // Phase 17d: the custom title bar's own four real effects -- see
    // TitleBarAction's own editor_ui.hpp comment for exactly when each field
    // is set. `closeRequested` reuses window_.requestClose(), which sets
    // GLFW's own should-close flag (glfwSetWindowShouldClose(), read back by
    // run()'s own `while (!window_.shouldClose())` loop condition) -- the
    // SAME flag the OS's own native close button, before this phase, already
    // set automatically. This is a DIFFERENT mechanism from this engine's
    // pre-existing Escape-while-uncaptured quit (decideCameraCapture(), Phase
    // 16): Escape never touches this flag at all -- it does a direct `break`
    // out of run()'s own loop body instead (see that call site's own
    // comment). Close and Escape both end up exiting the same loop, just via
    // two different routes, not one shared path. `requestedWindowPos` is
    // applied unconditionally when
    // present (no further validation needed here -- window_chrome.hpp's own
    // applyDragDelta() already guarantees a well-formed {x, y} pair, and
    // glfwSetWindowPos() itself has no failure mode this engine needs to
    // check for, the same "thin wrapper, no decision-making" contract
    // Window's own setCursorCaptured() already documents).
    if (titleBarAction.minimizeRequested) {
        window_.iconifyWindow();
    }
    if (titleBarAction.maximizeToggleRequested) {
        window_.toggleMaximizeRestore();
    }
    if (titleBarAction.closeRequested) {
        window_.requestClose();
    }
    if (titleBarAction.requestedWindowPos.has_value()) {
        window_.setWindowPos(titleBarAction.requestedWindowPos->first, titleBarAction.requestedWindowPos->second);
    }
    // Phase 16: see cameraCaptureRequestPending_'s own application.hpp
    // comment for why this is stored, not acted on immediately -- a plain
    // assignment (not an OR) is correct here: whatever this member held
    // going into this frame was already consumed at the very top of THIS
    // SAME frame's run() loop iteration, before update()/render() ran (see
    // run()'s own Phase 16 comment), so it is always false by the time this
    // line runs.
    //
    // Phase 18g: `&& !usingSceneCamera` -- a Viewport double-click detected
    // THIS frame, while Play mode is viewing through a scene Camera entity,
    // must never be allowed to capture the free-fly camera next frame (see
    // this function's own Phase 18g comment, near the top, on why there is
    // no free-fly camera to fly while the game view is locked to the scene
    // camera). This is the actual enforcement of that rule -- suppressing it
    // HERE, at the one place a same-frame request is latched for next
    // frame's decideCameraCapture() call, rather than threading a new
    // parameter into EditorUI's own double-click detection, since
    // `usingSceneCamera` is Application-owned state EditorUI has no need to
    // know about for any other reason.
    cameraCaptureRequestPending_ = cameraCaptureRequested && !usingSceneCamera;
    // Phase 15e: the File > Save Scene menu item's own second trigger path
    // (alongside run()'s own Ctrl+S check) -- see editor_ui.hpp/.cpp's own
    // Phase 15e comments for why EditorUI only ever reports this request
    // rather than acting on it directly.
    if (saveSceneRequested) {
        saveCurrentScene();
    }
    // Phase 15f: the Material Inspector's "Browse..." popup's own real
    // trigger path -- see editor_ui.hpp's own Phase 15f comment for why
    // EditorUI only ever reports which path was picked rather than loading
    // it itself. Only acted on when an entity is actually selected (the
    // popup can only be opened from a selected entity's own Inspector in
    // the first place, but selectedEntity_ is re-checked here defensively --
    // the same "don't assume a UI invariant still holds a function call
    // later" discipline every other ENGINE_DEBUG_*/UI-request handler in
    // this constructor/render() already follows).
    if (textureAssignRequested.has_value() && selectedEntity_.has_value()) {
        // See ENGINE_DEBUG_ASSIGN_TEXTURE's own constructor-time comment
        // (debugAssignTextureFromEnv()) for why resolveAssetPath() runs here,
        // right before the GL-touching resources_.getTexture() call, while
        // the ORIGINAL relative path is what's stored on the component --
        // the identical split modelPath/ModelComponent::path already uses.
        const std::string resolvedTexturePath = resolveAssetPath(*textureAssignRequested);
        try {
            MaterialOverride& materialOverride =
                registry_.addComponent<MaterialOverride>(*selectedEntity_, MaterialOverride{});
            materialOverride.diffuseTexture = resources_.getTexture(resolvedTexturePath);
            materialOverride.diffuseTexturePath = *textureAssignRequested;
            LOG_INFO("Inspector: entity " + std::to_string(selectedEntity_->index()) +
                      " now overrides its diffuse texture with \"" + *textureAssignRequested + "\"");
        } catch (const std::exception& e) {
            LOG_WARN("Inspector: failed to load texture \"" + *textureAssignRequested +
                      "\" for entity " + std::to_string(selectedEntity_->index()) + ": " + std::string(e.what()));
        }
    }
    // Phase 15g: the Viewport's own new drop target's real trigger path --
    // see editor_ui.hpp's own Phase 15g comment for why EditorUI only ever
    // reports which path was dropped rather than classifying or acting on it
    // itself, and handleViewportAssetDrop()'s own application.hpp comment for
    // the full model-vs-texture-vs-unrecognized dispatch this delegates to.
    if (assetDropRequested.has_value()) {
        handleViewportAssetDrop(*assetDropRequested);
    }
    renderDebugUI();
    editorUI_.render();
}

// Phase 14f: the Scene panel's Create menu's real implementation -- see this
// method's own application.hpp comment for the full contract (what each
// CreateEntityKind builds, where it's positioned, how its name is chosen).
// A no-op for CreateEntityKind::kNone (defensive only -- both real call
// sites, render()'s own createRequest handling above and the constructor's
// ENGINE_DEBUG_CREATE block, already filter that out before calling this).
void Application::spawnEntityFromCreateMenu(CreateEntityKind kind) {
    std::string baseName;
    // Two distinct path forms per kind, exactly mirroring the constructor's
    // own ENGINE_LEGACY_SCENE construction just above (which loads
    // resources_.getModel(kScenePath, *shader_) -- an already-resolved
    // ABSOLUTE path -- but stores the literal RELATIVE string
    // "assets/models/scene.obj" into ModelComponent::path): `loadPath` is
    // what actually gets handed to resources_.getModel() (an absolute path,
    // resolveAssetPath()'d once at static-init time into
    // kCreateCubeModelPath/etc. above); `storedPath` is the relative-form
    // string ModelComponent::path itself stores, matching what scene
    // serialization's saveScene()/loadScene() (scene_serialization.cpp) and
    // every entity assets/scenes/default.json already describes actually
    // expect there -- see ecs.hpp's own ModelComponent comment for why that
    // field specifically must stay a *reloadable* relative reference, not
    // whatever absolute path this one process happened to resolve it to.
    const std::string* loadPath = nullptr;
    std::string storedPath;
    switch (kind) {
        case CreateEntityKind::kCube:
            baseName = "Cube";
            loadPath = &kCreateCubeModelPath;
            storedPath = "assets/models/falling_cube.obj";
            break;
        case CreateEntityKind::kSphere:
            baseName = "Sphere";
            loadPath = &kCreateSphereModelPath;
            storedPath = "assets/models/sphere.obj";
            break;
        case CreateEntityKind::kPlane:
            baseName = "Plane";
            loadPath = &kCreatePlaneModelPath;
            storedPath = "assets/models/plane.obj";
            break;
        case CreateEntityKind::kEmpty:
            baseName = "Empty";
            // loadPath stays nullptr: an Empty is Transform + NameComponent
            // only, no ModelComponent -- see ecs.hpp's own NameComponent
            // comment and this phase's own brief for why (a real,
            // parentable organizational node, not a flat "folder" label
            // that isn't a real entity at all).
            break;
        case CreateEntityKind::kPointLight:
            baseName = "Point Light";
            // loadPath stays nullptr, same as Empty above -- this engine has
            // no light-gizmo mesh to draw for it (nothing in this project
            // renders a billboard/icon for a light today), so a Point Light
            // entity is Transform + NameComponent + light.hpp's PointLight
            // component, no ModelComponent. See this function's own
            // post-switch block below for where the PointLight component
            // itself gets added.
            break;
        case CreateEntityKind::kDirectionalLight:
            baseName = "Directional Light";
            // loadPath stays nullptr, same reason as kPointLight above --
            // see this function's own post-switch block below for where the
            // DirectionalLight component itself gets added, and for where
            // this new entity also becomes activeDirectionalLight_
            // (application.hpp).
            break;
        case CreateEntityKind::kCamera:
            baseName = "Camera";
            // loadPath stays nullptr, same reason as kPointLight/
            // kDirectionalLight above -- this engine has no camera-gizmo
            // mesh either. See this function's own post-switch block below
            // for where the CameraComponent itself gets added -- and,
            // unlike kDirectionalLight, that is the ENTIRE post-switch
            // effect: no Application member gets assigned afterward (see
            // camera_'s own application.hpp comment for why not).
            break;
        case CreateEntityKind::kNone:
        default:
            return;
    }

    // Phase 15g: the actual placement/Transform/NameComponent/ModelComponent
    // construction now lives in spawnPositionedEntity() (application.hpp's
    // own comment on it has the full "why factored out" reasoning) -- this
    // call is behavior-identical to what used to be inlined directly here.
    const EntityId entity =
        spawnPositionedEntity(baseName, loadPath, storedPath, "via the Scene panel's Create menu");

    // Phase 15a: a freshly Create'd Point Light starts at PointLight{}'s own
    // struct defaults (plain white, the same (1.0, 0.7, 1.8) attenuation
    // profile kPointLights already uses -- see light.hpp's own comment) --
    // a sane, immediately-visible starting point the Inspector's new Light
    // section (editor_ui.cpp) can then retune, not a placeholder nothing
    // reads. render()'s own collectPointLights() call picks this entity up
    // starting the very next frame, no further wiring needed here.
    if (kind == CreateEntityKind::kPointLight) {
        registry_.addComponent<PointLight>(entity, PointLight{});
    }
    // Phase 15b: a freshly Create'd Directional Light starts at
    // DirectionalLight{}'s own struct defaults -- see light.hpp's own
    // comment on why those defaults are deliberately NOT kLightDirection/
    // kLightColor's own values, unlike PointLight{} mirroring kPointLights'
    // shared attenuation profile above. Also unconditionally becomes this
    // Application's new activeDirectionalLight_ -- application.hpp's own
    // comment on that member has the full "why most-recently-created" design
    // discussion; the short version is that overwriting it here,
    // unconditionally, on every kDirectionalLight creation IS that rule, in
    // its entirety.
    if (kind == CreateEntityKind::kDirectionalLight) {
        registry_.addComponent<DirectionalLight>(entity, DirectionalLight{});
        activeDirectionalLight_ = entity;
    }
    // Phase 15c: a freshly Create'd Camera starts at CameraComponent{}'s own
    // struct defaults (60-degree FOV, 0.1/100.0 near/far -- copied verbatim
    // from engine::Camera's own defaults, camera.hpp -- see
    // camera_component.hpp's own comment for why). Deliberately no second
    // statement here the way kDirectionalLight has one above: this entity
    // becomes nothing's "active" anything -- it just exists in registry_,
    // selectable and inspectable, with nothing in render() ever reading it
    // back out. That is this phase's entire scope, on purpose (see
    // camera_component.hpp's own header comment).
    if (kind == CreateEntityKind::kCamera) {
        registry_.addComponent<CameraComponent>(entity, CameraComponent{});
    }

    // Phase 18h: pushes a real kCreateEntity Command onto undoStack_,
    // captured AFTER every one of the kind-specific component blocks above
    // has run -- so the record undo/redo actually recreates from already
    // includes the PointLight/DirectionalLight/CameraComponent this
    // entity's own kind adds, not just its bare Transform/NameComponent/
    // ModelComponent. See undo_stack.hpp's own header comment for why this
    // captures a full record rather than storing `kind` + a spawn position
    // to re-run this same function on redo (the camera may have moved by
    // then, which would silently redo to the WRONG position). Deliberately
    // the only creation path that pushes one -- see application.hpp's own
    // spawnEntityFromDroppedModel() comment for why a drag-and-drop model
    // drop is a separate, un-mentioned creation path this phase's own
    // confirmed scope does not extend undo/redo coverage to.
    undoStack_.push(
        makeCreateEntityCommand(entity, captureEntityRecord(registry_, entity, activeDirectionalLight_.value_or(EntityId()))));
}

// Phase 15g: see this method's own application.hpp comment for the full
// design (why this was factored out of spawnEntityFromCreateMenu(), and why
// it deliberately does NOT guard resources_.getModel() with a try/catch --
// that's each CALLER's own choice, made below and in
// spawnEntityFromDroppedModel()).
EntityId Application::spawnPositionedEntity(const std::string& baseName, const std::string* loadPath,
                                             const std::string& storedPath, const std::string& originDescription) {
    // Post-15g bug-review fix: resources_.getModel() -- when it's going to
    // run at all (loadPath != nullptr) -- now runs FIRST, before any
    // registry_ mutation whatsoever. It used to run last, after
    // registry_.create()/addComponent<Transform>()/addComponent<NameComponent>()
    // below, which was fine for spawnEntityFromCreateMenu()'s own three
    // model-backed kinds (Cube/Sphere/Plane only ever load checked-in,
    // known-good constants that never actually throw here in practice), but
    // spawnEntityFromDroppedModel()'s own call site CAN throw (an arbitrary
    // dropped path can name a real-but-unloadable file, e.g. a model's own
    // sibling .mtl -- see that method's own comment) -- and its try/catch
    // wraps this whole function call, with no EntityId back from a still-
    // in-flight call to clean up after a throw. The result, reproduced
    // directly by this bug's own review: a failed load left a permanent,
    // half-built "ghost" entity (a real Transform + NameComponent, no
    // ModelComponent) stranded in registry_ forever -- selectable, listed as
    // ordinary Scene Hierarchy clutter, and silently written straight into
    // the scene file by a later Save Scene, surviving even a reload. Loading
    // the model first means a throw here happens before registry_.create()
    // is ever called, so a failed load now leaves registry_ in EXACTLY the
    // state it was in before this function was entered -- no partial entity,
    // nothing to clean up, no special-case rollback logic needed anywhere.
    // Behavior for every existing (never-throwing) caller is unchanged:
    // reordering when a call that always succeeds happens relative to other
    // unconditional-success calls has no observable effect.
    std::shared_ptr<Model> loadedModel;
    if (loadPath != nullptr) {
        loadedModel = resources_.getModel(*loadPath, *shader_);
    }

    // Phase 14f: see kCreateEntityDistanceFromCamera/kCreateEntityMinHeight's
    // own comment above for the exact placement heuristic (in front of the
    // camera's current facing direction, floored above the ground plane).
    glm::vec3 spawnPosition = camera_.position() + (camera_.front() * kCreateEntityDistanceFromCamera);
    spawnPosition.y = std::max(spawnPosition.y, kCreateEntityMinHeight);

    const EntityId entity = registry_.create();
    registry_.addComponent<Transform>(entity).setPosition(spawnPosition);
    // uniqueEntityName() (this file's own Phase 14f comment) -- "Cube", then
    // "Cube (1)", "Cube (2)", ... the first time "Cube" is already taken,
    // exactly the phase brief's own chosen "simple append a counter" scheme
    // -- not bulletproof-unique, matching this project's own established
    // Phase 8b tolerance for duplicate NameComponent strings elsewhere, just
    // good enough that a freshly created entity doesn't read as a confusing
    // exact-duplicate row in the Scene Hierarchy tree by default.
    const std::string uniqueName = uniqueEntityName(registry_, baseName);
    registry_.addComponent<NameComponent>(entity, NameComponent{uniqueName});
    if (loadedModel) {
        registry_.addComponent<ModelComponent>(entity, ModelComponent{loadedModel, storedPath});
    }

    LOG_INFO("Created entity \"" + uniqueName + "\" (index " + std::to_string(entity.index()) + ") " +
              originDescription);
    return entity;
}

// Phase 15g: see this method's own application.hpp comment for the full
// design (why this reuses spawnPositionedEntity() above instead of a second
// copy of its Transform/NameComponent/ModelComponent-building logic, why
// placement stays that SAME "in front of camera, floored above ground"
// heuristic rather than a raycast into the dropped screen position, and why
// THIS call site -- unlike spawnPositionedEntity()'s own callers in
// spawnEntityFromCreateMenu() above -- does catch resources_.getModel()'s
// own exception).
void Application::spawnEntityFromDroppedModel(const std::string& assetRelativePath) {
    try {
        const std::string loadPath = resolveAssetPath(assetRelativePath);
        spawnPositionedEntity(modelBaseNameFromAssetPath(assetRelativePath), &loadPath, assetRelativePath,
                               "via a Viewport drag-and-drop of \"" + assetRelativePath + "\"");
    } catch (const std::exception& e) {
        LOG_WARN("Viewport drag-and-drop: failed to load model \"" + assetRelativePath +
                  "\" as an entity: " + std::string(e.what()));
    }
}

// Phase 15g: see this method's own application.hpp comment for the full
// design (why this is a THIRD independent trigger path for the identical
// MaterialOverride mechanism Phase 15f's Inspector "Browse..." popup and
// ENGINE_DEBUG_ASSIGN_TEXTURE already install, not a rewrite of either).
// Mirrors those two sites' own try/catch shape exactly (render()'s
// textureAssignRequested handling and debugAssignTextureFromEnv()'s own
// constructor-time block, both above/below) -- resolveAssetPath() right
// before the GL-touching resources_.getTexture() call, the ORIGINAL relative
// path stored in diffuseTexturePath so it stays a reloadable reference (see
// material_override.hpp's own MaterialOverride comment).
void Application::assignDroppedTextureOverride(EntityId entity, const std::string& entityLabel,
                                                const std::string& textureAssetPath) {
    const std::string resolvedTexturePath = resolveAssetPath(textureAssetPath);
    try {
        MaterialOverride& materialOverride = registry_.addComponent<MaterialOverride>(entity, MaterialOverride{});
        materialOverride.diffuseTexture = resources_.getTexture(resolvedTexturePath);
        materialOverride.diffuseTexturePath = textureAssetPath;
        LOG_INFO("Viewport drag-and-drop: entity " + entityLabel + " now overrides its diffuse texture with \"" +
                  textureAssetPath + "\"");
    } catch (const std::exception& e) {
        LOG_WARN("Viewport drag-and-drop: failed to load texture \"" + textureAssetPath + "\" for entity " +
                  entityLabel + ": " + std::string(e.what()));
    }
}

// Phase 15g: see this method's own application.hpp comment for the full
// dispatch contract (which AssetDropCategory does what, and why a texture
// dropped with nothing selected is a deliberately inert LOG_WARN, never a
// crash or an implicit guess at a target).
void Application::handleViewportAssetDrop(const std::string& assetRelativePath) {
    switch (classifyAssetDropPath(assetRelativePath)) {
        case AssetDropCategory::kModel:
            spawnEntityFromDroppedModel(assetRelativePath);
            break;
        case AssetDropCategory::kTexture:
            if (selectedEntity_.has_value()) {
                assignDroppedTextureOverride(*selectedEntity_, std::to_string(selectedEntity_->index()),
                                              assetRelativePath);
            } else {
                LOG_WARN("Viewport drag-and-drop: a texture (\"" + assetRelativePath +
                          "\") was dropped with no entity selected to assign it to; ignored");
            }
            break;
        case AssetDropCategory::kUnrecognized:
        default:
            LOG_WARN("Viewport drag-and-drop: \"" + assetRelativePath +
                      "\" is not a recognized model or texture asset path; ignored");
            break;
    }
}

// Phase 15e: Save Scene's real implementation -- see this method's own
// application.hpp comment for the full contract/design discussion. Writes
// EVERY entity currently in registry_ (not just ones a user actually
// touched this run) to currentScenePath_, exactly matching saveScene()'s
// own "serializes every entity with at least a Transform" contract
// (scene_serialization.hpp) -- there is no per-entity "dirty" tracking
// anywhere in this engine (an "unsaved changes" indicator is explicitly out
// of this phase's own scope, see README.md's Phase 15e section), so a save
// is always a full, unconditional snapshot of the live scene, the same way
// every prior phase's own saveScene() contract already promised even before
// anything called it.
//
// Phase 18i update: was unconditionally kDefaultScenePath through Phase
// 18h -- this is the one behavior change 18i makes to this pre-existing
// method, generalizing "save to the ONE file this engine always knows
// about" into "save to whichever file is currently open"
// (currentScenePath_, application.hpp), which for an untouched fresh run is
// still kDefaultScenePath (see the constructor's own Phase 18i comment) --
// so Ctrl+S/File > Save Scene's own behavior in the ordinary, most-common
// case is unchanged byte-for-byte, confirmed by this phase's own
// no-regression pixel-diff (see README.md's own Phase 18i Verify section),
// not merely assumed from the code alone.
void Application::saveCurrentScene() {
    saveScene(registry_, currentScenePath_, activeDirectionalLight_.value_or(EntityId()));
    LOG_INFO("Saved scene to \"" + currentScenePath_ + "\"");
}

// Phase 18i: see this method's own application.hpp comment for the full
// contract. `each<Transform>()` is collected into a plain vector FIRST,
// not destroyed inline inside the each() callback itself -- ComponentPool's
// own dense-array storage (ecs.hpp) means destroying an entity mid-iteration
// would invalidate/rearrange the very pool each() is walking (a swap-remove,
// the same reason spawnEntityFromCreateMenu()'s own captureEntityRecord()
// call happens BEFORE destruction elsewhere in this file), so this collects
// every live id first, into its own short-lived vector, then destroys each
// one in a completely separate second loop.
void Application::clearSceneForTransition() {
    std::vector<EntityId> liveEntities;
    registry_.each<Transform>([&](EntityId id, Transform& /*transform*/) { liveEntities.push_back(id); });
    for (EntityId id : liveEntities) {
        registry_.destroyEntity(id);
    }

    selectedEntity_.reset();
    activeDirectionalLight_.reset();
    undoStack_.clear();
}

// Phase 18i: see this method's own application.hpp comment for the full
// contract/design discussion (why no confirmation prompt, why
// kUntitledScenePath rather than std::nullopt).
void Application::newScene() {
    clearSceneForTransition();
    currentScenePath_ = kUntitledScenePath;
    LOG_INFO("New Scene: registry_ cleared to an empty scene; currentScenePath_ is now \"" + currentScenePath_ +
              "\" (not written to disk until the next Save)");
}

// Phase 18i: see this method's own application.hpp comment for the full
// contract. Does no validation of `sanitizedName` itself -- see that
// comment for why this method trusts its callers to have already run it
// through sanitizeSceneName() (scene_file_ops.hpp).
void Application::saveSceneAs(const std::string& sanitizedName) {
    const std::string resolvedPath = resolveAssetPath(sceneRelativePathForName(sanitizedName));
    // Phase 18i: this engine's plain Save Scene has always silently
    // overwritten kDefaultScenePath with no "are you sure" step (see
    // saveCurrentScene()'s own comment/README.md's Phase 15e section) --
    // Save As follows the identical convention for consistency, rather than
    // introducing a NEW confirm-before-overwrite behavior this engine has
    // never had anywhere else. The only difference here is a plain LOG_INFO
    // naming whether this call is creating a brand-new file or replacing an
    // existing one, purely for traceability (a headless run's own log, or a
    // real user watching the console) -- not a behavior change, just a more
    // informative message than saveCurrentScene()'s own single-file case
    // ever needed.
    std::error_code existsError;
    const bool overwriting = std::filesystem::exists(resolvedPath, existsError) && !existsError;
    saveScene(registry_, resolvedPath, activeDirectionalLight_.value_or(EntityId()));
    currentScenePath_ = resolvedPath;
    LOG_INFO(std::string("Save As: ") + (overwriting ? "overwrote existing file " : "saved new file ") + "\"" +
              resolvedPath + "\"; currentScenePath_ updated -- a following Save Scene now targets this file");
}

// Phase 18i: see this method's own application.hpp comment for the full
// contract/design discussion, in particular why the pre-validation
// parseSceneRecords() call below has to run BEFORE clearSceneForTransition()
// -- a corrupt/malformed scene file must never wipe the currently loaded
// scene out from under the user on its way to failing.
void Application::openScene(const std::string& sceneName) {
    const std::string resolvedPath = resolveAssetPath(sceneRelativePathForName(sceneName));

    // Pure-data validation only -- parseSceneRecords() (scene_serialization.hpp)
    // touches no registry/GL/ResourceManager state at all, so a file that
    // fails here (missing, not valid JSON, doesn't match the schema) leaves
    // the CURRENTLY loaded scene completely untouched. This is deliberately
    // NOT the only thing that can go wrong -- a record whose modelPath
    // references a missing/unloadable asset only fails later, inside the
    // real loadScene() call below, AFTER clearSceneForTransition() has
    // already run (see this method's own application.hpp comment for why
    // pre-flight-validating every referenced asset before committing to the
    // clear, rather than just the pure JSON/schema pre-check here, is real,
    // separate scope this method doesn't take on). That catch block below
    // repeats clearSceneForTransition() on this path so a failed load still
    // ends in a genuinely empty registry_ rather than a half-loaded one --
    // what's accepted as a documented gap is narrower than "the registry
    // ends up wrong": it's that the scene which was open before this
    // openScene() call is still gone (clearSceneForTransition() already
    // erased it before loadScene() was even attempted), the identical
    // "no recovery of the previous state" risk profile loadScene() already
    // has at STARTUP for a bad assets/scenes/default.json, just reached
    // from a live in-editor action instead of process launch.
    try {
        parseSceneRecords(resolvedPath);
    } catch (const std::exception& e) {
        LOG_ERROR("Open Scene: \"" + resolvedPath + "\" could not be parsed (" + std::string(e.what()) +
                    "); the currently loaded scene was left untouched");
        return;
    }

    clearSceneForTransition();

    EntityId loadedActiveDirectionalLight;
    try {
        loadScene(registry_, resolvedPath, resources_, *shader_, &loadedActiveDirectionalLight);
    } catch (const std::exception& e) {
        // Reachable only for the narrower "parsed fine, but a referenced
        // model/texture asset itself couldn't load" failure mode named
        // above. loadScene()'s own per-record loop (scene_loader.cpp) has
        // no rollback of its own: restoreEntityFromRecord() creates each
        // record's entity and adds its non-model components BEFORE it ever
        // touches that record's model path, so a LATER record's model
        // failure leaves every EARLIER record's entity already live in
        // registry_ when this catch fires -- clearSceneForTransition()
        // above already ran once (clearing the scene that was loaded
        // before this openScene() call even started), but that is not the
        // same thing as the load attempt itself having added nothing.
        // Calling it again here is what actually makes good on this
        // method's real contract for a failed load: destroy whatever
        // partial set of entities this failed loadScene() call just
        // created, so this run ends with the exact same genuinely-empty
        // registry_ a real newScene() produces, not a half-populated one
        // silently left behind from a partially-completed load.
        clearSceneForTransition();
        LOG_ERROR("Open Scene: \"" + resolvedPath + "\" passed its own JSON/schema validation but failed to load (" +
                    std::string(e.what()) + "); the partially-loaded entities were rolled back and the scene is "
                    "now genuinely empty -- see this method's own application.hpp comment. currentScenePath_ is "
                    "left unchanged (still \"" + currentScenePath_ + "\") since this load did not succeed");
        return;
    }
    if (loadedActiveDirectionalLight.valid()) {
        activeDirectionalLight_ = loadedActiveDirectionalLight;
    }

    currentScenePath_ = resolvedPath;
    std::size_t entityCount = 0;
    registry_.each<Transform>([&](EntityId /*id*/, Transform& /*transform*/) { ++entityCount; });
    LOG_INFO("Open Scene: loaded \"" + resolvedPath + "\" (" + std::to_string(entityCount) +
              " entit(y/ies)); currentScenePath_ updated -- a following Save Scene now targets this file");
}

// Phase 18h: see this method's own application.hpp comment for the full
// design. Captures BEFORE destroying anything -- captureEntityRecord()
// reads `id`'s own still-live components, so this order is load-bearing,
// not incidental.
void Application::deleteEntity(EntityId id) {
    const SceneEntityRecord record = captureEntityRecord(registry_, id, activeDirectionalLight_.value_or(EntityId()));
    undoStack_.push(makeDeleteEntityCommand(id, record));
    destroyEntityOrphaningChildren(registry_, id);
    if (selectedEntity_.has_value() && *selectedEntity_ == id) {
        selectedEntity_.reset();
    }
}

void Application::setTransformFromSnapshot(EntityId id, const TransformSnapshot& snapshot) {
    Transform* transform = registry_.getComponent<Transform>(id);
    if (transform == nullptr) {
        // Defensive only -- see this method's own application.hpp comment.
        return;
    }
    transform->setPosition(snapshot.position);
    transform->setRotation(snapshot.rotation);
    transform->setScale(snapshot.scale);
}

void Application::destroyCommandEntity(Command& cmd) {
    destroyEntityOrphaningChildren(registry_, cmd.entity);
    if (selectedEntity_.has_value() && *selectedEntity_ == cmd.entity) {
        selectedEntity_.reset();
    }
}

void Application::recreateCommandEntity(Command& cmd) {
    EntityId newId;
    try {
        newId = restoreEntityFromRecord(registry_, cmd.record, resources_, *shader_);
    } catch (const std::exception& e) {
        // See this method's own application.hpp comment -- shouldn't happen
        // for a record captured from a real, previously-loaded live entity,
        // but not assumed impossible.
        LOG_ERROR("Application::recreateCommandEntity: failed to restore entity \"" + cmd.record.name +
                   "\": " + std::string(e.what()));
        return;
    }

    // restoreEntityFromRecord() deliberately does not resolve `parentName`
    // (it needs a caller-specific name -> EntityId lookup -- see that
    // function's own scene_serialization.hpp comment) -- findEntityByName()
    // against the CURRENT live registry_ is this caller's own choice of how
    // to resolve it, the identical free function ENGINE_DEBUG_SELECT/
    // ENGINE_DEBUG_DELETE above already use. A parent that no longer exists
    // (e.g. it was itself deleted in the meantime) simply leaves the
    // recreated entity as a root -- the same "dangling Parent reference"
    // tolerance resolveWorldMatrix() already has, just resolved at
    // recreation time instead of read time.
    if (!cmd.record.parentName.empty()) {
        const EntityId parentId = findEntityByName(registry_, cmd.record.parentName);
        if (parentId.valid()) {
            registry_.addComponent<Parent>(newId, Parent{parentId});
        }
    }

    // Mirrors loadScene()'s own "last record with directionalLightActive
    // wins" rule (scene_serialization.hpp's own "Active directional light"
    // comment) -- a recreated DirectionalLight entity that WAS the active
    // one at deletion/undo time becomes activeDirectionalLight_ again,
    // pointing at its own brand-new id (the old id this member might still
    // hold is, by now, permanently stale either way -- see that member's
    // own "never reset on delete" application.hpp comment).
    if (cmd.record.hasDirectionalLight && cmd.record.directionalLightActive) {
        activeDirectionalLight_ = newId;
    }

    cmd.entity = newId;
}

void Application::undo() {
    undoStack_.undo([this](Command& cmd) {
        switch (cmd.kind) {
            case CommandKind::kTransformEdit:
                setTransformFromSnapshot(cmd.entity, cmd.before);
                break;
            case CommandKind::kCreateEntity:
                // Undoing a creation removes it.
                destroyCommandEntity(cmd);
                break;
            case CommandKind::kDeleteEntity:
                // Undoing a deletion restores it.
                recreateCommandEntity(cmd);
                break;
        }
    });
}

void Application::redo() {
    undoStack_.redo([this](Command& cmd) {
        switch (cmd.kind) {
            case CommandKind::kTransformEdit:
                setTransformFromSnapshot(cmd.entity, cmd.after);
                break;
            case CommandKind::kCreateEntity:
                // Redoing a creation recreates it.
                recreateCommandEntity(cmd);
                break;
            case CommandKind::kDeleteEntity:
                // Redoing a deletion removes it again.
                destroyCommandEntity(cmd);
                break;
        }
    });
}

// Phase 8c: builds and draws the debug overlay -- see this class's own
// Phase 8c header comment for the overall design and debug_ui.hpp for the
// ImGui context/backend lifecycle this wraps. A no-op (via
// debugUI_.enabled()) unless ENGINE_SHOW_DEBUG_UI was set at startup, so a
// default headless run never reaches a single ImGui:: call below.
//
// Panel contents, deliberately modest (see this phase's own "What NOT to
// do" scope notes):
//   - Frame Stats: frame count + ImGui's own smoothed frame time/FPS
//     (io.Framerate -- an exponential moving average ImGui itself
//     maintains from ImGui::GetIO().DeltaTime each NewFrame(), not
//     something this engine needs to compute separately).
//   - Render Passes: checkboxes bound directly to the existing
//     ssaoDisabled_/ssaoDebugMode_/ssrDisabled_/clusterDebugMode_ members
//     (Phase 13d/13f/13g) -- these were already plain bool members render()
//     re-reads every frame (see those phases' own comments above), so an
//     ImGui::Checkbox bound to one of them (by address) makes it
//     live-toggleable with no further plumbing: no getter/setter needed,
//     no conversion from a getenv-once flag to a "real" runtime one, since
//     they already were runtime-mutable state, just previously only ever
//     set once (from an env var) rather than from a UI too.
//   - Scene Entities: a minimal inspector over registry_.each<Transform>(...)
//     -- every entity with a Transform (not just ones with a Model, since a
//     future entity might reasonably have one without the other -- see
//     ecs.hpp's own "components are opt-in" design), labeled by its
//     NameComponent when present (falling back to a bare "entity N" label
//     otherwise, since NameComponent is itself opt-in -- see ecs.hpp), with
//     position/rotation/scale editable via ImGui::DragFloat3. Rotation is
//     shown/edited as Euler degrees (glm::eulerAngles/glm::radians convert
//     to and from the stored glm::quat) purely because that's what a human
//     can drag meaningfully in a debug UI -- Transform's own storage stays
//     quaternion-based (see transform.hpp's own header comment on why),
//     this just converts at the UI boundary each frame rather than
//     changing what's actually stored.
void Application::renderDebugUI() {
    if (!debugUI_.enabled()) {
        return;
    }

    // Phase 14a: no debugUI_.newFrame() call here any more -- editorUI_
    // already started this frame's one shared ImGui frame (see render()'s
    // own tail) before this function runs. Everything below is otherwise
    // byte-for-byte the same panel this function has drawn since Phase 8c.

    // ImGuiCond_FirstUseEver: places the window at a sane on-screen default
    // the first time it ever appears (this engine disables imgui.ini
    // persistence -- see DebugUI's own constructor comment -- so "first
    // use" really means "every run"), while still leaving it free to be
    // dragged/resized afterward in an interactive session, unlike
    // ImGuiCond_Always (which would re-snap it back here every single frame
    // and fight the user's own repositioning).
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::Begin("Engine Debug");

    if (ImGui::CollapsingHeader("Frame Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("Frame: %llu", static_cast<unsigned long long>(frameCount_));
        ImGui::Text("%.2f ms/frame (%.1f FPS)", io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f, io.Framerate);
    }

    if (ImGui::CollapsingHeader("Render Passes", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Disable SSAO (ENGINE_SSAO_DISABLE)", &ssaoDisabled_);
        ImGui::Checkbox("SSAO debug view: raw occlusion buffer", &ssaoDebugMode_);
        ImGui::Checkbox("Disable SSR (ENGINE_SSR_DISABLE)", &ssrDisabled_);
        ImGui::Checkbox("Cluster light-count debug view", &clusterDebugMode_);
    }

    // Phase 8d: read-only -- shows inputActionMap_'s actual current
    // bindings (rather than restating the defaults in a comment somewhere)
    // so the binding table is genuinely inspectable, matching this
    // phase's own "data-driven, not hardcoded ifs" goal. No editing here;
    // a rebinding UI is out of scope for this phase (see README.md).
    if (ImGui::CollapsingHeader("Input Bindings")) {
        constexpr std::array<InputAction, 8> kAllActions = {
            InputAction::MoveForward, InputAction::MoveBackward, InputAction::MoveLeft,       InputAction::MoveRight,
            InputAction::MoveUp,      InputAction::MoveDown,     InputAction::ToggleDebugUI,  InputAction::Quit,
        };
        for (InputAction action : kAllActions) {
            std::string keys;
            for (int key : inputActionMap_.bindingsFor(action)) {
                if (!keys.empty()) {
                    keys += " or ";
                }
                keys += keyName(key);
            }
            ImGui::Text("%s: %s", actionName(action), keys.empty() ? "(unbound)" : keys.c_str());
        }
    }

    // Cross-phase note (8c x 8e), not caught by either phase's own review in
    // isolation: for a RigidBody entity (e.g. "falling_cube"), this panel's
    // DragFloat3 edit and stepPhysics() (called once per frame from
    // update(), BEFORE render() -- see that function's own Phase 8e comment)
    // both write the same entity's Transform::position within one frame, in
    // this order: stepPhysics() first, this panel last (renderDebugUI() is
    // the final call in render()). So a drag here always "wins" for what the
    // Transform holds at the end of the frame -- it is never silently
    // clobbered -- but this same frame's already-drawn 3D view used the
    // OLDER, physics-computed position (drawn earlier in render(), before
    // this panel runs), so the drag's effect on-screen is visible one frame
    // late. That one-frame lag applies to editing ANY entity here, with or
    // without a RigidBody; what's specific to RigidBody is this: dragging an
    // entity's position does not touch its RigidBody::velocity, so next
    // frame's stepPhysics() resumes integrating from the dragged-to position
    // using whatever velocity the body already had -- e.g. dragging a
    // falling entity back up mid-fall does not make it come to rest at the
    // new spot; it keeps falling from there at its prior speed. Whether a
    // manual reposition should also zero velocity is a physics/product
    // design call this "gravity + ground collision" phase's own scope never
    // needed to make (see physics.hpp's own "What this deliberately IS NOT"
    // list) -- not a bug in either system considered alone, just a real
    // interaction worth knowing about before relying on this panel to
    // "place" a falling entity.
    if (ImGui::CollapsingHeader("Scene Entities", ImGuiTreeNodeFlags_DefaultOpen)) {
        registry_.each<Transform>([&](EntityId id, Transform& transform) {
            const NameComponent* nameComponent = registry_.getComponent<NameComponent>(id);
            const std::string label =
                nameComponent != nullptr ? nameComponent->name : ("entity " + std::to_string(id.index()));

            ImGui::PushID(static_cast<int>(id.index()));
            if (ImGui::TreeNode(label.c_str())) {
                glm::vec3 position = transform.position();
                if (ImGui::DragFloat3("Position", &position.x, 0.01f)) {
                    transform.setPosition(position);
                }

                glm::vec3 rotationDeg = glm::degrees(glm::eulerAngles(transform.rotation()));
                if (ImGui::DragFloat3("Rotation (deg)", &rotationDeg.x, 0.5f)) {
                    transform.setRotation(glm::quat(glm::radians(rotationDeg)));
                }

                glm::vec3 scale = transform.scale();
                if (ImGui::DragFloat3("Scale", &scale.x, 0.01f, 0.01f, 100.0f)) {
                    transform.setScale(scale);
                }

                const ModelComponent* modelComponent = registry_.getComponent<ModelComponent>(id);
                if (modelComponent != nullptr) {
                    ImGui::TextDisabled("Model: %s", modelComponent->path.c_str());
                }

                ImGui::TreePop();
            }
            ImGui::PopID();
        });
    }

    ImGui::End();

    // Phase 14a: no debugUI_.render() call here any more -- editorUI_.
    // render() (see render()'s own tail) rasterizes this panel's draw data
    // together with editorUI_'s own dockspace/panels in one shared
    // ImGui::Render() call.
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

        // Post-review bug fix: `forceEscapeDown` computed BEFORE polling,
        // and threaded straight into pollInputState() itself -- see that
        // function's own input.hpp comment and kDebugSimulateEscapeHoldFrames'
        // own comment above for exactly why this has to enter at the
        // physical-key-query layer (not as a post-hoc override of the
        // returned InputState) to genuinely exercise InputActionMap's real
        // edge-detection across multiple consecutive polls of a "held" key,
        // the same way a real physical Escape press spans many polls, not
        // one.
        const bool forceEscapeDown = debugSimulateEscape_ && frameCount_ >= kDebugSimulateEscapeFrame &&
                                      frameCount_ < kDebugSimulateEscapeFrame + kDebugSimulateEscapeHoldFrames;
        if (forceEscapeDown && frameCount_ == kDebugSimulateEscapeFrame) {
            LOG_INFO("ENGINE_DEBUG_SIMULATE_ESCAPE: simulating a HELD Escape press starting frame " +
                      std::to_string(frameCount_) + ", held for " + std::to_string(kDebugSimulateEscapeHoldFrames) +
                      " consecutive frame(s)");
        }

        // Polled once per frame, right after pollEvents() (same timing
        // real keyboard/mouse reads always had) and threaded down through
        // update() to whatever needs it -- Camera (movement/mouse-look) and,
        // since Phase 8d, update() itself (input.toggleDebugUIPressed, read
        // directly to flip debugUI_'s enabled state) -- see input.hpp.
        // Application -- like Camera since Phase 6 -- never reaches into
        // Window/GLFW key constants itself (`forceEscapeDown` above is the
        // one deliberate, documented exception, and even that enters through
        // pollInputState()'s own parameter, not a direct window_ call here);
        // InputState is the one place that does.
        const InputState input = pollInputState(window_, inputActionMap_, forceEscapeDown);

        // Escape now has two meanings depending on cameraCaptured_, decided
        // by ONE call to decideCameraCapture() (camera_capture.hpp) rather
        // than two separately-maintained branches -- see that function's own
        // header comment for the full precedence rule.
        //
        // Post-review bug fix: fed `input.escapeJustPressed` here, NOT
        // `input.escapePressed` -- see camera_capture.hpp's own "Post-review
        // bug fix" comment for exactly why decideCameraCapture() requires an
        // EDGE-triggered signal (true for exactly one poll per physical
        // press) and what went wrong when it was fed the level-triggered
        // field instead: a real held Escape exited capture correctly on the
        // first poll, then incorrectly quit the app on the very next one,
        // since the level-triggered field stayed true for as long as the
        // physical key was held, indistinguishable from a brand-new press
        // once `cameraCaptured_` had already flipped to false.
        //
        // The other half of this same call's job is consuming
        // cameraCaptureRequestPending_ -- last frame's own render() may have
        // detected a Viewport double-click one whole update()+render() call
        // after Escape's own check point (see that member's own
        // application.hpp comment for exactly why it has to cross the
        // run()/render() boundary this way, and the one frame of latency
        // that costs). Reset to false immediately after reading it, the
        // ordinary "one-shot signal, cleared once acted on" shape this
        // engine's other out-parameters/pending flags already follow.
        const CameraCaptureDecision captureDecision =
            decideCameraCapture(cameraCaptured_, input.escapeJustPressed, cameraCaptureRequestPending_);
        cameraCaptureRequestPending_ = false;
        setCameraCaptured(captureDecision.captured);
        if (captureDecision.quitRequested) {
            LOG_INFO("ESC pressed, exiting main loop");
            break;
        }

        // Phase 15e: Ctrl+S -- Save Scene's keyboard shortcut, deliberately
        // NOT routed through inputActionMap_/InputState the way every
        // action above and below this block is -- see ctrlSWasDown_'s own
        // application.hpp comment for the two reasons why (InputActionMap's
        // bindings are an OR of alternate single keys per action, not an AND
        // of simultaneous keys a chord needs; and Save is an editor-chrome
        // action, not one of the camera/window-lifecycle actions
        // InputActionMap actually exists to bind). A direct, edge-triggered
        // two-key read against window_ instead -- window_.isKeyPressed() is
        // the exact same public method input_action_map.cpp's own update()
        // calls internally, just checked here directly rather than through
        // that class. Both GLFW_KEY_LEFT_CONTROL and GLFW_KEY_RIGHT_CONTROL
        // are checked (either counts) for the same "don't force one specific
        // physical key when either conveys the same intent" reason
        // MoveUp's own Space-or-E default binding does.
        //
        // Post-15e review fix: gated on !ImGui::GetIO().WantCaptureKeyboard,
        // unlike escapePressed/InputState's own movement fields just above
        // (a pre-existing, separately-tracked gap -- debug_ui.hpp's own
        // Phase 8c comment already documents ImGui capture-flag gating as a
        // real, known, deliberately-deferred integration step, not something
        // this phase introduced or is responsible for closing everywhere).
        // Save is new, deliberate behavior added THIS phase, not inherited
        // legacy behavior, so it doesn't inherit that pass -- without this
        // gate, Ctrl+S pressed while an Inspector text/drag field (e.g.
        // Transform's Position DragFloat3) has keyboard focus would fire a
        // save mid-edit of a field value ImGui hasn't even committed back to
        // the underlying Transform/component yet, an editor foot-gun this
        // phase can easily avoid rather than propagate into new code. Only
        // gates the SHORTCUT, not the menu item (`saveSceneRequested` in
        // render()) -- clicking "File > Save Scene" is itself unambiguous
        // ImGui input, already only reachable when ImGui, not the 3D
        // viewport, has the click.
        const bool ctrlSDown = !ImGui::GetIO().WantCaptureKeyboard &&
                                (window_.isKeyPressed(GLFW_KEY_LEFT_CONTROL) ||
                                 window_.isKeyPressed(GLFW_KEY_RIGHT_CONTROL)) &&
                                window_.isKeyPressed(GLFW_KEY_S);
        if (ctrlSDown && !ctrlSWasDown_) {
            saveCurrentScene();
        }
        ctrlSWasDown_ = ctrlSDown;

        // Phase 18h: Ctrl+Z (undo) and Ctrl+Y-or-Ctrl+Shift+Z (redo) -- the
        // identical edge-triggered two/three-key chord read ctrlSDown just
        // above already establishes, for the identical two reasons given
        // there (a chord needs an AND of simultaneous keys, which
        // InputActionMap's own single-key-OR-bindings shape doesn't support;
        // and this is editor-chrome behavior, not one of the camera/window-
        // lifecycle actions that class actually exists to bind), gated on
        // the same !ImGui::GetIO().WantCaptureKeyboard so typing Ctrl+Z into
        // an Inspector text field (this engine has none today, but a future
        // one could) doesn't fire this shortcut mid-edit.
        //
        // Redo supports BOTH Ctrl+Y (the Windows/most-apps convention) and
        // Ctrl+Shift+Z (the Mac/many-creative-apps convention) -- trivial to
        // support both (one more `||` term) rather than forcing a single
        // choice, so this project's brief's own "or support both if
        // trivial" is taken literally. `redoChordDown`'s own Shift check is
        // OR'd against GLFW_KEY_Y specifically (not required alongside it)
        // -- Ctrl+Y alone is already a complete redo chord on its own.
        // ctrlZDown itself explicitly EXCLUDES Shift being held (`!(...)`)
        // so a Ctrl+Shift+Z press registers as redo only, never also as an
        // undo -- without that exclusion both edge-triggered checks would
        // fire on the exact same physical press.
        const bool shiftDown =
            window_.isKeyPressed(GLFW_KEY_LEFT_SHIFT) || window_.isKeyPressed(GLFW_KEY_RIGHT_SHIFT);
        const bool ctrlDownForUndoRedo = window_.isKeyPressed(GLFW_KEY_LEFT_CONTROL) ||
                                          window_.isKeyPressed(GLFW_KEY_RIGHT_CONTROL);
        const bool ctrlZDown =
            !ImGui::GetIO().WantCaptureKeyboard && ctrlDownForUndoRedo && window_.isKeyPressed(GLFW_KEY_Z) && !shiftDown;
        if (ctrlZDown && !ctrlZWasDown_) {
            undo();
        }
        ctrlZWasDown_ = ctrlZDown;

        const bool redoChordDown = !ImGui::GetIO().WantCaptureKeyboard && ctrlDownForUndoRedo &&
                                    (window_.isKeyPressed(GLFW_KEY_Y) ||
                                     (window_.isKeyPressed(GLFW_KEY_Z) && shiftDown));
        if (redoChordDown && !redoChordWasDown_) {
            redo();
        }
        redoChordWasDown_ = redoChordDown;

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
