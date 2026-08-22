#ifndef ENGINE_APPLICATION_HPP
#define ENGINE_APPLICATION_HPP

// Owns a Window and drives the main loop: poll events -> update -> render
// -> swap, once per frame, until the window should close or ESC is
// pressed. Computes delta-time and a frame count each iteration -- nothing
// consumes them yet, but later phases (animation, physics, camera
// movement, FPS display) will need exactly this, so the pattern is
// established here rather than bolted on later. Phase 6 adds the smallest
// possible "things in the scene are enumerable" structure (entities_, see
// entity.hpp) but deliberately does not grow into a full scene graph / real
// ECS / callback system -- that's later phases' job if/when it's needed.
//
// Phase 2 added the first real rendered content: render() draws a shaded
// cube via a Shader + Mesh instead of just clearing the framebuffer.
//
// Phase 3 replaces Phase 2's hardcoded eye position + hand-rolled view/
// projection matrices with a real Camera (view/projection) and Transform
// (the cube's model matrix). update() now feeds delta-time and input into
// camera_ every frame -- either real WASD/mouse input via Window, or (when
// ENGINE_CAMERA_DEMO is set) a small deterministic scripted path, since
// there's no real keyboard/mouse under the headless Xvfb verification
// harness. See update()'s definition for both paths.
//
// Phase 4 replaces Phase 2-3's flat per-face uColor rendering with a real
// Material (Shader + Texture + tint/shininess) and Blinn-Phong directional
// lighting: render() now uploads separate model/view/projection + normal
// matrices (instead of one combined MVP) plus light uniforms, and issues a
// single whole-mesh draw() instead of drawRange() per face, since the
// cube's six flat uColor faces are gone in favor of one textured, lit
// surface.
//
// Phase 5 replaces the Phase 2-4 hardcoded single cube with a real Model
// (Assimp-loaded scene graph, see model.hpp): render() now calls
// model_.draw(), which recursively composes each node's local transform
// with its parent's and uploads uModel/uNormalMatrix per node instead of
// once per frame. sceneTransform_ is an outer Transform placed above the
// whole model's own node hierarchy (a small fixed rotation, the same "prove
// the composition order is right, not just that it compiles" role
// cubeTransform_'s fixed rotation played in Phase 3-4) so the render path
// exercises at least two composed levels: sceneTransform_ * (accumulated
// parent node transform) * (node's own local transform). Camera and
// lighting are otherwise unchanged from Phase 3/4.
//
// Phase 6 restructures how all of the above is owned, without changing what
// gets rendered:
//   - shader_/model_'s previous ad-hoc construction (each loaded directly
//     in this class's constructor) is replaced by resources_ (a
//     ResourceManager): both are now requested via resources_.getShader()/
//     getModel(), which load on first request and hand back the same
//     cached instance on any later request for the same asset -- see
//     resource_manager.hpp.
//   - Phase 5's single model_ + sceneTransform_ pair (two loose members
//     that only worked for exactly one object) becomes entities_, a
//     std::vector<Entity>: each Entity bundles a Transform with a
//     (possibly shared, possibly null) Model -- see entity.hpp. This phase
//     only ever puts one Entity in it, but render() iterates entities_
//     rather than assuming exactly one, establishing the "a scene is an
//     enumerable list of things" pattern a later phase's real scene
//     graph/ECS can build on.
//   - Camera no longer reads Window directly for keyboard/mouse state:
//     Application::run() polls an InputState once per frame (see
//     input.hpp) and passes it down to update()/Camera, rather than Camera
//     calling Window::isKeyPressed() itself the way Phase 3-5's
//     processKeyboard(const Window&, float) did.
//
// Phase 7a extends the lighting/rendering pipeline without touching the
// main-loop shape above:
//   - Phase 4's single hardcoded directional light is joined by a small
//     fixed set of point lights and a spot light (kPointLights/kSpotLights
//     in application.cpp), uploaded to the shader as fixed-size uniform
//     arrays + a live count each frame -- see basic.frag.
//   - A second render target, shadowMap_ (see shadow_map.hpp), is rendered
//     into once per frame *before* the main pass: the whole scene, drawn
//     depth-only from the directional light's point of view via a second,
//     minimal shader (shadowShader_). The main pass then samples that depth
//     texture per-fragment to reduce the directional light's own
//     contribution for shadowed fragments (see renderShadowPass()/render()
//     and basic.frag's shadowFactor()).
//   - A hand-built ground plane (groundMesh_/groundMaterial_, not part of
//     scene.obj/Model) is drawn alongside entities_ specifically so this
//     phase has one demo surface with a bound normal map (see mesh.hpp's
//     makeGroundPlane()) independent of scene.obj's own (still
//     normal-map-less) materials.
//
// Phase 9 adds a second, independent material/shader pair -- a real
// metallic/roughness Cook-Torrance PBR model (see pbr_material.hpp,
// assets/shaders/pbr.vert/pbr.frag) -- alongside the existing Blinn-Phong
// Material/basic.vert/basic.frag path, which stays untouched as the
// reference/fallback for the table/box/pyramid/ground. Two rows of spheres
// (sphereMesh_/sphereInstances_ below) -- one sweeping metallic 0 -> 1 at a
// fixed low roughness, the other sweeping roughness across its range at
// fixed metallic = 1, placed low in frame in the open ground area in front
// of the existing scene rather than overlapping it (see application.cpp's
// Phase 9 bug-review composition-fix comment) -- is the new PBR-lit content
// this phase adds: it's the classic "PBR reference chart" made real, proving
// the BRDF actually produces the right qualitative behavior (sharp vs. broad
// highlights, colored vs. neutral reflectance) rather than just compiling. render()
// draws entities_/the ground plane with shader_ (unchanged), then switches
// to pbrShader_ and draws every sphere with its own PBRMaterial;
// renderShadowPass() depth-draws the spheres too (through the same
// shadowShader_ every other shadow-casting geometry uses -- shadow.vert only
// reads position, so it doesn't care which lighting model a mesh's *color*
// pass uses).
//
// Phase 7b adds a skybox background and an HDR + tonemapping pipeline,
// without changing the shape of the loop above:
//   - render() no longer draws the lit scene straight to the default
//     framebuffer: it renders into hdrFramebuffer_ (see framebuffer.hpp), a
//     floating-point (GL_RGBA16F) off-screen target, then resolves that to
//     the window with one fullscreen post-process pass
//     (postProcessShader_ + postProcessQuad_, assets/shaders/
//     postprocess.vert/.frag) that Reinhard-tonemaps and gamma-corrects.
//     This is what lets kPointLights[0] (application.cpp) carry an
//     intensity above 1.0 and still roll off smoothly near the light
//     instead of hard-clipping to a flat white disc the way writing
//     straight to the (8-bit, clamped) default framebuffer would.
//   - skybox_ (see skybox.hpp) is drawn as the scene's background: a
//     6-face procedural-sky cubemap, rendered LAST into hdrFramebuffer_
//     (after entities_/the ground plane) via a GL_LEQUAL depth trick so it
//     only shows through pixels nothing else was drawn to, replacing the
//     old flat kClearR/G/B glClearColor as what's actually visible behind
//     the scene (that glClearColor call still runs first, as a safety
//     fallback in case the skybox draw is ever skipped/fails).
//
// Phase 10 replaces pbr.frag's flat placeholder ambient term with real
// image-based lighting (see ibl_probe.hpp): iblProbe_ is built once, in the
// constructor, from skybox_'s own cubemap -- a diffuse irradiance cubemap, a
// mipmapped GGX-prefiltered specular cubemap, and a 2D BRDF integration LUT,
// the standard split-sum approximation (Karis, "Real Shading in Unreal
// Engine 4"). render() binds all three onto pbrShader_ (fixed texture units
// 4/5/6 -- see kIrradianceMapTextureUnit/kPrefilterMapTextureUnit/
// kBrdfLutTextureUnit in application.cpp, chosen not to collide with unit 0
// (albedo map)/1 (normal map)/2 (Phase 11's ORM map)/3 (shadow map)) right
// alongside every other per-frame pbrShader_ uniform upload, once per frame
// -- no change to render()'s overall shape, since iblProbe_'s own three
// maps never change after startup (this engine's skybox is static for the
// whole run).
//
// Phase 11 adds two features on top of Phase 9/10's "full PBR" pipeline:
//   - Textured PBR materials (see pbr_material.hpp's Phase 11 comment): two
//     of sphereInstances_' materials now bind an albedo + packed ORM texture
//     instead of flat scalar values -- no new Application-level rendering
//     step, just different PBRMaterial construction arguments (see
//     application.cpp's sphere-instance construction).
// Phase 13b adds frustum culling on top of every phase above, without
// changing what a normally-positioned camera actually sees:
//   - Every Mesh now carries its own local-space bounding sphere (see
//     mesh.hpp's BoundingSphere/Mesh::boundingSphere()), computed once at
//     construction time from its vertex positions.
//   - A small Frustum class (frustum.hpp) extracts the 6 view-frustum planes
//     from the combined view-projection matrix once per frame (the standard
//     Gribb/Hartmann method) and tests a world-space bounding sphere against
//     them.
//   - render() builds one Frustum per frame from camera_'s current view/
//     projection, then tests every drawable's own bounding sphere (each
//     Model node's mesh, via Model::draw()'s new frustum/cullStats
//     parameters; the ground plane; each PBR sphere instance) against it,
//     skipping the draw call for anything provably entirely outside --
//     never the *shadow* pass (renderShadowPass() draws everything
//     depth-only, unconditionally, exactly as before -- camera-frustum
//     culling only applies to what the camera itself would rasterize).
//     A running CullStats total/culled count is logged periodically so a
//     headless run can confirm culling is actually skipping draw calls, not
//     a no-op -- see kCullLogFrameInterval in application.cpp.
//   - frustumCullDemoMode_ (ENGINE_FRUSTUM_CULL_DEMO env var, same
//     getenv-gated pattern as cameraDemoMode_) parks the camera at its usual
//     default position but pointed *away* from the scene instead of at it,
//     purely so headless verification has a deterministic way to prove
//     culling drops the draw count instead of only ever exercising the
//     "everything stays in view" path a normal run takes.
//   - Bloom: after the existing lit-scene-+-skybox render into
//     hdrFramebuffer_, a bright-pass extraction (bloomExtractShader_, into
//     brightFramebuffer_) isolates pixels above a luminance threshold, a
//     ping-ponged separable Gaussian blur (blurShader_, alternating between
//     pingpongFramebuffer0_/pingpongFramebuffer1_, both reused via the same
//     Framebuffer class rather than one-off FBO code) softens that into a
//     glow, and the final postprocess pass (postProcessShader_/
//     postprocess.frag) additively blends the blurred result onto the HDR
//     color buffer before Reinhard tonemapping -- so bloom is tonemapped
//     exactly like everything else, not a separate hacky overlay. All three
//     new Framebuffers are sized at half the window's resolution (see
//     kBloomDownsampleFactor) -- bloom is a soft, low-frequency glow, so a
//     half-res blur costs a quarter the per-pixel work of a full-res one
//     with no visible loss of quality.
//
// Phase 13c replaces Phase 7a/7b's single whole-scene shadow map with
// Cascaded Shadow Maps (CSM), without changing the shadow-casting light
// itself (still just the one directional light) or the overall render()/
// renderShadowPass() shape:
//   - kCascadeCount (3) separate ShadowMap instances (shadowCascades_
//     below), each covering a different depth range ("cascade") of the
//     camera's own view frustum -- see computeCascades() in application.cpp
//     for the split-depth scheme (a blend of logarithmic and uniform
//     splits, the standard "practical split scheme") and per-cascade
//     frustum-fitting (unproject that depth range's 8 frustum corners via
//     Camera::getProjectionMatrix()'s near/far overload + frustum.hpp's
//     frustumCornersWorldSpace(), then fit a tight orthographic projection
//     around their light-space bounding box). Replacing the old single
//     fixed-size ortho projection covering the *whole* scene, this gives
//     cascades near the camera a much smaller world-space area per
//     shadow-map texel (i.e. higher effective shadow resolution close up)
//     without needing a bigger shadow map.
//   - renderShadowPass() now depth-renders the whole scene once per cascade
//     (kCascadeCount total passes, one full scene draw each) rather than
//     once total.
//   - basic.frag/pbr.frag each pick which cascade a given fragment belongs
//     to (comparing its view-space depth, a new varying, against the
//     cascades' own split depths) and sample that cascade's own shadow map
//     with its own light-space matrix, blending across a small transition
//     band near each split rather than a hard cutoff (see those shaders'
//     own Phase 13c comments) to avoid a visible seam where shadow
//     resolution/aliasing changes abruptly between cascades.
//
// MSAA HDR framebuffer bug fix (post-Phase-13c): Window has requested a
// multisampled default framebuffer since Phase 6, but every phase from 7b
// onward actually renders the real 3D scene into hdrFramebuffer_ instead --
// a plain single-sample GL_TEXTURE_2D + GL_RENDERBUFFER target, not
// multisample-capable at all. The window's own MSAA setting was therefore
// only ever antialiasing the final fullscreen tonemap/bloom-composite quad
// (a flat rectangle with no geometric edges), i.e. doing nothing for the
// jagged edges MSAA exists to fix. Fixed by:
//   - hdrFramebuffer_ is now constructed with a real sample count (see
//     engine::kRequestedMsaaSamples in window.hpp, the same count Window
//     requests for the default framebuffer, for consistency) -- see
//     framebuffer.hpp's own MSAA bug-fix comment for how Framebuffer itself
//     grew multisample support.
//   - hdrResolveFramebuffer_ (new member, below) is a same-size,
//     single-sample Framebuffer. render() now resolves hdrFramebuffer_'s
//     multisample color buffer into it (Framebuffer::resolveTo(), a
//     glBlitFramebuffer) once per frame, right after the scene+skybox color
//     pass finishes and before bloom extraction/the final tonemap pass --
//     both of which now read hdrResolveFramebuffer_ instead of
//     hdrFramebuffer_ directly, since a multisample color texture isn't
//     sampler2D-compatible (see framebuffer.hpp).
//   - Nothing else changes: renderShadowPass()/shadowCascades_ (a separate,
//     already-single-sample depth-only pass, never part of this bug) and
//     the bloom bright-pass/blur pipeline's own Framebuffer instances
//     (brightFramebuffer_/pingpongFramebuffer0_/1_, which already only ever
//     read/write hdrFramebuffer_'s -- now hdrResolveFramebuffer_'s --
//     resolved color, never its raw multisample buffer) are unaffected.
//
// Phase 13d replaces basic.frag/pbr.frag's brute-force "loop over every
// point/spot light for every fragment" with GPU compute-shader clustered
// forward shading (see cluster_light_culler.hpp for the full technique):
//   - clusterLightCuller_ owns two compute passes: one that builds every
//     cluster's view-space AABB (run exactly once, in this constructor --
//     it depends only on the fixed projection matrix/window size, never the
//     view matrix, so it never needs recomputing after startup) and one
//     that culls every light against those AABBs to build a per-cluster
//     light index list (run every frame in render(), since the *view*
//     matrix changes whenever the camera moves even though this engine's
//     own lights never do).
//   - basic.frag/pbr.frag each compute which cluster the current fragment
//     falls into (from its screen position + view-space depth, the exact
//     same logarithmic-Z formula the AABB pass used) and loop only over
//     that cluster's own culled light list (read from an SSBO) instead of
//     every light unconditionally -- each light's own position/color/
//     attenuation/cone-angle data is untouched, still the same
//     uPointLights[]/uSpotLights[] uniform arrays Phase 7a introduced.
//   - clusterDebugMode_ (ENGINE_CLUSTER_DEBUG, same getenv-gated pattern as
//     cameraDemoMode_/frustumCullDemoMode_) tints each fragment by its own
//     cluster's light count, proving the per-cluster lists actually vary
//     rather than only compiling.
//
// Phase 13e replaces the Phase 7b/10 procedural 6-face skybox with a real
// HDRI (equirectangular Radiance .hdr) environment as skybox_'s own source,
// without changing render()'s shape or IBLProbe's own convolution logic at
// all:
//   - assets/textures/hdri/sky.hdr is a real, valid equirectangular HDR
//     image -- generated entirely offline (tools/generate_hdri.py, numpy),
//     matching this project's established "procedurally generate every
//     texture, no network fetch" convention (see README's Phase 4/7b/11
//     notes) -- carrying a small, extremely bright sun disk (intensity far
//     above 1.0, HDR's whole reason to exist) plus a color-temperature sky
//     gradient, unlike the old procedural skybox's flat 8-bit gradient.
//   - engine::loadHdrEquirectangularAsCubemap() (hdri_loader.hpp) GPU-
//     converts that equirectangular image into a floating-point
//     (GL_RGBA16F) GL_TEXTURE_CUBE_MAP via a new one-time render pass
//     (equirectToCubemapShader_ below, pairing the existing
//     cubemap_capture.vert with a new equirect_to_cubemap.frag) -- the same
//     "temporary FBO, built and torn down within one function" shape
//     IBLProbe's own constructor already uses for its convolution passes.
//   - skybox_ gained a second constructor (see skybox.hpp) that takes
//     ownership of an already-built cubemap instead of loading 6 PNG faces
//     -- application.cpp's buildSkybox() picks between the two based on
//     ENGINE_USE_PROCEDURAL_SKYBOX (same getenv-gated pattern as every other
//     env-var toggle in this class), so the Phase 7b/10 procedural path
//     stays available as a fallback/reference rather than being deleted.
//   - Nothing downstream of skybox_.textureId() changes: render()'s
//     skybox_.draw() call, and iblProbe_'s convolution of skybox_'s own
//     cubemap, are exactly the code that already existed -- an HDRI is just
//     a different (and, unlike the old procedural cubemap, genuinely
//     floating-point/HDR) source for the same texture handle both already
//     consumed. The background draw's values flow into hdrFramebuffer_
//     exactly like the old procedural skybox's did, so they pass through
//     the same Reinhard tonemap in the final postprocess pass -- no separate
//     tonemap step was ever needed for the sky background, and none is
//     needed now.
//
// Phase 13f adds Screen-Space Ambient Occlusion (SSAO), feeding into
// basic.frag/pbr.frag's existing ambient term as a second, per-pixel,
// geometry-derived occlusion factor alongside their existing material-
// authored uAO/ORM-map scalar:
//   - renderSSAO() (called from render(), right after the shadow pass, since
//     it needs to hand shader_/pbrShader_ a finished texture to sample while
//     shading) runs three screen-space passes: a lightweight geometry
//     pre-pass (ssaoGBuffer_ -- a view-space normal color attachment plus a
//     real, samplable depth *texture*, see framebuffer.hpp's Phase 13f
//     depthAsTexture option) that Model::drawNormalDepth()/the ground
//     plane/every PBR sphere render into; the classic Crysis/John Chapman
//     hemisphere-kernel occlusion pass (ssaoRaw_, see ssao.frag) that
//     reconstructs each pixel's view-space position from that depth texture
//     plus the inverse projection matrix (rather than a second G-buffer
//     position render target -- the "buffer-lighter" approach this phase's
//     brief calls out as preferable for a scene this simple); and a small
//     box-blur pass (ssaoBlurred_, see ssao_blur.frag) that smooths the
//     kernel pass's per-pixel noise, sized to exactly cancel the tileable
//     rotation-noise texture's own repeat period (see ssaoKernel_/ssao.hpp).
//   - basic.frag/pbr.frag each sample ssaoBlurred_ at their own screen
//     position (gl_FragCoord.xy / uScreenSize, the same convention Phase
//     13d's clustered-lighting tile lookup already established) and
//     multiply it into their ambient term, alongside (not instead of) their
//     existing material AO -- see pbr.frag's own uSSAOMap comment for why
//     both are multiplied together rather than one replacing the other.
//     Applied to both shading models (not left PBR-only) since SSAO is a
//     purely screen-space effect independent of which BRDF produced a
//     pixel's color -- see basic.frag's own comment.
//   - ENGINE_SSAO_DISABLE (ssaoDisabled_) forces both shaders' ambient SSAO
//     factor to a flat 1.0 (no occlusion) instead of sampling ssaoBlurred_,
//     letting a headless run isolate SSAO's own exact contribution by
//     comparing an on/off screenshot pair from the same build.
//     ENGINE_SSAO_DEBUG (ssaoDebugMode_) makes the final postprocess pass
//     show ssaoRaw_ (the pre-blur occlusion buffer) directly, so that
//     buffer's own noise/halo/banding characteristics can be checked in
//     isolation from its (deliberately subtle) blended contribution to the
//     final lit image.
//
// Phase 13g adds Screen-Space Reflections (SSR), refining pbr.frag's
// existing IBL-only specular term (the prefiltered-environment-cubemap
// reflection, Phase 10) for the PBR sphere grid's smoothest/most metallic
// spheres with a real screen-space ray-marched reflection of the actual
// nearby scene, falling back to that same IBL term wherever SSR has no
// data (screen edges, grazing view angles, or high-roughness surfaces) --
// see pbr.frag's own Phase 13g comment for the ray-march/fade math itself.
//   - SSR reuses SSAO's own geometry pre-pass (ssaoGBuffer_'s view-space
//     normal + real depth texture, see this class's own Phase 13f comment)
//     as the "what does the visible scene actually look like" input the
//     ray march tests against, rather than building a second, redundant
//     G-buffer -- the same view/projection this frame's SSAO pass already
//     rendered it with is exactly what SSR's own ray march needs too.
//   - The classic ordering problem this technique always has to solve: a
//     ray march needs to sample the scene's own already-rendered color to
//     know what a reflection ray "hits", but this engine's PBR shading
//     (pbr.frag) computes a fragment's *entire* final lit color -- direct
//     lighting, IBL ambient, all of it -- in one single forward pass, so
//     there's no separate "the scene's shaded color, minus this one
//     surface's own reflection" buffer to read mid-pass. render() solves
//     this with a second, short compositing pass (renderSSRComposite())
//     rather than restructuring the whole engine into a deferred renderer:
//     the ordinary per-frame draw of entities_/ground/the PBR sphere grid
//     runs exactly as before (pbrShader_'s uSSREnabled uniform explicitly
//     off, so every pixel's IBL-only specular term is computed exactly as
//     Phase 10 left it); once the whole opaque scene + skybox is drawn and
//     resolved into hdrResolveFramebuffer_ (a real, finished, sampler2D-
//     readable color buffer), renderSSRComposite() redraws ONLY the PBR
//     sphere grid a second time, into the same hdrFramebuffer_ (GL_LEQUAL
//     depth test lets its fragments pass against the depth values the first
//     draw already wrote for the exact same geometry), this time with
//     uSSREnabled on -- pbr.frag ray-marches against ssaoGBuffer_'s depth
//     and, where it finds a hit, blends hdrResolveFramebuffer_'s already-
//     rendered color into that fragment's specular term in place of (or
//     blended with, via the fade factors) the plain IBL term. render() then
//     re-resolves hdrFramebuffer_ into hdrResolveFramebuffer_ a second time
//     so bloom/the final tonemap pass -- unchanged themselves -- read this
//     pass's own blended result. Only the PBR sphere grid is ever redrawn
//     this way (matching Phase 9's own "PBR path gets the new features,
//     Blinn-Phong stays as reference" precedent) -- entities_/the ground
//     plane are drawn exactly once per frame, same as every prior phase.
//   - ENGINE_SSR_DISABLE (ssrDisabled_) skips renderSSRComposite() and the
//     second resolveTo() entirely, leaving hdrResolveFramebuffer_ exactly as
//     the first (IBL-only) pass alone produced it -- same getenv-gated
//     before/after-comparison pattern as ssaoDisabled_/ENGINE_SSAO_DISABLE.
//
// Phase 8a promotes Phase 6's Entity/entities_ into a real (if deliberately
// small) ECS -- see ecs.hpp for the full design writeup. entities_ (a
// std::vector<Entity>, each hardcoding a Transform + optional Model as two
// fixed fields) becomes registry_ (an EntityRegistry): entities are now
// opaque EntityIds, and their Transform/Model data lives in
// ComponentPool<Transform>/ComponentPool<ModelComponent> pools keyed by
// entity id instead. This is a structural refactor, not a behavior change --
// the constructor still creates exactly one entity (the Phase 5 scene.obj
// model) and render()/renderShadowPass()/renderSSAO() still draw it exactly
// once per frame, in the same place in the frame; they just do it via
// registry_.each<ModelComponent>(...) (looking up each entity's Transform
// component alongside its ModelComponent) instead of `for (const Entity&
// entity : entities_)`. Every other system this file's earlier phase
// comments describe entities_/the ground plane/the PBR sphere grid
// participating in (shadows, SSAO's g-buffer, frustum culling) is
// unaffected -- only what stores the Transform+Model pair changed, not how
// many times or in what order it's drawn.
//
// Phase 8b adds real save/load for registry_'s entity data (see
// scene_serialization.hpp): the constructor's scene setup no longer builds
// that one entity directly in C++ by default -- it calls loadScene()
// against a checked-in assets/scenes/default.json instead (still exactly
// the same one Transform+ModelComponent entity, now as data instead of
// code), with the old hardcoded construction kept behind
// ENGINE_LEGACY_SCENE as a documented escape hatch. See this class's own
// Phase 8b constructor comment (application.cpp) for the full before/after.
//
// Phase 8c adds a Dear ImGui debug overlay (see debug_ui.hpp for the
// context/backend-lifecycle wrapper) without touching the render pipeline
// above at all -- renderDebugUI() is called from the very end of render(),
// after the tonemap/bloom postprocess pass has already resolved onto the
// default framebuffer (see render()'s own tail), so every ImGui widget is
// drawn straight onto the final, already-tonemapped image rather than
// participating in the HDR/bloom/SSR pipeline itself (an ImGui draw call
// going through Reinhard tonemapping would be nonsensical -- its own colors
// are already meant for an 8-bit display, not a scene-radiance HDR buffer).
//   - debugUI_ (a DebugUI, see debug_ui.hpp) owns the ImGui context + GLFW/
//     OpenGL3 backend lifecycle; renderDebugUI() (this class, defined in
//     application.cpp) owns *what the panel shows* -- it has direct access
//     to this class's own private state (registry_, ssaoDisabled_,
//     ssrDisabled_, ssaoDebugMode_, clusterDebugMode_, frameCount_), which
//     is exactly what a debug inspector needs to read and, for the render-
//     pass toggles, mutate at runtime.
//   - ENGINE_SHOW_DEBUG_UI (unset by default) gates the whole thing at
//     startup, same getenv-gated pattern as every other flag in this class
//     -- see debugUI_'s own construction in the constructor and DebugUI's
//     own header comment for why "disabled" means "never calls a single
//     ImGui function", not just "an invisible window": this project's
//     headless verification depends on every prior phase's screenshot
//     staying pixel-identical, so the default (hidden) path leaves the
//     existing render pipeline completely untouched rather than drawing an
//     invisible-but-still-composited overlay.
//   - The panel itself (see renderDebugUI()'s definition in application.cpp)
//     is deliberately modest: a frame-time/FPS readout, checkboxes for the
//     existing SSAO/SSR/cluster-debug toggles (ssaoDisabled_ etc. were
//     already plain bool members render() re-reads every frame -- see
//     Phase 13f/13g/13d's own comments above -- so wiring an
//     ImGui::Checkbox directly to one of them makes it live-toggleable with
//     no further plumbing needed), and an entity inspector
//     (registry_.each<Transform>(...), editable via ImGui::DragFloat3) --
//     see "What NOT to do" in this phase's own brief for why scene save/
//     load buttons, gamepad support, and physics visualization aren't here.

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/camera.hpp"
#include "engine/cluster_light_culler.hpp"
#include "engine/debug_ui.hpp"
#include "engine/ecs.hpp"
#include "engine/framebuffer.hpp"
#include "engine/ibl_probe.hpp"
#include "engine/input.hpp"
#include "engine/material.hpp"
#include "engine/mesh.hpp"
#include "engine/pbr_material.hpp"
#include "engine/resource_manager.hpp"
#include "engine/shader.hpp"
#include "engine/shadow_map.hpp"
#include "engine/skybox.hpp"
#include "engine/ssao.hpp"
#include "engine/transform.hpp"
#include "engine/window.hpp"

namespace engine {

class Application {
public:
    // width/height/title are passed straight through to the Window.
    //
    // maxFrames == 0 (the default) means run until the window should close
    // or ESC is pressed -- normal interactive behavior. A nonzero value
    // makes run() return on its own after that many frames; this exists
    // solely so the headless verification path (Xvfb has no real window
    // manager or keyboard to close the window / send ESC) can terminate
    // deterministically instead of relying on an external timeout/kill. See
    // main.cpp's ENGINE_MAX_FRAMES handling.
    Application(int width, int height, const std::string& title, std::uint64_t maxFrames = 0);

    void run();

    // Phase 13c: number of cascades in the CSM implementation (see this
    // header's own Phase 13c comment and application.cpp's computeCascades()
    // for why 3). Declared here, not only as an application.cpp-local
    // constant, because it also sizes shadowCascades_ below and the
    // std::array parameter renderShadowPass() takes -- both declared in this
    // header, so both need it visible here too.
    static constexpr int kCascadeCount = 3;

private:
    void update(double deltaTime, const InputState& input);
    void render();

    // Phase 7a: renders the whole scene depth-only from the directional
    // light's point of view into shadowMap_ (see shadow_map.hpp), using
    // shadowShader_. Called once per frame from render(), before the main
    // pass, and restores the window's own viewport (and default framebuffer
    // binding) before returning, since ShadowMap::bindForWriting() points
    // the viewport at the shadow map's own (typically much smaller)
    // resolution.
    //
    // Phase 13c: now renders the whole scene once per cascade (into
    // shadowCascades_[i], using lightSpaceMatrices[i]) instead of once
    // total -- see this header's own Phase 13c comment.
    void renderShadowPass(const std::array<glm::mat4, kCascadeCount>& lightSpaceMatrices);

    // Phase 13f: SSAO's three screen-space passes (see this header's own
    // Phase 13f comment below and assets/shaders/gbuffer.vert/.frag,
    // ssao.frag, ssao_blur.frag) -- a lightweight view-space normal + depth
    // geometry pre-pass (into ssaoGBuffer_), the kernel-sampling pass (into
    // ssaoRaw_), then a small blur pass (into ssaoBlurred_, what
    // shader_/pbrShader_ actually sample while shading, see render()).
    // Called once per frame from render(), after the shadow pass and before
    // the main color pass -- like renderShadowPass(), it needs this frame's
    // own view/projection matrices (the camera may have moved) and restores
    // nothing itself; render() rebinds the default framebuffer/window
    // viewport immediately afterward via hdrFramebuffer_.bindForWriting().
    void renderSSAO(const glm::mat4& view, const glm::mat4& projection);

    // Phase 13g: the SSR compositing pass -- redraws ONLY the PBR sphere
    // grid (sphereInstances_), a second time this frame, into
    // hdrFramebuffer_ (GL_LEQUAL depth test, no clear -- see this class's
    // own Phase 13g header comment for why this needs to be a second draw
    // rather than folded into the ordinary one). Called from render() after
    // the whole scene + skybox has been drawn and resolved into
    // hdrResolveFramebuffer_ once already -- that resolved buffer is what
    // pbr.frag's screen-space ray march (uSSREnabled, set to 1 only for
    // this call) samples as "the already-rendered scene". Builds its own
    // Frustum from `view`/`projection` (identical construction to render()'s
    // own, just re-derived here rather than threaded through as a parameter,
    // matching renderShadowPass()/renderSSAO()'s own "just the matrices it
    // needs" shape) so a sphere the first pass culled is skipped here too.
    void renderSSRComposite(const glm::mat4& view, const glm::mat4& projection);

    // Phase 8c: builds and draws the debug overlay panel -- see this
    // header's own Phase 8c comment above and debug_ui.hpp. Called last from
    // render(), after the tonemap/bloom postprocess pass has already resolved
    // onto the default framebuffer, so ImGui's own draw calls land straight
    // on top of the final image rather than participating in the HDR
    // pipeline. A no-op (via debugUI_.enabled() -- see DebugUI's own header
    // comment) unless ENGINE_SHOW_DEBUG_UI was set at startup.
    void renderDebugUI();

    // Phase 9: one sphere in the PBR test-grid -- its own placement
    // (Transform) plus its own PBRMaterial (metallic/roughness/albedo all
    // differ per instance; the mesh geometry itself, sphereMesh_, is shared
    // by every instance, so this struct deliberately does NOT hold a Mesh of
    // its own). Analogous to a registry_ entity's (Transform, ModelComponent)
    // pair (see ecs.hpp), but PBRMaterial-based instead of Model-based, and
    // copyable (PBRMaterial holds no exclusive GL handle --
    // see pbr_material.hpp) so std::vector<SphereInstance> needs no move-only
    // ceremony.
    struct SphereInstance {
        Transform transform;
        PBRMaterial material;
    };

    // Declaration order matters here: window_ must construct (and create
    // the GL context) before resources_ is used to load anything (Shader/
    // Model/ShadowMap/Mesh construction all do GL calls) -- resources_
    // itself does no GL work at construction (its caches start empty), but
    // shader_ is initialized from it in the constructor's member-
    // initializer list, so resources_ must still be declared (and thus
    // constructed) before shader_. shadowShader_/skyboxShader_/
    // postProcessShader_ likewise need resources_ constructed first.
    // shader_ must in turn come before registry_ and groundMaterial_:
    // building the scene's entity calls resources_.getModel(path,
    // *shader_), and each resulting Model's per-mesh Materials (plus
    // groundMaterial_, built directly against shader_ rather than through
    // Model) hold a pointer into that same shader_, so shader_ must
    // outlive all of them. hdrFramebuffer_/hdrResolveFramebuffer_ are sized
    // from window_.getSize() in their own initializers, so they too must
    // come after window_ (anywhere after is fine -- neither has any other
    // dependency). camera_ does no GL work so its position relative to the
    // above is unconstrained.
    // iblProbe_ (Phase 10) must come after skybox_ (it convolves skybox_'s
    // own cubemap, via skybox_.textureId()) and after irradianceShader_/
    // prefilterShader_/brdfShader_ (its constructor uses all three, once).
    Window window_;
    ResourceManager resources_;
    std::shared_ptr<Shader> shader_;
    // Phase 7a: a second, minimal program (assets/shaders/shadow.vert/
    // shadow.frag) used only to render the shadow map's depth pass -- see
    // renderShadowPass(). Routed through resources_ like shader_, even
    // though nothing else currently requests the same (vertex, fragment)
    // pair, for the same reason every other asset load goes through the
    // cache: one consistent loading path, not because sharing is expected
    // here. Still just this one program for all kCascadeCount cascades
    // (Phase 13c, below) -- shadow.vert only reads position, so one
    // depth-only program serves every cascade's own depth pass.
    std::shared_ptr<Shader> shadowShader_;
    // Phase 7b: the skybox's own program (assets/shaders/skybox.vert/
    // skybox.frag) and the HDR-resolve fullscreen pass's program
    // (assets/shaders/postprocess.vert/postprocess.frag) -- both routed
    // through resources_ for the same "one consistent loading path" reason
    // as shadowShader_ above.
    std::shared_ptr<Shader> skyboxShader_;
    std::shared_ptr<Shader> postProcessShader_;
    // Phase 11: bloom's two extra passes' own programs -- both pair
    // postprocess.vert (an ordinary fullscreen quad, no model/view/
    // projection) with a new fragment shader, the same "reuse the existing
    // screen-space vertex stage" pattern brdfShader_ already uses (see
    // Phase 10's comment above). bloomExtractShader_ thresholds
    // hdrFramebuffer_'s color by luminance (assets/shaders/
    // bloom_extract.frag); blurShader_ is the separable Gaussian blur pass,
    // run twice per ping-pong iteration (horizontal, then vertical --
    // assets/shaders/blur.frag), reused across every iteration in
    // render()'s bloom loop rather than needing a second program.
    std::shared_ptr<Shader> bloomExtractShader_;
    std::shared_ptr<Shader> blurShader_;
    // Phase 13f: SSAO's three programs -- gbufferShader_ (assets/shaders/
    // gbuffer.vert/.frag) is the lightweight view-space-normal-plus-depth
    // geometry pre-pass; ssaoShader_ (assets/shaders/ssao.frag, paired with
    // postProcessVertexShaderPath like every other fullscreen pass) is the
    // hemisphere-kernel occlusion sampling pass; ssaoBlurShader_ (assets/
    // shaders/ssao_blur.frag) is the small box blur that smooths its noisy
    // output. Routed through resources_ for the same "one consistent
    // loading path" reason as every other shader above.
    std::shared_ptr<Shader> gbufferShader_;
    std::shared_ptr<Shader> ssaoShader_;
    std::shared_ptr<Shader> ssaoBlurShader_;
    // Phase 9: the PBR pass's own program (assets/shaders/pbr.vert/
    // pbr.frag), routed through resources_ for the same reason as every
    // other shader above. Must be constructed before sphereInstances_ below
    // (each instance's PBRMaterial holds a pointer into *pbrShader_, the same
    // "shader_ must outlive registry_/groundMaterial_" constraint this
    // comment block already describes for the Blinn-Phong path).
    std::shared_ptr<Shader> pbrShader_;
    // Phase 10: the three one-time IBL precompute programs (see
    // ibl_probe.hpp) -- routed through resources_ like every other shader,
    // even though (like shadowShader_) nothing else currently requests the
    // same pairs. Must be constructed before iblProbe_ below, which uses
    // them once, in its own constructor, and keeps no reference to them
    // afterward.
    std::shared_ptr<Shader> irradianceShader_;
    std::shared_ptr<Shader> prefilterShader_;
    std::shared_ptr<Shader> brdfShader_;
    // Phase 13e: the equirectangular-HDRI-to-cubemap conversion pass (see
    // hdri_loader.hpp) -- pairs cubemap_capture.vert (reused from Phase 10)
    // with a new equirect_to_cubemap.frag. Routed through resources_ like
    // every other one-time precompute shader above; must be constructed
    // before skybox_ below, which (via buildSkybox() in application.cpp)
    // uses it once, in the constructor, to build the HDR cubemap skybox_
    // then takes ownership of.
    std::shared_ptr<Shader> equirectToCubemapShader_;
    // Phase 13d: clustered light culling's two compute passes + the two
    // SSBOs they build (see cluster_light_culler.hpp) -- constructed
    // directly (not through resources_) since exactly one instance ever
    // exists and nothing else in this engine would share it, the same
    // "direct member, no ResourceManager" choice shadowCascades_ already
    // makes for the same reason. Only needs window_ (a live GL context)
    // constructed first, like every other GL-handle-owning member here.
    ClusterLightCuller clusterLightCuller_;
    // Phase 7a: the directional light's depth-only render target. Fixed
    // resolution (see application.cpp's kShadowMapWidth/Height), independent
    // of the window's own framebuffer size.
    //
    // Phase 13c: now kCascadeCount (3) separate instances, one per CSM
    // cascade, rather than one covering the whole scene -- see shadow_map.hpp's
    // own Phase 13c comment for why N instances rather than one array
    // texture. All list-initialized together in the constructor's member-
    // initializer list (each element move-constructed from a temporary
    // ShadowMap(kShadowMapWidth, kShadowMapHeight), std::array<T, N>'s usual
    // aggregate-initialization), same "single self-contained initializer"
    // style the old single shadowMap_ member had.
    std::array<ShadowMap, kCascadeCount> shadowCascades_;
    // Phase 7b: the off-screen floating-point target render() draws the
    // whole lit scene (+ skybox) into, before postProcessShader_ resolves
    // it to the window -- see framebuffer.hpp. Sized once, from the
    // window's real framebuffer size at construction time; Window has no
    // resize callback/event of its own for this to react to (see
    // window.hpp), so -- like every other fixed-at-construction GL
    // resource in this engine -- this only ever needs to be sized once.
    //
    // MSAA HDR framebuffer bug fix: now constructed multisampled (see this
    // header's own MSAA bug-fix comment above and framebuffer.hpp) instead
    // of the single-sample target it used to be -- render() can no longer
    // sample its color directly (a multisample texture isn't
    // sampler2D-compatible); hdrResolveFramebuffer_ right below is what
    // bloom extraction/the final tonemap pass actually read.
    Framebuffer hdrFramebuffer_;
    // MSAA HDR framebuffer bug fix: a same-size, single-sample sibling of
    // hdrFramebuffer_ above -- render() resolves hdrFramebuffer_'s
    // multisample color buffer into this one (Framebuffer::resolveTo(), a
    // glBlitFramebuffer) once per frame, right after the scene+skybox color
    // pass finishes, so bloom extraction and the final tonemap/gamma pass
    // can keep reading an ordinary sampler2D-compatible HDR color buffer
    // exactly as they did before this bug fix -- neither needed to change
    // *how* they sample, only *which* Framebuffer they sample from.
    Framebuffer hdrResolveFramebuffer_;
    // Phase 11: bloom's own off-screen targets, all the same Framebuffer
    // class as hdrFramebuffer_ above (deliberately reused rather than a new
    // one-off FBO type -- see this class's Phase 11 header comment), each
    // sized at half the window's real framebuffer resolution (see
    // kBloomDownsampleFactor in application.cpp). brightFramebuffer_ holds
    // the bright-pass extraction's output; pingpongFramebuffer0_/1_
    // alternate as the separable Gaussian blur's read/write targets each
    // pass (a single Framebuffer can't be both simultaneously -- sampling a
    // texture that's also the current draw target is undefined behavior,
    // which is exactly why ping-ponging between two targets is the standard
    // separable-blur pattern). Each of these having its own (here, unused)
    // depth renderbuffer is a small, harmless side effect of reusing
    // Framebuffer as-is rather than adding a "skip the depth attachment"
    // parameter for a feature this size.
    Framebuffer brightFramebuffer_;
    Framebuffer pingpongFramebuffer0_;
    Framebuffer pingpongFramebuffer1_;
    // Phase 13f: SSAO's own three render targets, all sized at
    // 1/kSSAODownsampleFactor of the window's real framebuffer resolution
    // (application.cpp) -- primarily a performance tradeoff (this
    // technique's per-pixel cost, kSSAOKernelSize texture fetches per pixel
    // in the kernel pass alone, is steep enough on this project's software-
    // rasterizer headless verification target to matter for wall-clock run
    // time), the same half-res idea bloom's own kBloomDownsampleFactor
    // already uses, not because SSAO is conceptually as soft/low-frequency
    // as bloom's glow -- Framebuffer's own GL_LINEAR filtering upsamples the
    // result back onto full-res geometry with no visible halo/blockiness at
    // this scene's contact-crease scale (confirmed by this phase's own
    // screenshot/pixel-sample verification).
    //   - ssaoGBuffer_: this pass's own lightweight geometry pre-pass output
    //     -- a view-space normal in its ordinary RGBA16F color attachment,
    //     and (the reason this one Framebuffer is built with
    //     depthAsTexture = true, see framebuffer.hpp's Phase 13f comment) a
    //     real, samplable depth texture ssao.frag reconstructs each pixel's
    //     view-space position from.
    //   - ssaoRaw_: the kernel-sampling pass's raw, per-pixel-noisy
    //     occlusion output (see ssao.frag) -- an ordinary (unused-depth,
    //     same as brightFramebuffer_ above) Framebuffer reused purely for
    //     its RGBA16F color attachment, only .r of which SSAO actually uses.
    //   - ssaoBlurred_: the small box-blur pass's smoothed final output --
    //     what shader_/pbrShader_ actually sample while shading (see
    //     render()) and what the ambient term in basic.frag/pbr.frag
    //     multiplies in.
    Framebuffer ssaoGBuffer_;
    Framebuffer ssaoRaw_;
    Framebuffer ssaoBlurred_;
    // Phase 13f: SSAO's hemisphere sample kernel + tileable rotation-noise
    // texture -- see ssao.hpp. Only needs window_'s GL context to exist,
    // like clusterLightCuller_ above.
    SSAOKernel ssaoKernel_;
    // Phase 7b: the sky cubemap background -- see skybox.hpp. Loads its own
    // texture data directly (not through resources_/Texture, see skybox.hpp's
    // class comment on why it's a separate small class).
    //
    // Phase 13e: now built by buildSkybox() (application.cpp) from a real
    // HDRI by default -- a floating-point cubemap GPU-converted from
    // assets/textures/hdri/sky.hdr, see hdri_loader.hpp -- rather than always
    // the old 6-PNG procedural cubemap; ENGINE_USE_PROCEDURAL_SKYBOX switches
    // back to the latter, kept as a fallback/reference rather than deleted.
    Skybox skybox_;
    // Phase 10: the precomputed diffuse-irradiance + prefiltered-specular
    // cubemaps and BRDF LUT that drive pbr.frag's real image-based-lighting
    // ambient term (see ibl_probe.hpp) -- built once here, from skybox_'s own
    // cubemap (skybox_.textureId()), so it must be declared (and thus
    // constructed) after skybox_ and after irradianceShader_/
    // prefilterShader_/brdfShader_ above.
    IBLProbe iblProbe_;
    // Phase 7a: a hand-built ground plane (see mesh.hpp's makeGroundPlane())
    // with a bound normal map, drawn directly alongside registry_'s entities
    // rather than through Model/scene.obj -- see this header's Phase 7a
    // comment above for why. Constructed directly as members (not an
    // EntityRegistry entity) since there's exactly one and a registry_
    // entity's ModelComponent only knows how to hold a Model, not a raw
    // Mesh + Material pair.
    Mesh groundMesh_;
    Material groundMaterial_;
    // Phase 7b: the fullscreen quad the HDR-resolve pass draws (see
    // mesh.hpp's makeFullscreenQuad()) -- built once here, like groundMesh_,
    // rather than re-built every frame.
    Mesh postProcessQuad_;
    // Phase 9: the PBR sphere test-grid. One shared Mesh (every sphere is
    // geometrically identical) plus one SphereInstance (transform +
    // PBRMaterial) per sphere -- see application.cpp's kSphere* constants
    // for the grid layout (metallic x roughness) and Application's Phase 9
    // header comment above for why this is a second, PBRMaterial-based list
    // alongside registry_'s entities rather than a migration of it.
    Mesh sphereMesh_;
    std::vector<SphereInstance> sphereInstances_;
    // Phase 8a: replaces the old std::vector<Entity> entities_ -- see this
    // header's own Phase 8a comment and ecs.hpp for the full design. The
    // scene's Transform/Model data now lives in registry_'s
    // ComponentPool<Transform>/ComponentPool<ModelComponent> pools, keyed by
    // EntityId, rather than as two fixed fields per vector element.
    EntityRegistry registry_;
    Camera camera_;
    // Phase 8d: the data-driven key-binding table pollInputState() now
    // consults each frame (see input.hpp/input_action_map.hpp) -- owned
    // here (not a local temporary in run()) specifically so it persists
    // across frames, which edge-triggered actions (InputAction::
    // ToggleDebugUI) need in order to compare this frame's key state
    // against the previous frame's. No GL/window dependency at all, so
    // its position among these members is unconstrained; grouped next to
    // camera_ since both are per-frame input-consuming state.
    InputActionMap inputActionMap_;
    // Phase 8c: owns the ImGui context + GLFW/OpenGL3 backend lifecycle --
    // see debug_ui.hpp and this header's own Phase 8c comment above. Only
    // needs window_.handle() (a live GL context + GLFW window), so its
    // position here (after every GL-resource member above) is unconstrained
    // beyond "after window_", same as camera_. Phase 8d: toggled at
    // runtime via setEnabled() from InputAction::ToggleDebugUI (default
    // F1) -- see update()'s definition and debug_ui.hpp's own Phase 8d
    // comment.
    DebugUI debugUI_;
    std::uint64_t maxFrames_;
    std::uint64_t frameCount_ = 0;
    double totalTime_ = 0.0;

    // Set from the ENGINE_CAMERA_DEMO env var (see application.cpp's demo
    // path in update()'s definition) -- true drives the camera through a
    // scripted, frame-count-based orbit instead of reading Window input,
    // purely so headless verification can prove the free-fly/lookAt math
    // actually moves the camera, not just that it compiles.
    bool cameraDemoMode_ = false;

    // Phase 13b: set from ENGINE_FRUSTUM_CULL_DEMO -- true parks the camera
    // at kDefaultCameraPosition but looking away from the scene instead of
    // at it (see the constructor and update()), so a headless run can
    // demonstrate frustum culling actually dropping the draw count instead
    // of only ever exercising the "everything stays in view" default path.
    // Independent of cameraDemoMode_ above (checked first in update(), see
    // that function) -- the two are not expected to be combined, but nothing
    // stops a caller from setting both env vars, so update() picks one
    // deterministically rather than letting them race each frame.
    bool frustumCullDemoMode_ = false;

    // Phase 13d: set from ENGINE_CLUSTER_DEBUG -- true tints every fragment
    // by its own cluster's total (point + spot) light count instead of (on
    // top of, see basic.frag/pbr.frag's main()) its ordinarily-lit color,
    // so a headless run can visually confirm the per-cluster light lists
    // actually vary across the screen/depth. Same getenv-gated pattern as
    // cameraDemoMode_/frustumCullDemoMode_ above; independent of both (a
    // debug visualization, not an alternate camera path).
    bool clusterDebugMode_ = false;

    // Phase 13f: set from ENGINE_SSAO_DISABLE -- true forces
    // basic.frag/pbr.frag's uSSAOEnabled uniform to 0 every frame, making
    // both shaders fall back to an unconditional ssao = 1.0 (no occlusion)
    // instead of sampling ssaoBlurred_ -- so a headless run can compare an
    // SSAO-on and SSAO-off screenshot/pixel-sample pair from the same build
    // to isolate SSAO's own exact contribution, without needing a second
    // compile-time toggle. Default false (SSAO on), matching every other
    // env-gated flag in this class defaulting to "off/normal behavior
    // unless explicitly requested."
    bool ssaoDisabled_ = false;

    // Phase 13f: set from ENGINE_SSAO_DEBUG -- true makes the final
    // postprocess pass show ssaoRaw_ (SSAO's raw, pre-blur occlusion buffer)
    // directly instead of the ordinarily-tonemapped scene, so a headless
    // screenshot can verify the occlusion term's own noise/halo/banding
    // characteristics in isolation -- see postprocess.frag's own Phase 13f
    // comment. Same getenv-gated pattern as cameraDemoMode_/
    // clusterDebugMode_ above.
    bool ssaoDebugMode_ = false;

    // Phase 13g: set from ENGINE_SSR_DISABLE -- true skips
    // renderSSRComposite() and its follow-up resolveTo() entirely, leaving
    // hdrResolveFramebuffer_ exactly as the ordinary (IBL-only-reflections)
    // first pass alone produced it, so a headless run can compare an
    // SSR-on and SSR-off screenshot pair from the same build to isolate
    // SSR's own exact contribution -- same getenv-gated pattern as
    // ssaoDisabled_/ENGINE_SSAO_DISABLE. Default false (SSR on).
    bool ssrDisabled_ = false;
};

}  // namespace engine

#endif  // ENGINE_APPLICATION_HPP
