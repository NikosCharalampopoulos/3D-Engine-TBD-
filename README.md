# 3D Engine (TBD)

A small C++/OpenGL 3D engine: a real windowing/main-loop foundation, a
free-fly camera, GL 4.3 core shaders/meshes/textures (bumped from 3.3 in
Phase 12 -- see that section below), multi-light (directional
+ point + spot) Blinn-Phong lighting with tangent-space normal mapping and
directional shadow mapping, Assimp-based multi-object scene loading with a
real node hierarchy, MSAA anti-aliasing, a resource-cache layer, an HDR +
Reinhard-tonemapped post-process pipeline, (Phase 9) a real
metallic/roughness Cook-Torrance PBR material/shader alongside the original
Blinn-Phong path, proven out on a sphere reference grid, (Phase 10)
real-time image-based lighting -- a diffuse irradiance cubemap, a
GGX-prefiltered mipmapped specular cubemap, and a split-sum BRDF LUT, all
convolved once at startup from the skybox -- replacing that PBR path's flat
placeholder ambient term with a real, direction- and roughness-aware one --,
(Phase 11) textured PBR materials plus tonemap-aware bloom, (Phase 12) a
GL 4.3 core foundation bump laying the groundwork for compute-shader
clustered lighting, (Phase 13a) anisotropic texture filtering, (Phase 13b)
frustum culling -- skipping draw calls for geometry entirely outside the
camera's view --, (Phase 13c) Cascaded Shadow Maps, giving shadow resolution
near the camera without a bigger shadow map, (Phase 13d) GPU compute-shader
clustered forward light culling -- replacing the old brute-force
per-fragment light loop with a 3D grid of view-frustum "clusters", each with
its own GPU-built list of which lights actually reach it --, (Phase 13e) a
real HDRI (equirectangular Radiance .hdr) environment map -- generated
procedurally offline, GPU-converted into a floating-point cubemap, and fed
into both the sky background and the existing IBL pipeline in place of the
old flat procedural skybox (which stays available as a fallback/reference)
--, (Phase 13f) Screen-Space Ambient Occlusion, (Phase 13g) Screen-Space
Reflections -- refining the PBR sphere grid's IBL-only specular term with a
real ray-marched reflection of the actual nearby scene --, (Phase 8a) a
small but genuine component-based ECS (entities as opaque IDs, components in
typed per-type pools) replacing the earlier Phase 6 `Entity` struct, (Phase
8b) JSON scene serialization/level loading, (Phase 8c) a Dear ImGui
debug overlay (entity inspector, render-pass toggles, frame stats) drawn
last onto the final tonemapped image, (Phase 8d) a data-driven input
action-mapping layer underneath free-fly camera movement, and (Phase 8e)
basic physics/collision -- gravity plus ground-plane collision for
ECS entities with a new RigidBody/Collider component pair, demonstrated by a
small cube that visibly falls and comes to rest on the ground plane, and
(Phase 14a) the first sub-phase of a new "Phase 14: full editor UI" arc -- a
bigger, properly resizable window and an always-on Dear ImGui docking shell
(Scene/Assets/Viewport/Inspector placeholder panels, Unity/Blender-style)
layered over the still-unchanged 3D render, alongside (not replacing) the
existing Phase 8c F1 debug overlay -- built
up from bare-metal OpenGL, and
verified at every step by a headless Xvfb+Mesa run/screenshot harness (no
GPU or display required).
See "Architecture
overview" right below for what the finished whole looks like today, or
"Development history" further down for how it got built, phase by phase
(this repo was built incrementally across many phases, each independently
bug-reviewed), including the specific bugs each phase's review found and
fixed.

## Architecture overview

`engine_app` is a single executable (no separate engine library target
yet -- see the note at the top of `CMakeLists.txt`) built around a handful
of small, mostly-RAII classes in `include/engine/` + `src/`:

- **`Window`** (`window.hpp`/`.cpp`) -- owns the GLFW window and a GL 4.3
  core context (bumped from 3.3 in Phase 12 -- see that section below),
  requested with 4x MSAA (`GLFW_SAMPLES`, plus a Linux-only
  EGL context-creation path needed to actually get multisampling on this
  project's headless Xvfb+Mesa target -- see `ENGINE_DISABLE_EGL_CONTEXT`
  below if that path ever needs disabling on a real desktop). Reports raw
  keyboard/cursor state; holds no engine/scene state of its own.
- **`InputState`** (`input.hpp`/`.cpp`) -- a per-frame snapshot polled once
  from `Window` (`pollInputState()`) and passed down to whatever needs it.
  Nothing else in the engine (notably `Camera`, and `Application`'s own
  ESC-to-quit check) reads `Window` key/cursor state directly any more.
- **`Application`** (`application.hpp`/`.cpp`) -- owns the `Window`, a
  `ResourceManager`, the scene's shared `Shader` (plus a depth-only
  `shadowShader_`, a `skyboxShader_`, a `postProcessShader_`, and every other
  per-phase program below), `kCascadeCount` (3) `ShadowMap` cascades, a
  multisampled off-screen HDR `Framebuffer` plus several single-sample ones
  (resolve/bloom/SSAO targets, see below), a `Skybox`, a `ClusterLightCuller`,
  an `Ssao` pass, a hand-built normal-mapped ground plane
  (`groundMesh_`/`groundMaterial_`), the PBR sphere test-grid
  (`sphereMesh_`/`sphereInstances_`), a fullscreen `postProcessQuad_`, an
  `EntityRegistry` (`registry_`), and a `Camera`; runs the main loop (poll input ->
  update camera -> render -> swap) until the window closes, ESC is pressed,
  or `ENGINE_MAX_FRAMES` is reached (headless verification only -- see
  below). `render()` has grown a step for essentially every phase below --
  see "One frame, in short" right after this list for its current, full
  per-frame shape, and each phase's own section further down for why each
  step exists.
- **`Camera`** + **`Transform`** (`camera.hpp`/`.cpp`, `transform.hpp`) -- a
  yaw/pitch free-fly camera driven by `InputState` (or a small scripted
  waypoint path under `ENGINE_CAMERA_DEMO`, for headless verification where
  there's no real input device), and a position/quaternion-rotation/scale
  bundle used for every object's model matrix.
- **`Shader`** / **`Mesh`** / **`Texture`** / **`Material`** -- the
  rendering primitives: a linked GL program; an interleaved
  position/normal/texCoord/tangent VAO+VBO+EBO (plus its own local-space
  `BoundingSphere`, computed once at construction -- see "Phase 13b" below);
  a 2D GL texture loaded via stb_image, with anisotropic filtering applied
  uniformly when supported (see "Phase 13a" below); and a `Shader` + diffuse
  `Texture` + optional normal-map `Texture` + tint/shininess bundle bound
  once per draw call. `assets/shaders/basic.vert`/`basic.frag` implement
  Blinn-Phong lighting for one directional light + a fixed-size array of
  point/spot lights (see "Phase 7a" below, and "Phase 13d" for how each
  fragment now only loops over its own cluster's culled subset of them),
  tangent-space normal mapping, Cascaded Shadow Map sampling (see "Phase 13c"
  below), and a screen-space ambient occlusion factor (see "Phase 13f"
  below), all with a properly computed (transpose-inverse) normal matrix.
- **`PBRMaterial`** (`pbr_material.hpp`, header-only) -- a second,
  independent material type (see "Phase 9" below) alongside `Material`
  above: an albedo tint, metallic, roughness, and ambient-occlusion scalar
  (plus, since Phase 11, an optional packed albedo/ORM texture pair -- see
  that section below), bound to `assets/shaders/pbr.vert`/`pbr.frag`'s
  metallic/roughness Cook-Torrance BRDF instead of `basic.frag`'s
  Blinn-Phong. `Material`/`basic.vert`/`basic.frag` are unchanged and still
  drive the rest of the scene (the table/box/pyramid/ground) -- Phase 9 adds
  a second lighting model rather than replacing the first.
- **`ShadowMap`** (`shadow_map.hpp`/`.cpp`) -- an RAII depth-only FBO +
  texture used to render the scene from the directional light's point of
  view. Since Phase 13c (see below), `Application` owns `kCascadeCount` (3)
  independent instances of this class -- Cascaded Shadow Maps -- rather than
  one covering the whole scene, each rendered from the same directional
  light but fitted to a different depth slice of the camera's own view
  frustum; the main pass picks which cascade a given fragment falls into and
  samples that cascade's own depth texture to shadow the light's
  contribution.
- **`Framebuffer`** (`framebuffer.hpp`/`.cpp`) -- an RAII "render target":
  an FBO with a floating-point (`GL_RGBA16F`) color attachment (optionally
  multisampled, a real mip chain, or a real *sampled* depth texture instead
  of a write-only depth renderbuffer -- see this class's own Phase 13c/13g
  and Phase 13f comments) plus a depth buffer. `Application` renders the
  whole lit scene (+ skybox) into a multisampled one of these
  (`hdrFramebuffer_`) instead of straight to the window, so a light's real
  intensity can exceed 1.0 without hard-clipping (see "Phase 7b" below), then
  resolves it into a single-sample `hdrResolveFramebuffer_` every frame.
  Several more instances of this same class hold Phase 11's bloom
  bright-pass/ping-pong-blur targets and Phase 13f's SSAO geometry/occlusion
  targets -- a bloom pass and much more besides turned out to reuse exactly
  this one small abstraction, not just the "future pass" this bullet used to
  gesture at speculatively.
- **`Skybox`** (`skybox.hpp`/`.cpp`) -- a `GL_TEXTURE_CUBE_MAP` rendered as
  the scene's background via its own program
  (`assets/shaders/skybox.vert`/`.frag`), drawn last each frame with a
  `GL_LEQUAL` depth trick so it only shows through pixels nothing else drew.
  By default (Phase 13e) built from a real HDRI (`assets/textures/hdri/`),
  GPU-converted from equirectangular to cubemap via
  `engine::loadHdrEquirectangularAsCubemap()` (`hdri_loader.hpp`/`.cpp`);
  `ENGINE_USE_PROCEDURAL_SKYBOX=1` switches back to the original Phase 7b
  6-face procedural-sky cubemap (`assets/textures/skybox/`), kept as a
  fallback/reference.
- **`IBLProbe`** (`ibl_probe.hpp`/`.cpp`, see "Phase 10" below) -- real-time
  image-based lighting built from `Skybox`'s own cubemap: a diffuse
  irradiance cubemap, a mipmapped GGX-prefiltered specular cubemap, and a 2D
  BRDF integration LUT, all convolved once at startup (a handful of ordinary
  draw calls into small offscreen FBOs, not a persistent render target) via
  three dedicated one-time shader passes. `pbr.frag` samples all three every
  frame to drive its ambient term -- replacing Phase 9's flat placeholder
  ambient with real, direction- and roughness-aware image-based lighting.
  Since Phase 13g (see below), `pbr.frag`'s `traceSSR()` refines that same
  specular term further for the PBR sphere grid: a real screen-space
  ray-marched reflection of the actual nearby scene where one is found,
  fading back to this IBL-only term at screen edges, grazing angles, and
  high roughness.
- **`Model`** (`model.hpp`/`.cpp`) -- loads a whole scene via Assimp
  (`assets/models/scene.obj`: a table, a box on the table, and a separate
  pyramid) into a tree of `ModelNode`s, each with its own local transform,
  mesh indices, and children; `draw()` walks the tree depth-first,
  composing world transforms and binding each node's `Material`.
- **`EntityRegistry`** / **`EntityId`** / **`ComponentPool<T>`** (`ecs.hpp`,
  see "Phase 8a" below) -- a small component-based ECS replacing Phase 6's
  `Entity` struct: entities are opaque `EntityId` indices with no data of
  their own; a `Transform` component, a `ModelComponent` (wrapping the
  existing `shared_ptr<Model>` plus, since Phase 8b, the asset path it was
  loaded from), a `NameComponent` (Phase 8b, a human-readable name), and
  (Phase 8e) a `RigidBody`/`Collider` pair (`physics.hpp` -- velocity/gravity
  and a box collider, see "Phase 8e" below) each live in their own
  `ComponentPool<T>`, keyed by entity id. `Application::registry_` currently
  holds two entities (the static `scene.obj` model and Phase 8e's falling
  cube); `render()`/`renderShadowPass()`/`renderSSAO()` visit them via
  `registry_.each<ModelComponent>(...)`, looking up each entity's `Transform`
  alongside its `ModelComponent`, rather than iterating a hardcoded
  `std::vector<Entity>`.
- **`loadScene()`/`saveScene()`** (`scene_serialization.hpp`, see "Phase 8b"
  below) -- (de)serializes `registry_`'s entity data to/from a JSON file
  (`assets/scenes/default.json` by default), so `Application`'s constructor
  builds its scene from data instead of a hardcoded call sequence.
- **`stepPhysics()`** (`physics.hpp`/`.cpp`, see "Phase 8e" below) -- gravity
  integration plus ground-plane collision for every `RigidBody` entity,
  called once per frame from `update()`.
- **`ResourceManager`** (`resource_manager.hpp`/`.cpp`) -- a per-key
  `shared_ptr` cache for `Shader`/`Texture`/`Model`. Every asset load in the
  engine -- the scene's shader, the scene's model, every material's diffuse
  texture including the shared checker-texture fallback -- goes through it,
  so nothing is loaded from disk or re-uploaded to the GPU more than once.
- **`ComputeShader`** (`compute_shader.hpp`/`.cpp`, see "Phase 13d" below) --
  a small RAII sibling to `Shader` for a compute-only GL program (one
  `GL_COMPUTE_SHADER` stage, no vertex/fragment pipeline at all).
- **`ClusterLightCuller`** (`cluster_light_culler.hpp`/`.cpp`, see
  "Phase 13d" below) -- GPU compute-shader clustered forward light culling:
  divides the camera's view frustum into a 3D grid of "clusters", builds
  each cluster's view-space AABB once at startup, and re-culls every
  point/spot light against those AABBs every frame, so `basic.frag`/
  `pbr.frag` can loop over just the handful of lights that actually reach
  a given fragment's cluster instead of every light in the scene.
- **`Frustum`** (`frustum.hpp`, header-only, see "Phase 13b" below) -- the
  camera's 6 view-frustum planes, extracted once per frame from the combined
  view-projection matrix (the standard Gribb/Hartmann method), tested
  against each drawable's own world-space `BoundingSphere` (see `Mesh`
  above) so `render()` can skip the draw call entirely for anything
  provably outside the camera's view.
- **`Ssao`** (`ssao.hpp`/`.cpp`, see "Phase 13f" below) -- Screen-Space
  Ambient Occlusion: a small lightweight geometry pre-pass (view-space
  normal + a real, sampled depth texture) feeds a 32-sample hemisphere-kernel
  occlusion pass and a small box blur, producing a per-pixel occlusion
  factor `basic.frag`/`pbr.frag` multiply into their ambient term alongside
  their existing material-authored AO. Its geometry pre-pass's depth texture
  is also what Phase 13g's SSR ray march tests against, rather than building
  a second, redundant one.

One frame, in short (every step below runs unconditionally by default; the
env vars named along the way turn individual ones off for isolated
before/after comparison -- see each phase's own section for what each one
does): `Application::run()` polls GLFW events and an `InputState` (by default
a real per-frame poll through `inputActionMap_`'s data-driven bindings, see
"Phase 8d"), then `update()` toggles the debug overlay if `InputAction::
ToggleDebugUI`'s bound key (default F1) was just pressed ("Phase 8c"/"Phase
8d"), steps `stepPhysics()` for every `registry_` entity with a `RigidBody`
("Phase 8e"), and feeds `camera_` either that same `InputState` or a scripted
demo path, before `render()`:

1. Builds this frame's `kCascadeCount` (3) cascades from the camera's
   current pose (`computeCascades()` -- see "Phase 13c"), then renders the
   whole scene depth-only into each cascade's own `ShadowMap` from the
   directional light's point of view (`renderShadowPass()`).
2. Runs SSAO's three screen-space passes (geometry pre-pass, hemisphere-
   kernel occlusion, box blur -- `renderSSAO()`, "Phase 13f"), producing a
   blurred occlusion texture the color pass below samples.
3. Binds `hdrFramebuffer_` (the multisampled off-screen HDR target) and
   clears it, re-culls every point/spot light against
   `clusterLightCuller_`'s per-cluster AABBs for this frame's camera view (a
   compute-shader dispatch + memory barrier -- "Phase 13d"), and builds this
   frame's view-frustum `Frustum` ("Phase 13b").
4. Uploads view/projection/cascade/lighting/cluster/SSAO uniforms once, then
   iterates `registry_`'s `ModelComponent` pool (`registry_.each<ModelComponent>`,
   "Phase 8a"), looking up each entity's `Transform` component and calling
   `model->draw(shader, transform.getModelMatrix(), frustum, cullStats)`
   (which recurses the model's node tree, testing each mesh's world-space
   bounding sphere against `frustum` and skipping the draw call if it's
   provably out of view) plus the hand-built ground plane and every PBR
   sphere instance (`pbrShader_`, "Phase 9"), each surviving fragment
   sampling its own cascade's shadow map, any bound normal/ORM map ("Phase
   11"), its own cluster's culled light list, the blurred SSAO texture, and
   -- for the PBR spheres, when enabled -- `IBLProbe`'s convolved maps.
5. Draws `skybox_` (by default the Phase 13e HDRI-derived cubemap; the
   Phase 7b procedural one under `ENGINE_USE_PROCEDURAL_SKYBOX=1`) last into
   that same HDR framebuffer as the background, then resolves it into
   `hdrResolveFramebuffer_`.
6. Unless `ENGINE_SSR_DISABLE` is set, redraws just the PBR sphere grid a
   second time with SSR enabled (`renderSSRComposite()`, "Phase 13g"),
   ray-marching against SSAO's own depth buffer and blending in a real
   reflection of `hdrResolveFramebuffer_`'s already-shaded scene where one is
   found, then re-resolves `hdrFramebuffer_` into `hdrResolveFramebuffer_` a
   second time so the steps below see this pass's own result.
7. Runs Phase 11's bloom pipeline against `hdrResolveFramebuffer_`: a
   bright-pass extraction, then a ping-ponged separable blur.
8. Resolves the final image with one fullscreen tonemap/gamma-correct/
   bloom-composite pass (`postProcessShader_` + `postProcessQuad_`) into
   `viewportColorFramebuffer_` -- a dedicated off-screen target sized to the
   editor's Viewport panel (Phase 14c), not the default framebuffer.
9. Rebinds the default framebuffer at the window's own real size and clears
   it, then draws `EditorUI`'s always-on dockspace shell
   (Scene/Assets/Inspector placeholder panels, and -- since Phase 14c -- a
   real `ImGui::Image()` of `viewportColorFramebuffer_`'s color texture
   filling the Viewport panel) on top, followed by (unless
   `ENGINE_SHOW_DEBUG_UI`/F1 have it off) the Phase 8c Dear ImGui debug
   overlay (`renderDebugUI()`) as a normal free-floating window in that same
   ImGui frame -- see "Phase 14a" below for why these two share one ImGui
   context/frame instead of each owning their own, and "Phase 14c" for how
   the Viewport panel's own on-screen size now feeds back into steps 1-8
   above (one frame later) rather than the window's real size doing so.

## Directory layout

```
CMakeLists.txt        Root build: fetches deps (incl. Assimp), builds engine_app
src/                   Engine .cpp sources (main.cpp, window.cpp, application.cpp,
                       shader.cpp, mesh.cpp, camera.cpp, texture.cpp, model.cpp,
                       input.cpp, resource_manager.cpp, paths.cpp, shadow_map.cpp,
                       framebuffer.cpp, skybox.cpp, ibl_probe.cpp,
                       hdri_loader.cpp, compute_shader.cpp,
                       cluster_light_culler.cpp, ssao.cpp,
                       scene_serialization.cpp, scene_loader.cpp, debug_ui.cpp,
                       input_action_map.cpp, physics.cpp, editor_ui.cpp,
                       light.cpp -- Phase 15a, extended Phase 15b with
                       DirectionalLight/resolveActiveDirectionalLight(),
                       scene_hierarchy.cpp -- Phase 14d, asset_browser.cpp --
                       Phase 15d's buildAssetTree())
include/engine/        Public engine .h/.hpp headers (window, application, log,
                       gl_debug, version, shader, mesh, camera, transform,
                       texture, material, pbr_material, model, ecs, input,
                       resource_manager, paths, shadow_map, framebuffer, skybox,
                       ibl_probe, hdri_loader, compute_shader,
                       cluster_light_culler, frustum, ssao,
                       scene_serialization, debug_ui, input_action_map, physics,
                       editor_ui, light -- Phase 15a's new PointLight ECS
                       component, extended Phase 15b with DirectionalLight,
                       camera_component -- Phase 15c's new CameraComponent,
                       deliberately its own header rather than folded into
                       light.hpp, asset_browser -- Phase 15d's new
                       AssetTreeNode/buildAssetTree(), same "own header,
                       genuinely different kind of thing" precedent)
external/              Vendored small/single-header libs (stb_image, glad)
assets/                Shaders (incl. shadow.vert/.frag, skybox.vert/.frag,
                       postprocess.vert/.frag, pbr.vert/.frag,
                       bloom_extract.frag, blur.frag, cubemap_capture.vert,
                       irradiance_convolution.frag, prefilter.frag,
                       brdf_lut.frag, equirect_to_cubemap.frag,
                       cluster_aabb.comp, cluster_cull.comp, gbuffer.vert/.frag,
                       ssao.frag, ssao_blur.frag), textures
                       (checker.png, normal_bump.png, skybox/ -- 6 cubemap
                       faces (Phase 7b/10 procedural fallback), hdri/sky.hdr --
                       Phase 13e's real HDRI, rusted_metal_albedo/orm.png +
                       scuffed_plastic_albedo/orm.png -- Phase 11's textured
                       PBR materials), models (scene.obj + scene.mtl,
                       falling_cube.obj + falling_cube.mtl -- Phase 8e's demo
                       object), scenes/default.json -- Phase 8b's serialized
                       scene (Phase 8e adds its "falling_cube" entity)
tools/                 Build/run/screenshot scripts, generate_hdri.py (Phase 13e)
tests/                 scene_serialization_test.cpp (Phase 8b),
                       input_action_map_test.cpp (Phase 8d),
                       physics_test.cpp (Phase 8e), ecs_test.cpp (Phase 14f),
                       transform_hierarchy_test.cpp (Phase 14b),
                       scene_hierarchy_test.cpp (Phase 14d),
                       light_test.cpp (Phase 15a, extended Phase 15b with
                       resolveActiveDirectionalLight() coverage),
                       camera_component_test.cpp (Phase 15c),
                       asset_browser_test.cpp (Phase 15d) + its own
                       CMakeLists.txt (no longer just the Phase 0 placeholder)
```

A single executable target (`engine_app`) is built for now. A future phase
will likely split this into an `engine` library (most of src/) plus a thin
app target, so any additional front-ends can link the engine without
recompiling it -- see the comment at the top of `CMakeLists.txt`. Phase 8b's
own test (see `tests/CMakeLists.txt`) sidesteps needing that split today by
linking against just one GL-independent translation unit
(`scene_serialization.cpp`) directly, rather than the whole engine.

## Prerequisites (Linux)

GLFW's X11 backend needs its usual system development headers (this is a
GLFW/Linux requirement, not something CMake FetchContent can substitute
for). On Debian/Ubuntu:

```sh
sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
                  libxi-dev libxext-dev libgl1-mesa-dev libglx-dev
```

(Already present in this container.) A C/C++ toolchain (g++ >= 9, C++17)
and CMake >= 3.16 are also required.

## Building

```sh
cmake -B build -S .
cmake --build build -j
```

This requires network access the first time (CMake FetchContent does a
shallow `git clone` of GLFW and GLM straight from GitHub over HTTPS, not
via an archive/tarball download). Subsequent builds reuse the clones under
`build/_deps/` and do not need network access. No other manual steps are
required beyond the system packages above.

The resulting executable is `build/engine_app`. It can be launched from any
directory (double-clicking it, running it from an unrelated shell cwd,
etc.) -- the build copies `assets/` next to the executable, and asset
paths are resolved against the executable's own location at runtime
(`src/paths.cpp`), not the process's current working directory.

## Development history

The sections below are kept as design notes from how each phase actually
got built, in order, including the specific bugs each phase's independent
bug-review found and fixed -- not a description of what's missing today.
See "Architecture overview" above for the current, finished-whole picture.

### Phase 1: window + main loop

- **`engine::Window`** (`include/engine/window.hpp`, `src/window.cpp`) --
  RAII wrapper around GLFW window/context creation. The constructor does
  `glfwInit`, window + GL 3.3 core context creation, and GLAD function
  loading, throwing `std::runtime_error` (and cleaning up anything already
  created) on the first failure; the destructor destroys the window and
  calls `glfwTerminate()` exactly once, so every exit path (normal, early
  return, exception) leaves no leaked window/context. Exposes
  `shouldClose()`, `pollEvents()`, `swapBuffers()`, `getSize()`,
  `isKeyPressed()`.
- **`engine::Application`** (`include/engine/application.hpp`,
  `src/application.cpp`) -- owns a `Window` and runs the main loop (poll ->
  update -> render -> swap) until the window closes or ESC is pressed.
  Computes delta-time (`glfwGetTime()`) and a frame counter every iteration;
  nothing consumes them yet, but the pattern is in place for later phases.
  Each frame clears to cornflower blue (`(100, 149, 237)` 8-bit sRGB, same
  as Phase 0) so a screenshot can prove the loop is actually rendering
  repeatedly, not just once.
- **Logging** (`include/engine/log.hpp`) -- `LOG_INFO`/`LOG_WARN`/
  `LOG_ERROR` macros, timestamped, info to stdout and warn/error to stderr.
  Just enough to see lifecycle events (window created, GL context info,
  shutdown) and GL errors, not a general logging library.
- **GL error checking** (`include/engine/gl_debug.hpp`) -- `GL_CHECK(expr)`
  runs `expr` and, in debug builds only (`NDEBUG` undefined), drains
  `glGetError()` and logs every error found via `LOG_ERROR` tagged with the
  call site. Expands to plain `expr` in `NDEBUG`/Release builds. Wired into
  `Window`'s GL setup (`glViewport`) and `Application::render()`'s
  `glClearColor`/`glClear`.
- **Headless termination**: `main.cpp` reads an `ENGINE_MAX_FRAMES`
  environment variable; if set to a positive integer, `Application::run()`
  returns on its own after that many frames instead of waiting for ESC/window
  close. This is used *only* for headless verification (Xvfb has no real
  window manager or keyboard), e.g.:
  ```sh
  ENGINE_MAX_FRAMES=120 bash tools/run_headless.sh build/engine_app build/phase1_screenshot.png
  ```
  Left unset, normal interactive runs behave as expected (run until closed).

### Running headlessly (no GPU / no display required)

This container has no physical GPU, but Mesa's software rasterizer
(`swrast_dri.so`, providing the `llvmpipe` driver) supports real OpenGL 3.3
core rendering, driven through Xvfb (a virtual/off-screen X server). This is
the pattern later phases should reuse for any headless verification:

```sh
# One-shot helper: starts Xvfb, runs engine_app against it, grabs a
# screenshot, tears Xvfb down, and enforces a hard timeout so a hang can
# never block CI/verification.
tools/run_headless.sh [path-to-engine_app] [screenshot-output.png]

# Defaults:
#   path-to-engine_app    build/engine_app
#   screenshot-output.png build/phase0_screenshot.png
```

Equivalent manual steps, if you want to reproduce it by hand (Phase 14a
note: `engine_app`'s own default window size is 1600x900 as of that phase --
`ENGINE_WINDOW_WIDTH=800 ENGINE_WINDOW_HEIGHT=600` below, matching
`tools/run_headless.sh`'s own Phase 14a change, keeps this manual recipe's
window matched to the 800x600 Xvfb screen it's launched against; see that
phase's own section for why headless verification stays pinned at 800x600
instead of following the interactive default up to 1600x900):

```sh
Xvfb :99 -screen 0 800x600x24 &
export DISPLAY=:99
export LIBGL_ALWAYS_SOFTWARE=1   # force Mesa's llvmpipe software GL driver
export ENGINE_WINDOW_WIDTH=800 ENGINE_WINDOW_HEIGHT=600   # Phase 14a
./build/engine_app &
xwd -root -display :99 -out shot.xwd && convert shot.xwd shot.png
wait
```

`engine_app` itself only runs a handful of frames (with a short sleep
between them) and then exits on its own -- it is intentionally bounded so
headless verification never hangs waiting for a window to close.

### Confirming the screenshot is a real render, not a black window

Check that the captured PNG's average pixel color is close to the clear
color set in `src/main.cpp` (cornflower blue, `(100, 149, 237)` in 8-bit
sRGB), rather than solid black (which would indicate a broken/unbound GL
context or a screenshot taken before the window rendered anything):

```sh
convert shot.png -format "mean=%[fx:int(mean.r*255)],%[fx:int(mean.g*255)],%[fx:int(mean.b*255)]\n" info:
```

A broken/premature capture shows as `mean=0,0,0`; a correct one matches
`mean=100,149,237` (confirmed exactly on this container's Mesa llvmpipe
software renderer). If Pillow is installed and working, the equivalent
check is:

```sh
python3 -c "
from PIL import Image
im = Image.open('shot.png').convert('RGB')
print(im.resize((1, 1)).getpixel((0, 0)))
"
```

Note: in this container's Python environment, the apt-installed
`python3-pil` package is missing its compiled `_imaging` extension module
(`ImportError: cannot import name '_imaging'`) even though `pillow` shows as
installed via `pip3 list` -- a dist-packages/pip conflict. `convert` above
is the verified-working method here; fix or reinstall Pillow
(`pip3 install --force-reinstall pillow`) if you need the Python path.

### Phase 2: shaders + a rendered cube

- **`engine::Shader`** (`include/engine/shader.hpp`, `src/shader.cpp`) --
  RAII wrapper around a linked GL program (one vertex + one fragment
  stage). Loads source from files (`assets/shaders/basic.vert` /
  `basic.frag`), compiles each stage and links the program, logging the
  real GL info log via `LOG_ERROR` (not failing silently) if either step
  fails, then throwing `std::runtime_error`. `use()`/`bind()` binds the
  program; `setMat4`/`setVec3`/`setFloat`/`setInt` look up each uniform's
  location by name on every call (a caching layer is a later optimization).
  Move-only, like `Window`: copying would let two destructors
  `glDeleteProgram` the same handle.
- **`engine::Mesh`** (`include/engine/mesh.hpp`, `src/mesh.cpp`) -- RAII
  wrapper around a VAO + VBO + optional EBO. `Vertex` is an interleaved
  `{position, normal, texCoord}` struct so later phases can add attributes
  into the same buffer layout; this phase only enables `position` as an
  active vertex attribute (attribute 0) -- `normal`/`texCoord` are real
  uploaded data, just not yet wired to `glVertexAttribPointer`. `draw()`
  draws the whole mesh; `drawRange(indexOffset, count)` draws a sub-range of
  indices (used to color the cube's 6 faces individually). `makeCube()`
  builds a 24-vertex / 36-index unit cube (4 vertices per face, since
  normals differ by face). Move-only, same rationale as `Shader`.
- **Rendering**: `Application::render()` now draws `makeCube()`'s geometry
  through `basic.vert`/`basic.frag` (`assets/shaders/`) instead of just
  clearing the framebuffer. There's no `Camera` yet (Phase 3), so
  view/projection are a hardcoded `glm::lookAt` + `glm::perspective` built
  fresh each frame -- a fixed (non-animated) two-axis model rotation
  guarantees several faces are visible in every frame, including whatever
  moment a headless screenshot lands on. Each of the 6 faces is drawn with
  its own uniform color (`drawRange` + `setVec3("uColor", ...)`) so a
  screenshot shows visibly distinct, flat-shaded faces rather than a solid
  color. `GL_DEPTH_TEST` is enabled once in `Application`'s constructor and
  the depth buffer is cleared every frame alongside color, so faces occlude
  each other correctly.
- **Verify**: same headless harness as Phase 1, then check the screenshot
  has more than the 2 colors (background + faces) a flat clear would give:
  ```sh
  ENGINE_MAX_FRAMES=30 bash tools/run_headless.sh build/engine_app build/phase2_screenshot.png
  convert build/phase2_screenshot.png -format %k info:      # unique color count
  convert build/phase2_screenshot.png -colors 64 -format %c histogram:info:-
  ```
  On this container's Mesa llvmpipe software renderer this currently reports
  5 unique colors: the cornflower-blue background plus red/yellow/magenta
  (and a tiny cyan sliver) cube faces -- confirming real, distinguishable 3D
  geometry rendered, not a flat clear.

### Phase 3: Camera + Transform

- **`engine::Transform`** (`include/engine/transform.hpp`, header-only) --
  a plain position/rotation/scale bundle plus `getModelMatrix()`, built as
  `translate * rotate * scale` (scale applied first, then rotate, then
  translate, reading right-to-left against a vertex). Rotation is stored as
  a `glm::quat` rather than Euler angles specifically to avoid gimbal lock
  when composing rotations; `rotate(angleDeg, axis)` and `translate(delta)`
  are the simple mutators. Deliberately not a scene graph node -- no parent/
  child hierarchy (that's Phase 5's job).
- **`engine::Camera`** (`include/engine/camera.hpp`, `src/camera.cpp`) -- a
  standard yaw/pitch free-fly camera. `getViewMatrix()` builds `glm::lookAt`
  from `position_`/`front_`/`up_`; `getProjectionMatrix(aspectRatio)` builds
  `glm::perspective` (60 degree FOV, near 0.1, far 100 by default).
  `front_`/`right_`/`up_` are re-derived from yaw/pitch every time they
  change; pitch is clamped to +/-89 degrees so `front_` never becomes
  parallel to world-up, which is what would otherwise degenerate the
  `right_ = front_ x worldUp_` cross product and flip the view. Movement
  (`processKeyboard`, WASD + Space/Shift or E/Q for up/down) is scaled by
  delta-time so it's frame-rate independent; mouse-look
  (`processMouseInput(xpos, ypos)`) takes the absolute cursor position each
  frame and derives its own delta internally (discarding the very first
  reading so there's no first-frame jump).
- **Mouse input plumbing**: `Window::getCursorPos()` was added (a thin
  `glfwGetCursorPos` wrapper, mirroring the existing `isKeyPressed()`
  pattern of reporting raw current state) so `Camera` has something to read
  its mouse-look delta from.
- **Wired into `Application`**: `render()`'s previously-hardcoded eye
  position, `glm::lookAt`, `glm::perspective`, and two-`glm::rotate()`-call
  model matrix are gone; `Application` now owns a `Camera` and a
  `Transform` (the cube's fixed 35/45-degree two-axis rotation, unchanged
  from Phase 2 but expressed as one composed quaternion) and builds
  `mvp = camera.getProjectionMatrix(aspect) * camera.getViewMatrix() *
  cubeTransform.getModelMatrix()` fresh every frame. `update()` feeds
  delta-time and input into the camera every frame.
- **Headless-safe verification**: Xvfb has no real keyboard/mouse, so
  `ENGINE_CAMERA_DEMO=1` (an env var, checked only in `Application`'s
  constructor/`update()`) switches the camera onto a small, deterministic,
  frame-count-keyed set of waypoints -- all looking at the cube from
  different sides/heights, including a near-overhead angle that exercises
  the pitch-clamp path -- instead of reading `Window` input. It's keyed off
  `frameCount_` (an exact integer) rather than wall-clock time so the
  waypoint shown at any given frame never depends on this machine's
  render timing. Without the env var, the camera still starts at a fixed
  position well off to the side and above the cube (`(2.6, 1.9, 3.4)`,
  vs. Phase 2's dead-ahead `(0, 0, 3)`), so even a plain run visibly proves
  the view comes from live `Camera` state.
- **Verify**: same headless harness, e.g.
  ```sh
  ENGINE_CAMERA_DEMO=1 ENGINE_MAX_FRAMES=90 \
      bash tools/run_headless.sh build/engine_app build/phase3_screenshot.png
  ```
  Both the demo path and the plain default-camera path produce a cube
  clearly framed differently from Phase 2's screenshot: Phase 2 (eye at
  `(0,0,3)`, dead ahead) shows red/yellow/magenta faces plus a tiny cyan
  sliver; a Phase 3 run from the default elevated/offset camera instead
  shows a dominant magenta (top) face with red (front) and a thin blue
  (right) sliver -- the yellow (left) face has rotated out of view and the
  blue (right) face, invisible in Phase 2, is now visible -- proving the
  view matrix is coming from a real, different `Camera`, not a vestigial
  copy of Phase 2's hardcoded one.

### Phase 4: Texture + Material + Phong lighting

- **`engine::Texture`** (`include/engine/texture.hpp`, `src/texture.cpp`) --
  RAII wrapper around a GL 2D texture object. Loads an image file via
  `stb_image` (`src/texture.cpp` is the one translation unit in the project
  that `#define`s `STB_IMAGE_IMPLEMENTATION`), throwing `std::runtime_error`
  (after logging the real `stbi_failure_reason()`) if the file can't be
  found/decoded, rather than uploading garbage. Upload format is chosen from
  stb_image's own reported channel count (`GL_RED`/`GL_RG`/`GL_RGB`/
  `GL_RGBA` for 1/2/3/4 channels) instead of hardcoding `GL_RGB` -- a
  hardcoded format would read an RGBA source one byte short per pixel,
  shearing every row's colors. Filtering is `GL_LINEAR`
  (magnification)/`GL_LINEAR_MIPMAP_LINEAR` (minification, with
  `glGenerateMipmap`) and wrap is `GL_REPEAT`. `bind(unit)` activates
  `GL_TEXTURE0 + unit` and binds the texture there; move-only, same
  rationale as `Shader`/`Mesh` (a GL texture name is a scarce handle owned
  by exactly one `Texture`).
- **A real texture asset**: `assets/textures/checker.png`, a 256x256 8-bit
  RGB (`PNG24`, not palette-indexed, so stb_image reports a clean 3-channel
  load) orange/blue checkerboard, generated with ImageMagick `convert`
  (Pillow is broken in this container, see the Phase 1 note above) and
  committed as a real binary asset -- not generated at build/run time.
- **`engine::Material`** (`include/engine/material.hpp`, header-only) --
  bundles a `Shader&` (non-owning; shaders are typically shared across many
  materials/objects and outlive any one `Material`), an owned `Texture`
  (diffuse map), and two simple Phong-lite properties: `tint` (`vec3`) and
  `shininess` (`float`). `bind()` activates the shader, binds the texture,
  and uploads the sampler/tint/shininess uniforms; model/view/projection
  and lighting uniforms are scene/per-draw state the caller sets separately
  right after. Move-only (holds a `Texture`). Deliberately minimal -- no
  multi-texture-slot system, no serialization -- but shaped so Phase 5
  (model loading) can plausibly attach one `Material` per `Mesh` later.
- **Phong lighting** (`assets/shaders/basic.vert`/`basic.frag`, updated in
  place since nothing else referenced the old flat-color version): standard
  ambient + diffuse (N.L) + specular, using the Blinn-Phong halfway vector
  (`normalize(lightDir + viewDir)`) rather than classic `reflect()` -- a
  deliberate choice (more numerically robust, the modern convention), not
  an oversight. One directional light (`uLightDirection`/`uLightColor`/
  `uAmbientColor`), the minimum bar for this phase. The vertex shader
  uploads a proper normal matrix, `transpose(inverse(mat3(uModel)))`,
  computed on the CPU each frame in `Application::render()` -- not
  `mat3(uModel)` directly, which is a classic subtly-wrong shortcut that
  only happens to work under uniform scale and silently skews normals
  otherwise (this engine's `Transform` allows non-uniform scale).
- **`engine::Mesh`** now wires up attributes 1 (`normal`) and 2
  (`texCoord`), left unwired since Phase 2 because nothing consumed them
  until lighting/texturing existed; the interleaved vertex data itself
  (and `makeCube()`'s per-face outward normals/UVs) was already correct,
  just not exposed via `glVertexAttribPointer`/`glEnableVertexAttribArray`.
- **Wired into `Application`**: `render()` now uploads separate
  `uModel`/`uView`/`uProjection`/`uNormalMatrix` matrices (instead of one
  combined `uMVP`, since the fragment shader needs a world-space fragment
  position/normal) plus the light uniforms, binds the cube's `Material`,
  and issues one whole-mesh `draw()` -- replacing the six
  `drawRange()` + flat `uColor` draw calls from Phase 2-3. Camera/Transform
  usage is otherwise unchanged from Phase 3.
- **Verify**: same headless harness, e.g.
  ```sh
  ENGINE_MAX_FRAMES=90 bash tools/run_headless.sh build/engine_app build/phase4_screenshot.png
  convert build/phase4_screenshot.png -format %k info:   # unique color count
  ```
  On this container's Mesa llvmpipe software renderer this currently
  reports 987 unique colors (vs. Phase 2-3's 5), confirming real lighting
  gradients plus the checker texture pattern -- not a flat per-face color.
  Spot-checking pixels within a single face (e.g. two points on the visible
  shadowed side face) shows distinct colors (checker squares at different
  brightness/hue, e.g. `(57,34,12)` vs. `(11,28,60)`), confirming
  within-face variation; comparing the same checker-square color across the
  lit top face vs. the shadowed side face (e.g. `(226,133,40)` vs.
  `(57,34,12)`) confirms the directional-light shading gradient.

### Phase 5: Assimp model loading + a node hierarchy

- **Assimp integration** (`CMakeLists.txt`) -- fetched via `FetchContent`
  pinned to release tag `v5.4.3`. Every relevant Assimp cache variable is
  force-set *before* `FetchContent_Declare`/`MakeAvailable` runs, so it wins
  over the `OPTION()` calls in Assimp's own `CMakeLists.txt` (which only
  initialize a cache variable if it doesn't already exist): `BUILD_SHARED_LIBS
  OFF` (a static `libassimp.a`, so `engine_app` needs no runtime
  rpath/`LD_LIBRARY_PATH` to find a shared lib), `ASSIMP_BUILD_TESTS`/
  `ASSIMP_BUILD_ASSIMP_TOOLS`/`ASSIMP_BUILD_SAMPLES`/`ASSIMP_BUILD_DOCS`/
  `ASSIMP_INSTALL OFF`, `ASSIMP_NO_EXPORT ON` (this phase only imports),
  `ASSIMP_WARNINGS_AS_ERRORS OFF` (Assimp defaults this ON; a newer host
  compiler tripping one of *its* warnings isn't something this project
  should let fail the build), `ASSIMP_BUILD_ZLIB OFF` (this container has
  `zlib1g-dev`, so Assimp's own `find_package(ZLIB)` finds the system copy).
  Importer scope is deliberately narrowed via
  `ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF` plus `ASSIMP_BUILD_OBJ_IMPORTER`/
  `ASSIMP_BUILD_GLTF_IMPORTER ON` -- confirmed at configure time by CMake's
  own `Enabled importer formats: OBJ GLTF` status line -- instead of Assimp's
  full ~50-format default set, which meaningfully cuts build time/scope.
  `assimp` is linked into `engine_app` alongside `glfw`/`glm::glm`/`glad`/
  `stb_image`.
- **`engine::Model` / `engine::ModelNode`** (`include/engine/model.hpp`,
  `src/model.cpp`) -- loads a whole scene via `Assimp::Importer::ReadFile()`
  with `aiProcess_Triangulate | aiProcess_GenSmoothNormals` (no
  `aiProcess_FlipUVs`: this engine's `Texture` already flips rows on load to
  put a texture's own (0,0) at OpenGL's v=0, and `scene.obj`'s hand-written
  `vt` values already follow that same bottom-left-origin convention --
  adding the flag would flip an already-correct mapping). A null/incomplete
  scene (or `importer.GetErrorString()` on failure) is logged via `LOG_ERROR`
  and turned into a thrown `std::runtime_error`, mirroring
  `Shader`/`Window`/`Texture`'s throw-on-failure convention -- never a raw
  null-scene dereference. Every `aiMesh` becomes an `engine::Mesh` (missing
  normals/UV channels default to a zero vector/`vec2` rather than reading
  `mesh->mNormals`/`mesh->mTextureCoords[0]` when either can be null; a
  non-triangular leftover face is skipped with a warning instead of
  desyncing the `GL_TRIANGLES` draw call). Every `aiNode` becomes a
  `ModelNode` (name, local transform, mesh indices, children), preserving
  the scene's parent/child structure; a node's `aiMatrix4x4` (Assimp: row-
  major, translation in the last column of each row) is converted to
  `glm::mat4` (column-major) by placing each element at its transposed
  `[col][row]` slot -- not a raw memcpy, which would silently transpose the
  whole matrix. Each `aiMaterial` becomes one `engine::Material`: its
  diffuse texture (if any) is resolved relative to the model file's own
  directory and loaded, falling back to the engine's existing checker
  texture (still tinted by the material's own `Kd`/shininess) if there is no
  diffuse texture or it fails to load -- a material this engine doesn't
  fully understand never crashes the load. `Model::draw(shader, rootTransform)`
  recurses depth-first, composing `worldTransform = parentTransform *
  node.localTransform` at each node, uploading that node's `uModel`/
  `uNormalMatrix`, then binding each of the node's mesh(es)' `Material` and
  drawing. Fully RAII: `Model` owns every `Mesh`/`Material` it built (plus a
  `defaultMaterial_` fallback) by value, so its destructor releases every GL
  handle with no manual cleanup code, and it's move-only for the same reason
  `Mesh`/`Material`/`Texture` are (each owns scarce GL handles).
- **Test asset** (`assets/models/scene.obj` + `scene.mtl`) -- hand-authored:
  a table (a wide flat box), a small box sitting exactly on the table's top
  face, and a separate pyramid off to the side with a deliberate 0.4-unit
  gap from the table -- three distinct objects at different positions/
  heights, proving the node-hierarchy + transform-composition code actually
  places more than one mesh, not just that a single mesh loads. Each object
  has its own `Kd`-only material (no texture image), so `Model`'s
  no-diffuse-texture fallback path (default checker texture, tinted by
  `Kd`) is exercised for every mesh in this asset. Plain OBJ has no
  per-object transform field of its own (unlike glTF/FBX), so each object's
  vertices are given directly in final world-space coordinates and Assimp's
  OBJ importer gives each object node an identity local transform --
  documented in a comment at the top of `scene.obj` rather than left as an
  unexplained gap. Non-identity composition is still exercised end to end
  via `Application`'s `sceneTransform_` (a fixed 12-degree Y rotation applied
  above the whole node tree, composed with each node's own, mostly-identity
  transform).
- **Wired into `Application`**: the single `cube_`/`material_` pair is
  replaced with one `model_` (loaded from `assets/models/scene.obj`) and a
  `sceneTransform_` (`engine::Transform`, replacing `cubeTransform_`).
  `render()` sets view/projection/lighting uniforms once per frame (they're
  scene-level state, unchanged by the repeated same-program `glUseProgram`
  calls each `Material::bind()` makes while `Model::draw()` walks the tree),
  then calls `model_.draw(shader_, sceneTransform_.getModelMatrix())`.
  Camera position/lighting are unchanged from Phase 3/4.
- **Verify**: `ENGINE_MAX_FRAMES=90 bash tools/run_headless.sh
  build/engine_app build/phase5_screenshot.png` renders a pyramid, a table,
  and a box on the table at clearly different screen positions. Checked
  with ImageMagick: `convert phase5_screenshot.png -fuzz 10% -transparent
  "rgb(100,149,237)"` (the clear color) isolates the rendered geometry from
  the background, and `-define connected-components:verbose=true
  -connected-components 4` on the resulting mask finds **two** disjoint
  non-background components -- one ~94x83px (the pyramid) and one
  ~209x106px (the table + box, touching in screen space) -- confirming
  multiple distinct clusters at different image locations rather than one
  blob. The overall trimmed bounding box of non-background pixels is
  272x147px (out of the 800x600 frame), far larger than the roughly
  cube-sized footprint Phase 4's single half-extent-0.5 cube produced at the
  same unchanged camera position -- confirming the scene's actual extent
  (~2.7 world units wide) rather than a single ~1-unit cube.

### Phase 6: engine foundations (MSAA, Entity, ResourceManager, InputState)

Phase 6 is the final planned phase. It's deliberately a structural/
foundations phase, not a new rendering feature on top of the scene: nothing
about *what* gets rendered changes from Phase 5 (same camera, same
lighting, same `scene.obj`), only *how the engine is put together* changes,
plus one genuinely new rendering capability (MSAA).

- **MSAA anti-aliasing** (`include/engine/window.hpp`, `src/window.cpp`) --
  `Window`'s constructor now calls `glfwWindowHint(GLFW_SAMPLES, 4)` before
  window/context creation, and `glEnable(GL_MULTISAMPLE)` right after the
  context becomes current. `GLFW_SAMPLES` is a *hint*, not a guarantee, so
  the constructor also queries the real `GL_SAMPLES` value via
  `glGetIntegerv()` and logs what it actually got -- a warning (not a
  thrown error) if it came back 0, rather than silently claiming success.
  On this project's headless verification stack (Xvfb + Mesa llvmpipe),
  that check mattered in practice: probing Xvfb's own GLX implementation
  directly (`glXGetFBConfigs`) turns up **zero** GLX framebuffer configs
  with `GLX_SAMPLE_BUFFERS > 0` -- Xvfb's GLX extension simply never
  advertises a multisample-capable visual on this stack, so GLFW's default
  GLX-based context creation always came back with `GL_SAMPLES = 0`
  regardless of the hint. Probing EGL instead (`eglGetConfigs`/
  `eglGetConfigAttrib`) on the exact same display/driver found 25
  window-capable EGL configs with up to 4 samples -- EGL's own config
  negotiation doesn't go through the X server's (limited) GLX extension at
  all. `Window`'s constructor therefore also sets
  `glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API)`,
  guarded to Linux only (`#if defined(__linux__)`; EGL context creation
  isn't available on macOS), which is what actually gets `GL_SAMPLES = 4`
  on this headless stack. This is applied unconditionally on Linux (not
  just when GLX turns out to lack multisample configs) because it's cheap
  and verified-safe on the one Linux target this project actually tests
  against; it hasn't been validated against every real desktop Linux GLX
  setup (indirect/network GLX such as `ssh -X`, an old proprietary driver,
  a system with no `libEGL` at all), so `ENGINE_DISABLE_EGL_CONTEXT` (any
  non-empty, non-`"0"` value) is a bug-review-added escape hatch back to
  GLFW's default GLX path for anyone who hits a regression there, matching
  this project's existing `ENGINE_MAX_FRAMES`/`ENGINE_CAMERA_DEMO`
  getenv-gated-behavior convention. GLAD's hand-written `glad.h` gained the
  `GL_MULTISAMPLE`/`GL_SAMPLE_BUFFERS`/`GL_SAMPLES` enum values it didn't
  previously define (the functions used to enable/query them, `glEnable`/
  `glGetIntegerv`, were already loaded).
- **`engine::Entity`** (`include/engine/entity.hpp`, header-only) -- the
  smallest possible "thing in the scene" concept: a `Transform` plus an
  optional `std::shared_ptr<Model>`. Replaces Phase 5's `model_` +
  `sceneTransform_` pair (two loose `Application` members that only worked
  for exactly one object) with `Application::entities_`, a
  `std::vector<Entity>`, so `render()` iterates entities rather than
  assuming exactly one. Deliberately *not* a general archetype/sparse-set
  ECS -- no component pools, no systems scheduler -- just enough structure
  to establish the "things in the world have a transform + renderable data
  and can be enumerated" pattern a later phase's real ECS could replace.
- **`engine::ResourceManager`** (`include/engine/resource_manager.hpp`,
  `src/resource_manager.cpp`) -- a per-key `unordered_map<key,
  shared_ptr<T>>` cache for `Shader`/`Texture`/`Model`, populated on first
  request (`getShader`/`getTexture`/`getModel`) and returned again on any
  later request for the same key, instead of each asset being loaded
  ad-hoc. `Model`'s constructor now also takes a `ResourceManager&` and
  routes its own texture loads (each material's diffuse map, plus the
  shared default/fallback checker texture) through the same cache --
  concretely fixing a real redundant-load bug this refactor surfaced:
  Phase 5's `Model` reconstructed (reloaded and re-uploaded to the GPU) its
  own checker-texture fallback once per material that needed one, so
  `scene.obj`'s three no-texture materials plus `defaultMaterial_` loaded
  the exact same PNG **four separate times** every run. Verified fixed --
  the headless run's log now shows exactly one `Texture loaded:
  assets/textures/checker.png` line (and one matching `ResourceManager:
  cached texture` line) for the whole run, not four. `Material` now holds
  its diffuse texture as a `std::shared_ptr<Texture>` (previously an owned
  `Texture` by value) so several `Material`s can share one cached instance.
  Deliberately not a general asset-management system: no LRU/eviction, no
  hot-reload, no async loading.
- **`engine::InputState`** (`include/engine/input.hpp`, `src/input.cpp`) --
  a small POD snapshot (WASD-equivalent movement flags, Escape, raw cursor
  position) that `Application::run()` polls once per frame from `Window`
  (`pollInputState()`) and passes down to `update()`/`Camera`, instead of
  `Camera` calling `Window::isKeyPressed()`/`getCursorPos()` itself.
  `Camera::processKeyboard(const Window&, float)` (Phase 3-5) became
  `Camera::processMovement(const InputState&, float)`; `Camera` no longer
  includes `<GLFW/glfw3.h>` or `window.hpp` at all. Not a general
  action-mapping/rebinding system as of this phase -- just the concrete
  fields this phase's `Camera` reads (Phase 8d later adds a real, data-driven
  binding table underneath `pollInputState()`, without changing `InputState`'s
  fields or `Camera`'s interface at all -- see that phase's own section).
- **Delta-time**: unchanged from Phase 1 (`glfwGetTime()`-based), now
  threaded through `Application::update(double deltaTime, const
  InputState& input)` alongside the polled input rather than a second time
  source being introduced.
- **Verify**: same headless harness, e.g.
  ```sh
  ENGINE_MAX_FRAMES=90 bash tools/run_headless.sh build/engine_app build/phase6_screenshot.png
  ```
  The log now includes an `MSAA active: GL_SAMPLES = 4 (requested 4)` line
  (a `GL_SAMPLES reports 0` warning if the GL/driver combo doesn't honor
  the hint). Pixel-level proof that real multisample blending occurred (not
  just that the flag was accepted): sampling along the pyramid's left
  slanted silhouette edge (a non-axis-aligned edge against the background,
  unlike the table/box's mostly axis-aligned edges) at absolute pixel
  `(272, 231)` finds color `(83, 82, 128)`, flanked by the background clear
  color `(100, 149, 237)` at `(271, 231)` and the pyramid's dark checker-face
  color `(28, 14, 26)` at `(273, 231)` -- a color matching neither
  neighbor and lying strictly between them in every channel (R between 28
  and 100, G between 14 and 149, B between 26 and 237), consistent with a
  4x-MSAA-resolved partial-coverage sample (a 2 background + 1 dark + 1 lit
  4-sample average predicts `(92.5, 82.25, 126.25)`, close to the observed
  value). The scene composition itself is unchanged from Phase 5: `convert
  phase6_screenshot.png -fuzz 10% -transparent "rgb(100,149,237)" -channel A
  -connected-components 4` still finds the same two large disjoint
  components -- ~208x105px (table + box) and ~93x81px (pyramid), matching
  Phase 5's ~209x106px/~94x83px almost exactly -- plus a scattering of new
  1-4px partial-alpha specks along silhouette edges that Phase 5's hard-
  edged render didn't have, itself further evidence of the new antialiased
  edges.

### Phase 7a: multiple lights, normal mapping, directional shadow mapping

Phase 7a is the first purely rendering-feature phase since Phase 6's
structural work: three additions on top of Phase 4-6's single directional
light, all forward-rendered (no G-buffer/deferred pass).

- **Multiple lights** (`assets/shaders/basic.frag`, `src/application.cpp`) --
  the Phase 4 directional light is joined by a fixed-size uniform array of
  point lights (`uPointLights[8]`/`uNumPointLights`) and spot lights
  (`uSpotLights[4]`/`uNumSpotLights`), the standard "fixed array + live
  count" forward-lighting pattern: the fragment shader loops
  `for (int i = 0; i < uNumPointLights; ++i)` etc., touching only the
  uniform slots `Application::render()` actually uploaded that frame. Point
  lights use the standard `1 / (constant + linear*d + quadratic*d^2)`
  distance attenuation; spot lights add a `smoothstep`-based soft cone
  (fading between a precomputed `cos(innerAngle)`/`cos(outerAngle)` pair)
  instead of a hard binary cutoff. `Application` places two point lights
  (warm, above `BoxOnTable`; cool, above the pyramid's apex) and one spot
  light (above the table, aimed down) at positions derived from
  `scene.obj`'s own documented object extents -- see `application.cpp`'s
  `kPointLights`/`kSpotLights` tables. Only the directional light casts a
  shadow (see below); point/spot contributions are never reduced by
  `shadowFactor()`.
- **Normal mapping** (`mesh.hpp`'s `Vertex::tangent`, `model.cpp`,
  `material.hpp`, `basic.vert`/`.frag`) -- `Mesh`'s interleaved vertex now
  carries a `tangent` (attribute location 3) alongside
  position/normal/texCoord; `Model` gets it from Assimp's own
  `aiProcess_CalcTangentSpace` post-process step rather than hand-deriving
  the UV-delta formula, since `Model` already loads through Assimp. The
  vertex shader transforms the tangent into world space via `mat3(uModel)`
  (a plain linear transform, deliberately **not** the inverse-transpose
  normal matrix -- a tangent is an ordinary direction embedded in the
  surface, unlike a normal). `Material` gained an optional (nullable)
  normal-map `Texture`; when bound, the fragment shader builds a
  Gram-Schmidt-orthogonalized TBN matrix and rotates the sampled tangent-
  space normal (`texture(...).rgb * 2.0 - 1.0`) into world space, the same
  space the light positions/directions are already expressed in. The
  bitangent is derived as `cross(normal, tangent)` rather than carried as
  its own vertex attribute (no separate handedness tracking -- a documented
  simplification). `scene.obj`'s existing Kd-only materials have no normal
  map and render exactly as before; a hand-built ground plane
  (`mesh.hpp`'s `makeGroundPlane()`, drawn directly by `Application`
  alongside `entities_`, not through `Model`) is this phase's one
  normal-mapped demo surface, using a procedurally-generated
  `assets/textures/normal_bump.png` (a mostly-flat `rgb(128,128,255)` tangent-
  space normal map with a few blurred, tilted-normal blotches, made with
  ImageMagick).
- **Directional shadow mapping** (`shadow_map.hpp`/`.cpp`,
  `assets/shaders/shadow.vert`/`shadow.frag`, `Application::renderShadowPass()`)
  -- a depth-only FBO + `GL_DEPTH_COMPONENT` texture (`ShadowMap`, RAII,
  move-only like every other GL-handle-owning class here). Each frame,
  `Application::render()` first renders the whole scene (every entity via a
  new `Model::drawDepthOnly()`, plus the ground plane) into `shadowMap_`
  through a minimal depth-only program (`shadow.vert` computes
  `uLightSpaceMatrix * uModel * aPos`; `shadow.frag` is an empty `main()`,
  since the FBO has no color attachment to write), using a fixed
  orthographic projection + a `lookAt` from a chosen point back along the
  directional light's own direction (a real "position" a directional light
  doesn't otherwise have, picked purely to render this one depth pass from).
  `glDrawBuffer(GL_NONE)`/`glReadBuffer(GL_NONE)` are set on that FBO for
  driver-completeness. The main pass then re-projects each fragment into the
  same light-clip-space (`vFragPosLightSpace`, computed once per vertex),
  perspective-divides, remaps to `[0,1]`, and compares depths with a
  slope-scaled bias (`max(0.006 * (1 - N.L), 0.0015)` -- bigger for
  glancing-angle surfaces, where depth-map quantization error is worse,
  smaller for surfaces facing the light head-on, avoiding both shadow acne
  and excessive peter-panning) to compute a 0/1 shadow factor that only
  reduces the directional light's own diffuse+specular terms.
  - **A deliberate lighting-direction change**: Phase 4-6's directional
    light (`(-0.5, -1.0, -0.3)`) casts shadows on the side of each object the
    light continues past -- towards `-x, -z` here -- which is *away* from
    `kDefaultCameraPosition` (`(2.6, 1.9, 3.4)`, established since Phase 3):
    from that camera, any shadow so cast falls mostly behind its own caster,
    invisible. Since this phase's whole point is a shadow the screenshot can
    actually show, `kLightDirection` was changed to `(0.6, -0.7, 0.35)` --
    same general "steep sun" character, but shadows now fall towards
    `+x, +z`, the side the long-established default camera actually views
    from -- rather than moving that camera instead.
- **Verify**: `ENGINE_MAX_FRAMES=90 bash tools/run_headless.sh
  build/engine_app build/phase7a_screenshot.png`. Verified by re-rendering
  with one factor at a time forced off/on (a temporary shader/source edit,
  reverted after) and diffing against the real screenshot:
  - *Shadow*: forcing `shadowFactor()` to `0` and diffing against the real
    render isolates a coherent dark region (not scattered noise) roughly
    behind the table/box; sampling the same ground material at a shadowed
    point vs. a nearby unshadowed point in the real screenshot gives
    `(47, 63, 69)` vs. `(255, 255, 224)` -- clearly, measurably darker.
  - *Normal mapping*: rendering the ground plane with `normalMap = nullptr`
    produces smooth, gradient-only shading with zero geometric variation on
    that flat quad; the real render shows a repeating pattern of soft
    circular blotches (matching `normal_bump.png`'s tiled bump pattern) laid
    over that gradient -- e.g. one such blotch samples `(80, 108, 169)`
    with the normal map vs. `(204, 238, 255)` at the same pixel without it.
  - *Multiple lights*: forcing `uNumPointLights`/`uNumSpotLights` to `0` and
    diffing shows a real (if modest -- these lights sit close to their
    target surfaces) contribution, concentrated near the box and pyramid;
    e.g. near the box, a table-top pixel goes from `(18, 7, 1)` (directional
    only) to `(88, 39, 6)` with the warm point light added -- R rising far
    more than G/B, consistent with that light's `(1.0, 0.35, 0.15)` color;
    near the pyramid, `(7, 17, 40)` becomes `(12, 36, 94)` with the cool
    point light added -- B rising far more than R/G, consistent with its
    `(0.15, 0.55, 1.0)` color.

### Phase 7b: skybox, HDR + tonemapping, a small render-target class

Phase 7b is another purely rendering-feature phase: a procedural-sky
background and a floating-point HDR pipeline with tonemapping, on top of
Phase 7a's multi-light/shadowed/normal-mapped forward renderer.

- **Skybox** (`skybox.hpp`/`.cpp`, `assets/shaders/skybox.vert`/`.frag`,
  `assets/textures/skybox/`) -- a 6-face `GL_TEXTURE_CUBE_MAP` rendered as
  the scene's background. The 6 faces (`right`/`left`/`top`/`bottom`/
  `front`/`back.png`, 512x512) were generated procedurally with ImageMagick
  rather than sourced/painted: the 4 side faces are an identical vertical
  gradient (`gradient:'#8ec9f0-#0b1d3a'`, light sky blue at the top edge to
  dark navy at the bottom edge); `top.png` is a radial gradient from a
  bright zenith highlight down to that *same* `#8ec9f0` at every edge/corner
  (ImageMagick's default radial-gradient radius reaches exactly the corner
  color at the image boundary -- verified with `identify -format` before
  committing to the approach); `bottom.png` is a radial gradient down to
  that same `#0b1d3a` at its edges. The result: every one of the 6 faces'
  shared edges is colored *identically* by construction (side-to-side
  because all 4 are the same image; side-to-top/bottom because the radial
  gradients' outer color was chosen to exactly match the vertical
  gradient's own top/bottom color) -- not just visually close, but the same
  sRGB value, so there is no seam to be found at any of the 12 face-boundary
  edges regardless of viewing angle. Face order/GL target mapping follows
  the common `right/left/top/bottom/front/back` ->
  `GL_TEXTURE_CUBE_MAP_POSITIVE_X/NEGATIVE_X/POSITIVE_Y/NEGATIVE_Y/
  POSITIVE_Z/NEGATIVE_Z` convention (`skybox.cpp`'s `kCubeMapTargets`,
  zipped 1:1 against `Skybox`'s constructor argument order) -- the classic
  place this kind of feature silently jumbles itself if the order and the
  target table ever drift apart. `Skybox::draw()` uploads a
  translation-stripped view matrix (`mat4(mat3(view))`, so the sky rotates
  but never translates with the camera) and draws a plain unit cube (reusing
  `makeCube()` rather than a second hand-rolled VAO) with `gl_Position.z`
  forced to `gl_Position.w` in `skybox.vert` (pins every skybox fragment's
  depth to exactly the far plane) plus `glDepthFunc(GL_LEQUAL)` around the
  draw call (restored to `GL_LESS` after) -- together these mean the skybox
  only shows through pixels nothing else drew this frame, without ever
  needing to disable depth testing. Drawn *last*, after every opaque
  entity/the ground plane.
- **`Framebuffer`** (`framebuffer.hpp`/`.cpp`) -- a small reusable
  "off-screen render target" class, deliberately modeled on `ShadowMap`'s
  existing shape (RAII, move-only, `bindForWriting()`/a bind-for-reading
  method, throws on `GL_FRAMEBUFFER_COMPLETE` failure): an FBO with one
  `GL_RGBA16F` (floating-point) color attachment plus a `GL_DEPTH_COMPONENT24`
  depth *renderbuffer* (not a texture -- nothing needs to sample this
  target's depth the way the shadow pass samples `ShadowMap`'s). Stops
  there rather than growing into a general multi-pass render graph: a
  future bloom pass could reuse this same class for its own downsample/blur
  targets, but that pass isn't built this phase.
- **HDR + tonemapping** (`application.cpp`, `assets/shaders/postprocess.vert`/
  `.frag`) -- `Application::render()` now renders the whole lit scene (+
  skybox) into `hdrFramebuffer_` (sized from the window's real framebuffer
  size at construction -- `Window` has no resize callback for this to react
  to, so, like every other fixed-at-construction GL resource in this
  engine, it only needs sizing once) instead of straight to the window.
  A fullscreen quad (`mesh.hpp`'s new `makeFullscreenQuad()`, already
  authored directly in NDC space -- no model/view/projection needed for a
  pass that operates purely in screen space) then resolves that HDR color
  buffer to the default framebuffer: Reinhard tonemapping
  (`color / (color + 1)`) compresses the unbounded HDR range into `[0,1)`
  with a smooth rolloff, followed by gamma correction (`pow(x, 1/2.2)`) --
  this is the one place in the whole per-frame pipeline that gamma-corrects;
  neither `basic.frag` nor `skybox.frag` does. `kPointLights[0]`'s color
  (`application.cpp`) was deliberately bumped well above the old `[0,1]`
  range (`{6.0, 2.1, 0.9}`, up from `{1.0, 0.35, 0.15}`) specifically so
  this pipeline has real overbright values to tonemap -- HDR's whole point.
- **Verify**: `ENGINE_MAX_FRAMES=90 bash tools/run_headless.sh
  build/engine_app build/phase7b_screenshot.png`. One flake was found and
  understood (not a rendering bug) while verifying: on an occasional run,
  `tools/run_headless.sh`'s screenshot loop -- which stops at its *first*
  successful capture, without checking that capture's content -- grabbed a
  frame before the app's first `swapBuffers()` had actually happened,
  producing an all-black PNG; this phase's heavier per-frame GL work (two
  new shader programs, an extra FBO bind/clear, a cubemap sample) made that
  narrow startup window more likely to be hit than in earlier, lighter
  phases. Re-running the same command a moment later (and confirming via
  `convert screenshot.png -format "%[fx:standard_deviation]" info:` that the
  result isn't a uniform/degenerate image) reliably produces a real frame --
  this is a harness timing race, not anything `Application`/`Skybox`/
  `Framebuffer` gets wrong.
  - *Skybox*: the real screenshot's background is a smooth vertical
    gradient, not flat cornflower blue -- sampling straight down a column
    behind the scene gives `(136, 154, 165)` near the top of frame fading to
    `(120, 140, 154)` further down, a gradual change, not a step. Sampling
    *across* a row at a fixed height (`x = 0, 200, 400, 600, 799` at a fixed
    `y`) gives `(133,152,164)`, `(134,153,164)`, `(134,153,164)`,
    `(134,152,164)`, `(133,152,164)` -- effectively identical, confirming no
    visible seam where the two visible side faces meet in this framing (as
    expected, since all 4 side faces are pixel-identical by construction).
  - *HDR/tonemap*: rendered once with the real (Reinhard) `postprocess.frag`
    and once with a temporary edit removing only the Reinhard line (gamma
    correction alone, reverted immediately after capturing) to reproduce
    exactly what Phase 7a's pre-HDR pipeline would have shown for the same
    bumped light. A horizontal scanline through the brightest pixel near
    `kPointLights[0]` (found by scanning the real screenshot for the
    maximum red channel value, at `(434, 366)`): the tonemapped version
    ranges smoothly from `223` down to `124` red over `x = 434..520` (never
    reaching `255`, no plateau); the no-Reinhard version *hard-clips* to
    `255` (actually `(255,255,~195)`, a saturated yellow-white) across
    `x = 400..470`, then drops sharply once the cubemap/tile pattern takes
    over -- a flat plateau with a hard edge, exactly the "clipped white
    disc" the Reinhard version's smooth rolloff avoids.
  - *No regression*: the same shadow/normal-map checks Phase 7a's review
    used still hold through the new HDR/post-process pipeline -- a ground
    pixel right at the pyramid's shadow boundary goes from `(197,188,187)`
    (sunlit) to `(52,47,41)` (shadowed) over 10 pixels, and the normal map's
    tiled soft-blotch pattern is still visible across the ground plane in
    the final composited screenshot.

### Phase 9: real metallic/roughness PBR + a sphere reference grid

Phase 9 begins a "full PBR" arc: a second, independent material/shader pair
implementing the standard Cook-Torrance microfacet BRDF, alongside (not
replacing) Phase 4/7a's Blinn-Phong `Material`/`basic.vert`/`basic.frag`
path, which still drives the table/box/pyramid/ground unchanged. Ambient
lighting here is still a flat placeholder term (`uAmbientColor * albedo *
ao`), not real image-based lighting -- that's explicitly the next phase's
job.

- **`PBRMaterial`** (`include/engine/pbr_material.hpp`) -- a value bundle
  (albedo tint, metallic, roughness, an ambient-occlusion scalar, plus two
  optional nice-to-have textures not used by this phase's demo content) and
  a `bind()` that uploads it as uniforms, shaped like `Material` but
  copyable rather than move-only (no exclusive GL handle of its own).
  Roughness is clamped away from exactly 0 in `bind()`
  (`PBRMaterial::kMinRoughness = 0.045`) -- `alpha = roughness^2` is singular
  at `alpha == 0`.
- **`assets/shaders/pbr.vert`/`pbr.frag`** -- `pbr.vert` is contract-identical
  to `basic.vert` (same attributes/uniforms/varyings). `pbr.frag` implements:
  - **Normal distribution** (GGX/Trowbridge-Reitz): `D = alpha^2 / (pi *
    ((N.H)^2 * (alpha^2-1) + 1)^2)`, `alpha = roughness^2` (the standard
    remapping, not roughness used directly).
  - **Geometry** (Smith, Schlick-GGX, direct-lighting `k`):
    `k = (roughness+1)^2 / 8` (NOT the IBL `k = roughness^2/2`, a different
    constant for a later phase), `G = G1(N,V,k) * G1(N,L,k)`.
  - **Fresnel** (Schlick): `F = F0 + (1-F0) * (1 - (H.V))^5`,
    `F0 = mix(vec3(0.04), albedo, metallic)` -- the physically-important
    metal/dielectric split: metals tint their specular by their own albedo,
    dielectrics reflect a small neutral ~4% at normal incidence regardless of
    albedo color.
  - **Combine**: `specular = (D*G*F) / (4 * max(N.V,eps) * max(N.L,eps) +
    eps)`; **diffuse**: `kD = (1-F) * (1-metallic)`,
    `diffuse = kD * albedo / pi` -- the `(1-metallic)` factor is what zeroes
    a metal's diffuse response; forgetting it is a common PBR bug.
  - Reuses `basic.frag`'s point/spot light uniform arrays, attenuation, and
    spot-cone logic verbatim (only what happens to each light's radiance
    once it reaches the fragment changes), and its tangent-space
    normal-mapping TBN construction + shadow-map PCF sampling (the shadow
    factor still multiplies only the direct-light terms, never the ambient
    term).
  - **Bug found and fixed during this phase's own verification**: the GGX
    denominator's divide-by-zero guard was originally `max(denom, 1e-7)`.
    At `N.H == 1` that denominator is exactly `pi * alpha^4`, which is
    already only `~5.3e-11` at this engine's own minimum roughness
    (`PBRMaterial::kMinRoughness = 0.045`) -- 27 orders of magnitude above
    float32's smallest normal value, so no real underflow risk, but *below*
    the `1e-7` floor. That floor was silently clamping away the entire peak
    brightness advantage of the smoothest spheres: re-evaluating this exact
    formula in Python for roughness 0.05 vs. 0.2 at `N.H == 1` found the 0.05
    case coming out *dimmer* at its own peak than the 0.2 case -- backwards
    from "lower roughness = sharper, brighter highlight." Fixed by lowering
    the floor to `1e-12` (safely below every denominator this engine
    legitimately produces, still far above float32 underflow).
- **`Mesh::makeUVSphere(latSegments, lonSegments, radius)`** (`mesh.hpp`/
  `.cpp`) -- a standard UV sphere with analytic per-vertex normals
  (`normalize(position)`, exact for a sphere centered at the origin) and
  tangents (`d(position)/d(phi)`, closed-form: `(-sin(phi), 0, cos(phi))`),
  built as a `(latSegments+1) x (lonSegments+1)` vertex grid so every vertex
  (including the poles) gets its own well-defined texCoord.
- **The sphere test-grid** (`Application`'s `sphereMesh_`/`sphereInstances_`)
  -- two single-axis rows of `kSphereRowLength` (4) spheres each, one shared
  albedo (a saturated red-orange) so the metal/dielectric Fresnel distinction
  is directly comparable across both rows: a metallic sweep (0 -> 1 left to
  right at a fixed low roughness) and a roughness sweep (`kMinPBRRoughness`
  -> `kMaxPBRRoughness` left to right at fixed metallic = 1). **Bug found and
  fixed during this phase's own verification**: the original design packed both
  axes into one 4x4 matrix (metallic across columns, roughness across rows)
  instead -- varying two BRDF parameters across the same 16-sphere grid at
  once made it hard to look at any one sphere and tell which axis a visible
  difference from its neighbor was actually demonstrating, since every
  sphere differed from its row-neighbor *and* its column-neighbor
  simultaneously. Splitting it into two separate single-axis rows (one
  fixed-roughness metallic sweep, one fixed-metallic roughness sweep) fixes
  that: each row isolates exactly one BRDF parameter, with the other held
  constant, matching the classic "PBR reference chart" layout this phase
  models itself on. Built directly in the camera's own image plane (its
  right/up basis vectors, derived from `kDefaultCameraPosition`/
  `kSceneCenter`) rather than laid out along world X/Z: an axis-aligned grid
  recedes away from the camera along a mostly depth-facing direction at this
  engine's fixed camera angle, foreshortening spacing so hard that adjacent
  spheres visibly crowded on screen even with generous world-space spacing
  -- confirmed by re-projecting sphere centers through the same
  view/projection matrices `Application` builds. A camera-facing layout
  instead faces the camera edge-on like a real reference chart, every sphere
  equidistant from the camera, evenly spaced in screen space: columns follow
  the camera's own (always-horizontal) right vector, while the two rows
  themselves are stacked along plain world Y rather than the camera's own
  (pitched) up vector -- an earlier version of this same fix used the
  camera's up vector for the row offset too, which silently sank the lower
  row far enough to interpenetrate the ground plane, caught in this same
  screenshot-driven review. Placed in front of the existing table/box/pyramid
  scene (closer to the camera) so both the new PBR content and the existing
  Blinn-Phong-lit scene remain visible in the same frame. Both
  `renderShadowPass()` and `render()`'s PBR pass re-upload the same
  view/projection/light-space/light-array/shadow-map uniforms already
  uploaded for the Blinn-Phong pass -- GL uniform state lives per-program,
  so switching the active program does not carry them over.
- **Verify**: `ENGINE_MAX_FRAMES=90 bash tools/run_headless.sh
  build/engine_app build/phase9_screenshot.png`. Verified three ways:
  1. *The exact BRDF equations, independent of rendering*: `pbr.frag`'s
     formulas re-implemented directly in Python (no screenshot/tonemap
     involved) confirm `D`'s peak strictly decreases and its half-max
     angular width strictly increases with roughness (`D(N.H=1)`:
     `50929.6 -> 198.9 -> 5.09 -> 0.318` for roughness `0.05, 0.2, 0.5, 1.0`;
     half-width `0.57 deg -> 1.52 deg -> 9.58 deg -> flat/uniform`), and that
     for the same albedo `(0.85, 0.12, 0.08)`, a dielectric's specular color
     comes out exactly neutral (`(1.0, 1.0, 1.0)` ratio) while its diffuse
     keeps the full albedo tint, versus a metal's specular color coming out
     `(1.0, 0.141, 0.094)` -- an exact match to `albedo`'s own
     `(1.0, 0.141, 0.094)` ratio -- with zero diffuse. Grazing-angle
     (`N.V`/`N.L` near 0) and roughness-extreme cases all stay finite.
  2. *The rendered grid, visually*: cropped close-ups show a tiny, sharp,
     near-white pinpoint highlight on the smoothest dielectric sphere; the
     same tiny sharp highlight with a warm/orange cast on the smoothest
     metal sphere; no distinguishable highlight at all (a soft, uniform
     gradient) on the roughest dielectric sphere; and a broad, soft, reddish
     sheen (no tight point) on the roughest metal sphere.
  3. *Pixel data*: re-projecting each sphere's world position through
     `Application`'s own view/projection matrices and sampling a
     lit-but-off-specular-axis point (faces both the directional light and
     the camera, avoiding both the unlit far side and the specular hotspot)
     on the grid's un-occluded top row gives `(67,12,9)` at metallic 0.0,
     falling monotonically to `(54,10,7)`, `(44,8,6)`, and `(39,6,5)` at
     metallic 1.0 -- the metallic=1.0 value matches the ambient-only floor
     sampled everywhere else on that sphere, confirming its diffuse term is
     genuinely zero, while the dielectric sphere's diffuse keeps the scene's
     red albedo visibly brighter than ambient alone.
  - *No regression*: Phase 7a/7b's shadows, ground normal mapping, skybox,
    HDR/tonemap, and MSAA (`GL_SAMPLES = 4`, confirmed in the run log) are
    all still visible/active in the same screenshot -- nothing in the
    existing Blinn-Phong path or post-process pipeline was touched.

### Phase 10: real-time image-based lighting (IBL)

Phase 10 replaces `pbr.frag`'s flat placeholder ambient term
(`uAmbientColor * albedo * ao * (1-metallic)`) with real image-based
lighting, via the standard real-time split-sum approximation (Karis, "Real
Shading in Unreal Engine 4"): the environment `Skybox` already renders is
convolved once at startup into two precomputed environment maps plus one
environment-independent BRDF LUT, all sampled every frame instead of a
single hand-picked ambient constant.

- **`IBLProbe`** (`include/engine/ibl_probe.hpp`/`src/ibl_probe.cpp`) --
  built once in `Application`'s constructor from `skybox_.textureId()`,
  producing three GL textures:
  1. **Diffuse irradiance cubemap** (32x32/face, `GL_RGBA16F`) --
     `assets/shaders/irradiance_convolution.frag` integrates incoming
     radiance over the hemisphere around each texel's own direction, a
     discretized double loop over spherical coordinates (`phi` step 0.05,
     `theta` step 0.05) weighted by `cos(theta)*sin(theta)` (Lambert's law +
     the solid-angle element), normalized by the actual sample count
     accumulated rather than a closed-form constant tied to one specific
     step size.
  2. **Prefiltered specular cubemap** (128x128 base, 5 mips, `GL_RGBA16F`,
     `GL_LINEAR_MIPMAP_LINEAR`) -- `assets/shaders/prefilter.frag`
     importance-samples the GGX distribution (Hammersley sequence +
     `ImportanceSampleGGX`, the standard `N == V == R` real-time
     simplification) at a fixed roughness per mip (`0, 0.25, 0.5, 0.75, 1.0`
     for mips `0..4`), rendered directly into each `(face, mip)` pair via
     `glFramebufferTexture2D`'s mip parameter. 32 samples/texel (deliberately
     below the 1024 a single-LUT pass can afford -- see that shader's own
     comment) keeps the one-time convolution cost tractable on a software
     (llvmpipe) GL rasterizer with no real GPU parallelism.
  3. **BRDF integration LUT** (128x128, `GL_RGBA16F`, only `.rg` meaningful)
     -- `assets/shaders/brdf_lut.frag` (paired with the existing
     `postprocess.vert`, itself just a screen-space passthrough) integrates
     the split-sum's environment-independent BRDF half across a
     `(N.V, roughness)` grid, 1024 samples/texel, once.
  - Both cubemap passes share `cubemap_capture.vert` (a fixed 90-degree-FOV
    view/projection per face, no depth trick -- unlike `skybox.vert`, this
    is an offline convolution pass with depth testing disabled entirely, not
    a per-frame background draw) and a single temporary FBO reused across
    all three passes (deleted at the end of `IBLProbe`'s constructor -- no
    persistent render-target class needed for a one-time precompute).
  - `GL_RGBA16F`/`GL_RGBA` used throughout (not a 3- or 2-channel format)
    because this project's vendored `external/glad/include/glad/glad.h` is a
    hand-pruned GL 3.3 subset (only what earlier phases needed) that doesn't
    define `GL_RGB16F`/`GL_RG16F` -- the unused extra channel(s) are simply
    never read back.
- **`pbr.frag`'s new ambient term** (replacing Phase 9's flat placeholder):
  `kS = fresnelSchlickRoughness(N.V, F0, roughness)` (the roughness-aware
  Fresnel variant, clamping the Schlick curve's upper bound to
  `max(1-roughness, F0)` instead of a flat 1.0 -- tempers a rough surface's
  grazing-angle overestimate versus the plain Schlick curve direct lighting
  uses), `kD = (1-kS)*(1-metallic)`; `diffuseIBL = irradiance(N) * albedo`;
  `R = reflect(-V,N)`, `prefilteredColor = textureLod(prefilterMap, R,
  roughness * MAX_REFLECTION_LOD)`; `specularIBL = prefilteredColor *
  (F0*envBRDF.x + envBRDF.y)`; `ambient = (kD*diffuseIBL + specularIBL) *
  ao`. `uAmbientColor` itself is no longer read by `pbr.frag` at all.
- **Sphere-grid material revision**: now that IBL gives a fully metallic
  surface real reflected-environment brightness instead of being near-black
  (Phase 9's `kRoughnessRowMetallic = 0.35` was an explicit stopgap for
  exactly this, see that constant's Phase 9 comment), the roughness-sweep
  row goes back to fully metallic (`1.0`), and the metallic-sweep row's
  fixed roughness drops from `0.45` to `0.2` so its IBL reflection reads as a
  recognizably coherent, mirror-ish patch rather than a soft, generic-looking
  glow.
- **Bug found and fixed during this phase's own verification**: the
  prefiltered cubemap's FBO came back `GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT`
  for every mip level past mip 0 on this project's Mesa llvmpipe driver.
  Root cause: `GL_TEXTURE_MAX_LEVEL` was left at its GL default of 1000 while
  only 5 mip levels (`0..4`) were ever uploaded -- explicitly bounding
  `GL_TEXTURE_BASE_LEVEL`/`GL_TEXTURE_MAX_LEVEL` to `[0, 4]` after upload
  fixed it. Also found (by code review, not a run failure): a logging bug
  where a `GLenum` framebuffer-status code was concatenated as `"0x" +
  std::to_string(status)` -- `std::to_string` prints decimal, so the result
  looked like hex but wasn't (`"0x36054"` for the real hex value `0x8CD6`,
  i.e. `GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT`), which is exactly what led to
  briefly misreading the error while debugging it; fixed with a small
  `snprintf("0x%04X", ...)` helper matching `gl_debug.hpp`'s own formatting.
- **Verification harness fix**: `tools/run_headless.sh`'s screenshot-capture
  loop previously stopped polling at the *first* successful `xwd`+`convert`
  call, which can't distinguish a genuinely rendered frame from Xvfb's still-
  blank root window (both "succeed" -- they just capture whatever pixels are
  there). This project's headless verification predates Phase 10, but
  `IBLProbe`'s one-time ~1-second startup convolution (between window
  creation and the first real rendered frame) made that pre-existing race
  far more likely to actually manifest -- it was caught immediately, the
  very first `run_headless.sh` capture after Phase 10 came back a ~240-byte
  flat-black PNG. Fixed by having that loop keep polling (bounded, ~8s total)
  until the captured file also clears a minimum size (a blank capture
  encodes to a few hundred bytes; a real 800x600 rendered frame, tens to
  hundreds of KB), not just "conversion succeeded."
- **Verify**: `ENGINE_MAX_FRAMES=90 bash tools/run_headless.sh
  build/engine_app build/phase10_screenshot.png` -- full run (including
  `IBLProbe`'s one-time convolution) completes in well under 5 seconds.
  Verified by actually looking at the (correctly-cropped-to-800x600)
  screenshot, not just sampling pixels: the metallic-sweep row's most
  metallic sphere shows a visibly larger, brighter, warm-toned reflective
  patch than its dielectric end (rather than Phase 9's uniform matte
  reddish-brown look); the roughness-sweep row (now fully metallic) shows a
  sharp, mirror-like highlight at its smoothest end softening into a broad,
  low-contrast sheen at its roughest end, with no sphere going flat black.
  Pixel sampling backs this up quantitatively: the roughness row's peak
  brightness strictly decreases (`751 -> 544 -> 545 -> 458` from roughness
  `0.05` to `1.0`) while the metallic row's bright-highlight pixel fraction
  strictly increases (`0.001 -> 0.002 -> 0.003 -> 0.004` from metallic `0`
  to `1`). The reflection reads warm/reddish rather than sky-blue because
  this sphere grid's albedo is a saturated, low-blue red-orange
  (`(0.85, 0.12, 0.08)`) and a metal's specular tints by its own albedo
  (`F0 = albedo`) -- physically correct (a copper- or gold-like metal
  reflects a blue sky as a warm tone, not blue), not a bug. *No regression*:
  the skybox background, shadows, ground normal mapping, HDR/tonemap, MSAA,
  and the Blinn-Phong table/box/pyramid are all still visible/unchanged in
  the same screenshot.

### Phase 11: textured PBR materials + tonemap-aware bloom

Phase 11 adds two independent features on top of Phase 9/10's "full PBR"
pipeline: real textured PBR materials (instead of every sphere using a flat
scalar albedo/metallic/roughness), and an HDR-aware bloom post-process pass.

- **Textured PBR materials** (`PBRMaterial`'s new `metallicRoughnessMap_`,
  see `pbr_material.hpp`'s Phase 11 comment) -- a third optional texture, a
  packed "ORM" map (R = ambient occlusion, G = roughness, B = metallic, the
  common glTF-style packing convention: one texture fetch instead of three).
  Two of the sphere grid's instances swap their flat scalar metallic/
  roughness for an albedo + ORM texture pair instead (`kRustedMetalAlbedoPath`/
  `kRustedMetalORMPath` and `kScuffedPlasticAlbedoPath`/`kScuffedPlasticORMPath`
  in `application.cpp`, procedurally generated with ImageMagick -- organic
  blob/noise masks blended between two flat colors, the same "procedural, not
  sourced/painted" approach `checker.png`/`normal_bump.png`/the skybox faces
  already used). `pbr.frag` samples the bound ORM map's G/B channels for
  roughness/metallic instead of the scalar uniforms, and multiplies its R
  channel by `uAO` (so the scalar AO knob still works as an overall
  multiplier even with a map bound). Bound at `textureUnit + 2` -- see this
  class's own header comment for why every scene-level texture unit
  (shadow map, IBL maps) shifts up by one starting this phase to keep that
  slot free.
- **Bloom** -- after the existing lit-scene-+-skybox render into
  `hdrFramebuffer_`, a bright-pass extraction (`bloomExtractShader_`/
  `assets/shaders/bloom_extract.frag`, into `brightFramebuffer_`) keeps only
  pixels whose Rec. 709 luminance exceeds `kBloomThreshold`, zeroing
  everything else; a ping-ponged separable Gaussian blur
  (`blurShader_`/`assets/shaders/blur.frag`, alternating horizontal/vertical
  between `pingpongFramebuffer0_`/`pingpongFramebuffer1_`) softens that mask
  into a glow; and the final postprocess pass (`postprocess.frag`)
  additively blends the blurred result onto the HDR color buffer *before*
  Reinhard tonemapping, so bloom is tonemapped exactly like everything else
  rather than being a separate hacky overlay pasted on afterward. All three
  new `Framebuffer`s are sized at half the window's resolution
  (`kBloomDownsampleFactor`) -- a soft, low-frequency glow, so a half-res
  blur costs a quarter the per-pixel work of a full-res one with no visible
  loss of quality.
  - `kBloomThreshold = 2.0`: a first-principles guess of `1.0` (the
    brightness Reinhard tonemapping starts compressing hardest) turned out
    too low in practice -- this scene's point lights are intense enough
    (`kPointLights[0]`'s `{6.0, 2.1, 0.9}`, `kPointLights[2]`'s
    `{5.5, 5.2, 4.7}`) that ordinary *diffuse* checker-floor texels near them,
    not just tight specular highlights, already exceed a raw HDR luminance of
    `1.0`, so a `1.0` mask included a sizable patch of ordinary floor around
    the sphere grid, not just highlight dots; blurred and added back, that
    read as the floor being washed out over a wide area rather than a soft
    glow localized to each highlight (confirmed by diffing a bloom-off render
    against this one: at `1.0` the floor between spheres brightened by
    ~30/255 with zero bloom-strength contribution expected there; at `2.0`
    that same patch is pixel-identical to the bloom-off render).
  - `kBloomBlurPasses = 6` (3 full horizontal+vertical pairs, `static_assert`ed
    even) and `kBloomStrength = 1.0` (postprocess.frag's additive blend
    multiplier, applied at the blurred texture's own real brightness) are
    both named, tunable constants rather than magic literals at their call
    sites.

Verified: clean rebuild is warning-free under `-Wall -Wextra`; a headless
screenshot shows the two textured spheres with real rust/scuff surface detail
(not flat color) and a visible soft glow around the sphere grid's brightest
specular highlights and the brightest point light, fading smoothly rather
than a hard-edged disc; *no regression* in shadows, ground normal mapping,
skybox, HDR/tonemap, MSAA, IBL reflections on the untextured spheres, or the
Blinn-Phong table/box/pyramid.

### Phase 12: GL 3.3 -> 4.3 core foundation bump

Phase 12 is a deliberately narrow foundation phase, not a rendering-feature
phase: it changes *what GL context version the engine requests*, proves
nothing about the existing rendering broke, and stops there. It exists to
lay the groundwork for Phase 13 (cascaded shadows, compute-shader clustered
lighting, HDRI, SSAO, SSR, anisotropic filtering) -- specifically, GL 4.3 is
the version that introduces compute shaders, which Phase 13's clustered
lighting will need. 4.3 rather than a bigger jump (4.5/4.6) because nothing
currently planned needs bindless textures or direct state access -- request
exactly the version the next phase's known requirements call for, not more.

- **`Window`'s context-version hints** (`src/window.cpp`) --
  `GLFW_CONTEXT_VERSION_MAJOR`/`MINOR` changed from `3`/`3` to `4`/`3`;
  `GLFW_OPENGL_PROFILE` stays `GLFW_OPENGL_CORE_PROFILE`, unchanged. GLFW's
  contract for this hint pair is to fail context creation outright (a null
  window, the pre-existing failure path this constructor already throws
  `std::runtime_error` on) rather than silently handing back a lower
  version if the driver can't satisfy the requested minimum -- confirmed
  directly on this project's own headless target by temporarily requesting
  a deliberately absurd version (9.9): `glfwCreateWindow` returned `nullptr`
  and GLFW's own error callback logged `EGL: Failed to create context:
  Arguments are inconsistent`, exactly the existing "no GL context
  available?" throw path, not a downgraded context. So a successful
  `Window` construction after this change is proof a real >=4.3 core
  context was actually granted, not a silent version downgrade going
  unnoticed.
- **Verified on both of this project's headless context-creation paths**
  (`src/window.cpp`'s Linux-only EGL path, Phase 6, plus its
  `ENGINE_DISABLE_EGL_CONTEXT=1` GLX fallback) against this container's
  actual Mesa version (`libgl1-mesa-dri` 25.2.8, `llvmpipe` LLVM 20.1.2):
  both paths report `OpenGL version: 4.5 (Core Profile) Mesa
  25.2.8-0ubuntu0.24.04.2` in the engine's own startup log, comfortably
  above the requested 4.3 floor -- this was true even *before* this
  phase's change (Mesa/llvmpipe already granted 4.5 when only 3.3 was
  requested, since core-profile context creation grants the highest
  version the driver supports that's >= the requested minimum, not exactly
  the requested version), so the version bump itself changes nothing
  observable in the log's version *number* on this specific
  environment -- what it changes is the *floor*: a driver that could only
  offer 3.3-4.2 would now fail construction instead of silently running in
  a lower-than-intended context. GLX still reports `GL_SAMPLES = 0`
  (unrelated to this phase -- Xvfb's GLX simply never advertises a
  multisample-capable visual, see Phase 6) while EGL still reports
  `GL_SAMPLES = 4`; that pre-existing MSAA behavior is unaffected by the
  version bump.
  - **On real hardware**: the user's own NVIDIA RTX 4060 (Windows) --
    which this project's history notes has already run this engine
    successfully once before -- will very likely grant a real 4.3+ core
    context outright via its native WGL path (unaffected by any of this
    phase's Linux-only EGL/GLX discussion); this headless container's
    llvmpipe software rasterizer is the only rendering target actually
    re-verified for this phase.
  - **macOS is the one platform that genuinely can't take a flat 4.3
    request**, not just an unverified one: Apple's OpenGL implementation
    (deprecated in favor of Metal since macOS 10.14, frozen since) has
    never shipped a core profile above 4.1 -- no driver or hardware
    changes that ceiling. An initial version of this phase requested 4.3
    unconditionally on every platform, which would have made
    `glfwCreateWindow` fail outright on every Mac (per the hard-fail
    contract just above) despite this codebase already carrying explicit
    macOS support (the `GLFW_OPENGL_FORWARD_COMPAT` hint right after the
    version hints, plus `paths.cpp`'s and `log.hpp`'s own `_WIN32`
    branches for the other non-Linux target). Fixed by requesting 4.1 on
    `__APPLE__` and 4.3 everywhere else (`src/window.cpp`) -- whatever in
    Phase 13 actually ends up needing compute shaders will need its own
    macOS story then (feature-gated off, or reworked), not a context that
    silently stops existing today for a phase that doesn't call anything
    4.3-specific yet.
- **GLAD loader** (`external/glad/`) -- left unchanged. Requesting a higher
  context version at creation time needs no new GL entry points by itself;
  nothing in *this* phase calls anything GL-4.3-specific (compute shaders
  are Phase 13d's job, not this one), so no new function pointers/enums
  were added to `glad.h`/`glad.c` -- doing so speculatively, before
  anything actually calls them, would violate this loader's own established
  pattern (see "GL loader" below) of only growing when a real call site
  needs it. A GL 4.3 core context still exposes every GL 3.3 entry point
  this loader already declares, so nothing broke.
- **GLSL shader version left at `#version 330 core`** (every file under
  `assets/shaders/`) -- deliberately *not* bumped to `430 core`. A GL 4.3
  core context is required by spec to support every GLSL version from
  1.10 through 4.30, including 3.30, with fully compatible semantics for
  the language subset these shaders actually use (no shader here reads any
  4.3-only builtin, layout qualifier, or behavior that changed between
  3.30 and 4.30) -- confirmed empirically, not just by spec reading: all
  eleven `.vert`/`.frag` programs (`basic`, `shadow`, `skybox`,
  `postprocess`, `bloom_extract`, `blur`, `pbr`, `cubemap_capture`,
  `irradiance_convolution`, `prefilter`, `brdf_lut`) still compile and link
  cleanly under the new 4.3 context, and the rendered output is
  byte-identical (see below) to the pre-bump 3.3-context render. Bumping
  the `#version` string would add a chance of newly-strict validation
  behavior for zero functional benefit this phase -- there is nothing in
  this phase's own scope that needs a GLSL 4.30 feature (compute shaders
  use GLSL 430, but they're a new file Phase 13d adds, not an edit to any
  shader that exists today). Revisit this once Phase 13 actually adds a
  shader that needs 4.30-or-later GLSL features.
- **No visual regression**: `ENGINE_MAX_FRAMES=90 bash tools/run_headless.sh
  build/engine_app <out.png>` was run once against the unmodified (pre-Phase-12)
  build and once against the post-bump build; `compare -metric AE` between
  the two PNGs reports `0` -- zero differing pixels, not just "looks the
  same" -- confirming the entire existing scene (shadows, PBR direct
  lighting + IBL reflections on both the plain and Phase 11 textured
  spheres, bloom, MSAA-smoothed silhouettes, skybox, HDR/tonemap, and the
  Blinn-Phong table/box/pyramid) renders pixel-for-pixel identically before
  and after this version bump.

### Phase 13a: anisotropic texture filtering

Phase 13a fixes a specific visual artifact -- the ground plane's checker
texture shimmering/aliasing at grazing, near-horizon viewing angles -- that
trilinear mipmapping alone can't: trilinear blurs isotropically based on the
worst-case screen-space minification axis, over-blurring the other axis,
while anisotropic filtering samples more texels along the more-compressed
axis specifically.

- **`Texture`'s constructor** (`src/texture.cpp`) queries
  `GL_EXT_texture_filter_anisotropic` support exactly once per process
  (cached in a function-local static -- the answer can't change for the
  lifetime of a GL context) via the modern `glGetStringi`/`GL_NUM_EXTENSIONS`
  per-index query, not the old `glGetString(GL_EXTENSIONS)` single
  space-separated string -- a core-profile context (this engine has been on
  one since Phase 12) no longer supports that legacy string at all, so the
  old-style query would silently return null instead of the real answer.
  When supported, the driver's own reported maximum
  (`GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT`) is capped at a conventional 16x
  ceiling (some drivers report unusually high/exotic maxima; capping avoids
  blindly requesting whatever a given driver happens to claim) and applied
  via `glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, ...)`.
- Only meaningful alongside mipmaps (it refines minification filtering, which
  mipmaps drive), so it's gated on the same `generateMipmaps` flag as
  `GL_LINEAR_MIPMAP_LINEAR`. Applied uniformly in `Texture`'s own constructor
  rather than as a per-call-site opt-in, so every texture the engine creates
  -- ground plane, table/box/pyramid, PBR sphere materials -- gets it, all
  the way through `ResourceManager`'s cache.
- No visible-behavior toggle/env var: unlike later Phase 13 sub-phases (SSAO,
  SSR), there's no meaningful "before" image worth A/B-comparing at runtime
  here -- the driver either exposes the extension or it doesn't, and this
  container's Mesa/llvmpipe does.

Verified: clean rebuild is warning-free under `-Wall -Wextra`; the engine's
own startup log confirms the extension is detected and the applied level
(`Anisotropic filtering: GL_EXT_texture_filter_anisotropic supported (driver
max 16.000000x), applying 16.000000x to all textures`, this container's own
Mesa/llvmpipe build); a headless screenshot of the ground plane at a grazing
angle shows a crisp, stable checker pattern all the way to the horizon rather
than trilinear's characteristic moiré/shimmer.

### Phase 13b: frustum culling

Phase 13b adds one performance-architecture feature -- skipping draw calls
for geometry entirely outside the camera's view frustum -- without changing
what a normally-positioned camera renders. This engine's own test scene is
small enough that culling has no dramatic FPS impact today; the point is
building the mechanism correctly and generally (per-mesh bounding volumes +
a reusable frustum test) so it scales to a larger scene later, not a
special-cased hack for this one.

- **`Mesh::boundingSphere()`** (`mesh.hpp`/`mesh.cpp`) -- every `Mesh` now
  computes its own local-space bounding sphere once, at construction time,
  from its vertex positions: center is the axis-aligned bounding box's
  center (not a centroid, which would skew towards denser vertex clusters
  rather than the shape's actual middle), radius is the farthest any vertex
  actually sits from that center -- a correct (every vertex provably inside),
  if not minimal, enclosing sphere. `BoundingSphere::transformed(worldMatrix)`
  re-expresses it in world space: the center is transformed directly, and
  the radius is scaled by the *largest* of the transform's three axis scale
  factors (not an average) so a non-uniformly-scaled instance still gets a
  conservative, safe superset of its real extent -- under-culling (culling
  something that should be visible) is a correctness bug, over-culling
  slack (an occasional unculled-but-invisible object) merely costs one
  wasted draw call.
- **`engine::Frustum`** (`frustum.hpp`, header-only) -- extracts the 6
  view-frustum planes from a combined view-projection matrix via the
  standard Gribb/Hartmann method (each plane is a fixed row-sum/difference
  of the matrix's own rows -- see that header's `update()` comment for the
  derivation and the column-major-vs-row indexing pitfall it calls out by
  name). `intersects(center, radius)` is the conservative sphere-frustum
  test: true unless the sphere is provably entirely outside at least one
  plane. Verified against a standalone set of synthetic cases (a plain
  `lookAt`/`perspective` camera; a dead-center point, points off to each
  side/above/behind/beyond-far, and a large sphere whose edge alone reaches
  back into frustum) -- all behaved as expected, including left/right
  symmetry for a symmetric FOV.
- **Wired into `Application::render()`** -- one `Frustum` is built per frame
  from `camera_`'s current view/projection (never cached across frames,
  matching `Camera`'s own "no premature caching" style), then tested
  against every drawable this frame: each `Model` node's own mesh(es) (via
  new optional `frustum`/`cullStats` parameters on `Model::draw()`/
  `drawNode()` -- default `nullptr`, so every other call site, notably the
  shadow-depth pass, is unaffected), the hand-built ground plane, and every
  PBR sphere instance. Only the camera color pass is culled this way --
  `renderShadowPass()` still depth-draws everything unconditionally every
  frame, exactly as before (culling the shadow pass against the *light's*
  own frustum instead is a reasonable future refinement, not required for
  this phase's scope). A running `CullStats` total/culled count is logged
  every `kCullLogFrameInterval` (15) frames.
- **Proving it isn't a no-op**: `ENGINE_FRUSTUM_CULL_DEMO=1` (same
  getenv-gated pattern as `ENGINE_CAMERA_DEMO`) parks the camera at its
  usual default position but aimed 180 degrees away from the scene instead
  of at it. `ENGINE_MAX_FRAMES=90 ENGINE_FRUSTUM_CULL_DEMO=1 bash
  tools/run_headless.sh build/engine_app <out.png>` logs `Frustum culling:
  12/12 drawables culled this frame` on every logged frame (all 12 --
  the scene model's mesh, the ground plane, and the 8 PBR spheres -- fully
  outside the frustum), and the resulting screenshot shows nothing but the
  procedural-sky background, no scene geometry at all. A normal run (no env
  var) logs `Frustum culling: 0/12 drawables culled this frame` throughout,
  since this engine's whole small test scene fits inside the default
  camera's view.
- **No visual regression at the normal default camera pose**:
  `ENGINE_MAX_FRAMES=90 bash tools/run_headless.sh build/engine_app <out.png>`
  was run once against the pre-Phase-13b build and once against this
  phase's build; `compare -metric AE` between the two PNGs reports `0` --
  zero differing pixels, confirming the entire existing scene (shadows,
  PBR direct lighting + IBL reflections, bloom, MSAA-smoothed silhouettes,
  anisotropic-filtered textures, skybox, HDR/tonemap, and the Blinn-Phong
  table/box/pyramid/ground) still renders pixel-for-pixel identically --
  nothing visible is incorrectly culled at this camera's default framing.

### Phase 13c: Cascaded Shadow Maps

Phase 13c replaces Phase 7a/7b's single, fixed, whole-scene-covering
orthographic shadow projection with Cascaded Shadow Maps (CSM), without
changing the shadow-casting light itself (still just the one directional
light) or `render()`/`renderShadowPass()`'s overall shape.

- **`kCascadeCount` (3) separate `ShadowMap` instances** (`shadowCascades_`,
  a `std::array<ShadowMap, 3>` rather than a layered/array-texture mode added
  to `ShadowMap` itself -- see that class's own Phase 13c comment for why),
  each covering a different depth range ("cascade") of the camera's own view
  frustum. `renderShadowPass()` now depth-renders the whole scene once per
  cascade (3 total passes) instead of once total.
- **Split-depth scheme** (`computeCascadeSplitDepths()`): the standard
  "practical split scheme" (GPU Gems 3, ch. 10) -- a blend of a logarithmic
  split (equal *ratios* between consecutive split distances, matching how
  on-screen texel density falls off with distance under a perspective
  projection) and a uniform split (equal *differences*, avoiding the log
  scheme's tendency to make the nearest cascade too thin to be useful),
  blended evenly (`kCascadeSplitLambda = 0.5`, the commonly cited default).
  Splits are computed out to `kCascadeShadowDistance` (12 world units) --
  deliberately not the camera's real 100-unit far plane, since this scene's
  own shadow-relevant content all sits within roughly 8-9 units of
  `kDefaultCameraPosition`, so splitting across the full far plane would burn
  two of three cascades' resolution on empty space nothing ever casts a
  shadow into. The actual resulting split distances are logged once at
  startup (this container's own run: `0.100000 2.279954 5.233108 12.000000`).
- **Per-cascade frustum-fitting** (`computeCascades()`): for each cascade's
  depth range, unproject that range's 8 view-frustum corners to world space
  (`Camera::getProjectionMatrix()`'s near/far overload +
  `frustumCornersWorldSpace()`, see `frustum.hpp`), then fit a tight
  orthographic projection around their light-space bounding box (backed off
  `kCascadeLightBackoff` (20) units behind the slice's own center along
  `-lightDir`, padded by `kCascadeXYPadding`/`kCascadeZPadding` so an object
  just outside one cascade's tight slice still gets rendered into that
  cascade's own depth map) -- the standard CSM per-cascade frustum-fitting
  method. This is what gives CSM its resolution win over the old single
  whole-scene map: a cascade close to the camera covers a much smaller
  world-space area, so the same fixed `kShadowMapWidth`/`Height` texel budget
  lands a proportionally finer world-space grid over it.
- **`basic.frag`/`pbr.frag`** each pick which cascade a given fragment
  belongs to (comparing its view-space depth, a new varying, against
  `uCascadeSplits[]`) and sample that cascade's own shadow map with its own
  light-space matrix, blending smoothly across a small transition band near
  each split rather than a hard cutoff, to avoid a visible seam where shadow
  resolution/aliasing changes abruptly between cascades.

Verified: clean rebuild is warning-free under `-Wall -Wextra`; the startup
log confirms three cascades built with sane, monotonically increasing split
distances; a headless screenshot shows visibly sharper, better-defined
shadow edges close to the camera (the table/box/pyramid and PBR sphere grid's
own contact shadows) than the old single whole-scene map produced at the
same resolution, with no visible seam at either cascade boundary and no
regression to the existing shadow-casting/lit content.

### Phase 13d: GPU compute-shader clustered forward lighting

Phase 13d replaces `basic.frag`/`pbr.frag`'s brute-force "loop over every
point/spot light for every fragment, unconditionally" with the standard
clustered forward shading technique (Olsson & Assarsson; the same
logarithmic-Z-slicing scheme Doom (2016) and Angry Birds' clustered
renderers popularized) -- an architecture change for when this engine's
light count grows into dozens, not a fix for an actual bottleneck at this
scene's current handful of lights.

- **GLAD gained its first GL 4.3-specific entry points**
  (`external/glad/`): `glDispatchCompute`, `glMemoryBarrier`,
  `glBindBufferBase`, `glGetBufferSubData` (for the debug occupancy
  read-back only), plus the `GL_COMPUTE_SHADER`/`GL_SHADER_STORAGE_BUFFER`/
  `GL_SHADER_STORAGE_BARRIER_BIT` enums -- the context has requested GL 4.3
  core since Phase 12 specifically so this phase could add these once
  something actually called them.
- **`engine::ComputeShader`** (`compute_shader.hpp`/`.cpp`) -- a small RAII
  sibling to `Shader`: one `GL_COMPUTE_SHADER` stage instead of a linked
  vertex+fragment pair (a compute program is a fundamentally different kind
  of GL object, not an overload of `Shader`'s own one-vertex-one-fragment
  contract), same move-only/no-uniform-caching shape.
- **`engine::ClusterLightCuller`** (`cluster_light_culler.hpp`/`.cpp`) owns
  the whole technique: a **12x8x24 = 2304** cluster grid (12x8 screen tiles
  matching this engine's fixed 800x600/4:3 window; 24 logarithmic Z-slices,
  the same slice count Doom 2016's published grid uses) and two SSBOs --
  a per-cluster view-space AABB and a per-cluster **fixed-capacity light
  index list** (`pointCount` + up to `MAX_POINT_LIGHTS` (8) indices,
  `spotCount` + up to `MAX_SPOT_LIGHTS` (4) indices -- chosen over a
  compacted offset+flat-list scheme because with this engine's own light
  count topping out at 12 total, a fixed-size array is just as cheap and
  categorically simpler to get right: no atomic-counter compaction pass to
  review for races). Each light's actual color/attenuation/cone data stays
  exactly where it always lived, `basic.frag`/`pbr.frag`'s own
  `uPointLights[]`/`uSpotLights[]` uniform arrays -- clustering only changes
  *which* and *how many* of those entries a fragment loops over.
  - `computeClusterAABBs()` (`cluster_aabb.comp`, `local_size = 4x4x4`,
    dispatched as `3x2x6` groups) builds every cluster's AABB from the
    projection matrix alone (screen-tile corners unprojected via the
    inverse projection, intersected against each cluster's near/far
    Z-plane -- the standard line-through-origin trick). Run **exactly
    once**, in the constructor -- a cluster's view-space AABB is a pure
    function of the projection matrix and window size, neither of which
    this engine ever changes after startup, so it never needs
    recomputing.
  - `cullLights()` (`cluster_cull.comp`, `local_size_x = 256`, dispatched
    as 9 groups) sphere-vs-AABB tests every light (view-space position,
    computed on the CPU each frame from `view * worldPosition`; effective
    radius from LearnOpenGL's color-and-attenuation-aware "light volume"
    cutoff -- deliberately generous, since an over-generous radius only
    costs a few wasted tests while a too-small one would silently drop a
    light from a cluster it still visibly lights) against every cluster,
    writing the surviving indices. Run **every frame** -- unlike the AABBs,
    this depends on the *view* matrix, which changes whenever the camera
    moves (this engine's own lights are static world-space constants and
    never move, but the camera does, in both free-fly and the scripted
    demo path, so re-culling every frame is the actually-correct choice,
    not a conservative default).
  - Both dispatches are followed by `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)`
    before anything reads their SSBO writes (the light-culling dispatch
    itself, and every subsequent fragment-shader draw call this frame,
    respectively).
- **`basic.frag`/`pbr.frag`** each compute which cluster the current
  fragment falls into (`computeClusterIndex()`: `gl_FragCoord.xy` for the
  screen tile, `vViewSpaceDepth` -- already computed for CSM's own cascade
  selection -- through the identical logarithmic-Z formula the AABB compute
  shader solved in the opposite direction) and loop only over that
  cluster's own light list (`layout(std430, binding = 1) readonly buffer`)
  instead of every light unconditionally. Bumped from `#version 330 core`
  to `430 core` (both `.vert` and `.frag` of each pair) -- SSBOs require
  GLSL 4.30.
- **`ENGINE_CLUSTER_DEBUG=1`** (same getenv-gated pattern as
  `ENGINE_CAMERA_DEMO`/`ENGINE_FRUSTUM_CULL_DEMO`) blends a heat-map tint
  (red = more lights, blue = fewer) keyed by each fragment's own cluster's
  light count into the lit color -- visible proof the per-cluster lists
  vary spatially, not just that this code compiles.
- **Proving it isn't a no-op**: a periodic log line (same
  `kCullLogFrameInterval` cadence as Phase 13b's frustum-culling summary,
  reading the light-list SSBO back via `glGetBufferSubData` -- deliberately
  not every frame, to avoid a GPU->CPU stall on the hot path) reports e.g.
  `Clustered lighting: 2136/2304 clusters occupied, avg 3.565543
  lights/occupied cluster` -- not all 2304 (some clusters, e.g. ones behind
  every light's culling sphere, are correctly empty) and not a flat "every
  cluster sees every light" either.
- **No visual regression**: `ENGINE_MAX_FRAMES=90 bash tools/run_headless.sh
  build/engine_app <out.png>` was run once against the pre-Phase-13d build
  and once against this phase's build; `compare -metric AE`/`RMSE` between
  the two PNGs both report `0`, and the two PNGs are byte-identical
  (matching MD5 sums) -- every light that reached a surface before
  clustering still reaches it after, confirming clustering is a pure
  optimization/architecture change with zero effect on the lit result.
  Verified with a `Debug` build (`GL_CHECK` draining `glGetError()` after
  every call, including the new compute dispatch/SSBO/barrier calls) --
  zero GL errors across a normal run, `ENGINE_CLUSTER_DEBUG=1`,
  `ENGINE_CAMERA_DEMO=1`, and `ENGINE_FRUSTUM_CULL_DEMO=1`.

### Phase 13e: a real HDRI environment map

Phase 13e replaces Phase 7b/10's flat, procedurally-gradient-only skybox
with a real HDRI (High Dynamic Range Image) environment -- an
equirectangular Radiance `.hdr` file carrying genuine floating-point
radiance values well above 1.0 (a small, extremely bright sun disk), which
now feeds both the visible sky background and the existing Phase 10 IBL
convolution pipeline unchanged. No network fetch is used anywhere in this
phase (this project's sandbox has no general internet access, and the
existing convention for every other texture -- `checker.png`,
`normal_bump.png`, the old skybox faces, the Phase 11 rusted-metal/
scuffed-plastic materials -- is to procedurally generate assets offline
rather than source them) -- the HDRI is generated entirely by
`tools/generate_hdri.py` (numpy), and written out as a real, valid Radiance
RGBE file by a from-scratch Python port of `stb_image_write.h`'s own
`stbi_write_hdr_core()` encoder (traced by reading that vendored-by-GLFW
header directly, then verified by round-tripping the written file back
through this project's own vendored `external/stb/stb_image.h`
(`stbi_loadf()`) in a small standalone test program -- confirming
byte-for-byte-format compatibility before ever wiring it into the engine).

- **`tools/generate_hdri.py`** builds a 1024x512 (2:1) equirectangular sky:
  a color-temperature gradient (deep blue at the zenith, warm/bright at the
  horizon, dim at the "ground" below it -- `smoothstep`-blended, not a hard
  cutoff) plus a small sun disk (a sharp ~1.1-degree-radius core at
  radiance ~200+, `sun_core_intensity = 220.0` before blending -- far above
  1.0, the whole reason HDR exists, since an 8-bit image can't represent it
  at all) with a softer ~14-degree glow/corona around it. The sun's
  (elevation, azimuth) is deliberately chosen (4 degrees, 230 degrees) so it
  actually falls inside the engine's default camera's visible frustum
  (computed from `kDefaultCameraPosition`/`kSceneCenter` in
  `application.cpp`) rather than passing overhead or behind the camera,
  unverifiable by a screenshot. The script asserts the left/right image
  seam (azimuth wraps at u=0/1) matches to within floating-point precision
  before ever writing the file, and logs the sun's resolved world direction
  plus zenith/horizon sample colors for independent review.
- **Direction <-> equirectangular UV convention** (`equirect_to_cubemap.frag`,
  matched exactly by the generation script's own row/column -> direction
  derivation -- see both files' header comments for the full derivation):
  `u = atan2(dir.z, dir.x) / (2*pi) + 0.5` (azimuth, wraps), `v = asin(dir.y)
  / pi + 0.5` (elevation; `v=1` at straight up). This is the standard
  LearnOpenGL/Karis "spherical environment map" formula, chosen (over the
  equally common `v = acos(dir.y)/pi` form) specifically because it composes
  directly with `engine::Texture`'s existing
  `stbi_set_flip_vertically_on_load(true)` convention that
  `loadHdrEquirectangularAsCubemap()` reuses rather than inventing a second
  image-loading convention for one asset -- a wrong sign/axis choice here
  would silently render a rotated/mirrored/pole-swapped sky, not fail to
  compile, which is why this was verified by direct calculation (see below),
  not just visual eyeballing.
- **`engine::loadHdrEquirectangularAsCubemap()`** (`hdri_loader.hpp`/`.cpp`)
  -- loads the equirectangular source via `stbi_loadf()` (this project's
  vendored `stb_image.h` already supports HDR loading, see its own "HDR
  image support" section -- no new vendored dependency needed) into a
  floating-point `GL_TEXTURE_2D` (`GL_RGBA16F`, same "no `GL_RGB16F` token in
  this project's hand-pruned glad build" reason `ibl_probe.cpp` already
  documents), then GPU-renders it into a 512x512-per-face floating-point
  `GL_TEXTURE_CUBE_MAP` via a new one-time pass pairing the existing
  `cubemap_capture.vert` (reused as-is from Phase 10) with the new
  `equirect_to_cubemap.frag` -- the same "temporary FBO, built and torn down
  within one function" shape `IBLProbe`'s own constructor already uses for
  its convolution passes. A free function, not a class: unlike
  `Skybox`/`IBLProbe`, nothing here needs to persist past the conversion --
  only the finished cubemap texture name escapes.
- **`Skybox` gained a second constructor** (`explicit Skybox(unsigned int
  existingCubemapTextureId)`) that takes ownership of an already-built GL
  cubemap instead of loading 6 PNG faces itself -- distinguishable from the
  original constructor by parameter type, so both stay available. Nothing
  else about `Skybox` changes: `draw()`, `textureId()`, and move semantics
  are identical regardless of which constructor built the texture it holds.
- **`Application::buildSkybox()`** (`application.cpp`) picks between the two
  based on `ENGINE_USE_PROCEDURAL_SKYBOX` (same getenv-gated pattern as
  `ENGINE_CAMERA_DEMO`/`ENGINE_FRUSTUM_CULL_DEMO`/`ENGINE_CLUSTER_DEBUG`) --
  unset (the default) builds from the new HDRI; set, non-zero uses the
  Phase 7b/10 procedural 6-face cubemap, kept as a fallback/reference rather
  than deleted (this project's established convention -- e.g.
  `Material`/`basic.frag` stayed untouched alongside `PBRMaterial`/`pbr.frag`
  since Phase 9). `IBLProbe`'s own constructor and `basic.frag`/`pbr.frag`'s
  own IBL sampling are completely unmodified -- both already only ever
  consumed `skybox_.textureId()`, so a different (and, unlike the old
  procedural cubemap, genuinely floating-point/HDR) source feeding that same
  handle is the entire change. The background draw's HDR values flow into
  `hdrFramebuffer_` exactly like the old procedural skybox's always did, so
  they pass through the same Reinhard tonemap in the final postprocess pass
  -- no separate/second tonemap step was ever needed or added.
- **Verifying the direction/UV convention, not just eyeballing it**: the
  sun's exact expected screen position was independently computed (Python,
  replicating `glm::lookAt`/`glm::perspective` by hand from
  `kDefaultCameraPosition`/`kSceneCenter`/the camera's 60-degree vertical
  FOV) as pixel (373, 24) in the 800x600 headless screenshot; the actual
  rendered sun disk appears there. A deliberately oversized "debug" sun
  (20/60-degree core/glow radii) was rendered first to unambiguously confirm
  the whole pipeline (generation -> RGBE round-trip -> GPU conversion ->
  skybox draw) placed the sun in the geometrically-predicted region before
  dialing the size back down to the final, realistic value -- isolating "is
  the math right" from "is the feature simply too small to see casually" as
  two separately-answered questions.
- **No seam/pole artifacts**: (1) the generation script's own left/right
  azimuth-wrap seam is verified to match exactly (`max abs diff: 0.0`)
  before the file is ever written; (2) both pole rows (zenith, `dir.y=+1`,
  and nadir, `dir.y=-1`) were independently decoded from the written `.hdr`
  file (a from-scratch Python RGBE reader, separate code from the writer
  above) and confirmed to hold the exact same color across every column --
  every azimuth converging on the same physical point, as it must; (3) the
  actual rendered headless screenshot's background gradient was scanned for
  any abrupt per-pixel jump (several scanlines' adjacent-pixel differences
  all stayed under ~10/765 combined-channel units, consistent with a smooth
  gradient/glow falloff, not a hard seam) -- with a real HDRI sun/gradient in
  frame, `ENGINE_MAX_FRAMES=90 bash tools/run_headless.sh build/engine_app
  <out.png>` shows no visible cube-face boundary anywhere the camera can see.
- **No regressions**: with `ENGINE_USE_PROCEDURAL_SKYBOX=1`, this phase's
  build was compared against a byte-for-byte-identical build of the
  pre-Phase-13e commit (via a separate `git worktree`) using the same
  headless harness/camera pose -- `compare -metric AE` reports `0` and the
  two PNGs' MD5 sums match exactly, confirming every other system (CSM
  shadow cascades, PBR/IBL direct-lighting math, clustered lighting, bloom,
  MSAA, anisotropic filtering, frustum culling, the Blinn-Phong scene) is
  completely unaffected by this phase's changes when the old procedural path
  is selected. Clean `rm -rf build && cmake -B build -S . && cmake --build
  build` succeeds with zero warnings under `-Wall -Wextra`.

### Phase 13f: Screen-Space Ambient Occlusion (SSAO)

Phase 13f adds the classic Crysis/John Chapman hemisphere-kernel SSAO
technique, multiplied into both `basic.frag`'s and `pbr.frag`'s existing
ambient term alongside their material-authored AO.

- `Framebuffer` gains an optional `depthAsTexture` attachment so a target's
  depth can be sampled (not just written) -- used by SSAO's own lightweight
  geometry pre-pass (`gbuffer.vert`/`.frag`: view-space normal + real depth
  texture), reconstructing view-space position from that depth + the inverse
  projection matrix rather than a second position render target.
- `ssao.frag`: a 32-sample tangent-space hemisphere kernel (biased toward the
  origin), rotated per-pixel by a tiled 4x4 noise texture
  (`engine::SSAOKernel`, fixed-seed for reproducible headless screenshots),
  with a range check to avoid halos from unrelated distant geometry.
- `ssao_blur.frag`: a small box blur (radius = noise tile size / 2) removes
  the kernel pass's per-pixel noise.
- All three SSAO targets run at half the window's resolution
  (`kSSAODownsampleFactor`) -- a pure performance tradeoff for this project's
  software-rasterizer headless verification target, upsampled back via
  ordinary `GL_LINEAR` filtering with no visible quality loss.
- Applied to both `basic.frag` and `pbr.frag` (not left PBR-only): SSAO is a
  purely screen-space effect independent of which BRDF produced a pixel's
  color.
- `ENGINE_SSAO_DISABLE` forces the ambient SSAO factor to 1.0 (verified
  pixel-identical to the pre-Phase-13f render); `ENGINE_SSAO_DEBUG` shows the
  raw pre-blur occlusion buffer directly for isolated inspection.

Verified: clean rebuild is warning-free under `-Wall -Wextra`; a headless run
shows subtle, localized darkening at sphere/box/ground contact points with no
halos or banding, zero difference in open floor areas, and a pixel-perfect
match to the pre-SSAO render with `ENGINE_SSAO_DISABLE=1`.

### Phase 13g: Screen-Space Reflections (SSR)

Phase 13g refines `pbr.frag`'s IBL-only specular term (Phase 10's
prefiltered-environment-cubemap reflection) with a real screen-space
ray-marched reflection of the actual nearby scene, for the PBR sphere grid's
smoothest/most metallic spheres -- IBL alone can only ever reflect the
distant skybox/HDRI, never another nearby object or the ground plane, which
SSR now can.

- Reuses SSAO's existing geometry pre-pass (`ssaoGBuffer_`'s view-space
  normal + real depth texture) as the ray march's "what does the scene
  actually look like" input, rather than building a second, redundant
  G-buffer.
- Solves the classic "can't read the color buffer you're still writing"
  ordering problem with a second, short compositing pass
  (`Application::renderSSRComposite()`): the ordinary per-frame draw of the
  whole scene runs unchanged (`uSSREnabled` off, exactly Phase 10's IBL-only
  behavior), then -- once the resolved opaque scene color
  (`hdrResolveFramebuffer_`) is available -- the PBR sphere grid alone is
  redrawn a second time (`GL_LEQUAL` depth test against the same first-pass
  depth values) with the ray march enabled, blending a real local reflection
  into the specular term where one is found.
- Fixed 28-step linear view-space march + a 4-step binary-search refinement
  (tuned down from the technique's usual 48-64-step reference range -- this
  project's software-rasterizer headless verification target makes every
  dependent-texture-fetch loop iteration costly, the same reasoning
  `kSSAOKernelSize`'s own comment already applies at 32 samples). Fades the
  SSR contribution back to the existing IBL term at screen edges, grazing
  view angles, and high roughness.
- `ENGINE_SSR_DISABLE` forces the ordinary IBL-only path unconditionally, for
  isolating SSR's own contribution in headless before/after verification,
  same pattern as `ENGINE_SSAO_DISABLE`.

This phase's own verification, and three rounds of independent re-review
after it, found four bugs in a row in `traceSSR()`'s ray march -- each fix
below is the one that shipped at the time, written up as it was found, with
the next review's own re-derivation of the math sometimes overturning a
previous fix's own reasoning (not just its code) once a wider screenshot or
a closer look at the algebra showed the earlier fix's justification didn't
actually hold up. Read in order, they trace a single ray march from
"visibly broken" to "correct":

1. **Reflection aliasing**, not self-intersection, produced a mottled dark
   patch on one sphere.
2. Fixing #1 left a second, smaller **false-hit "island"** on the same
   sphere, traced to the SSR ray march reusing SSAO's *half-resolution*
   depth buffer.
3. Fixing #2's false hit **broke almost every legitimate reflection in the
   scene** -- the thickness gate #2 tightened was, on closer derivation,
   gating the wrong quantity for every ray, not just the false-hit case.
4. Fixing #3 introduced a **swirling/donut-shaped distortion** on several
   spheres -- #3's refactor had silently deleted the coarse march's own
   crossing test along the way, a regression invisible in #3's own
   single-sphere before/after diff but obvious on a full 8-sphere
   screenshot.

- **Bug review (#1): reflections of the checkerboard floor
  aliased into a mottled/checkered dark patch, not a self-intersection.**
  A headless screenshot's top-right sphere showed a noisy, blotchy dark patch
  between its specular highlight and terminator that didn't belong -- gone
  entirely with `ENGINE_SSR_DISABLE=1`, so clearly SSR's own doing. The
  initial suspicion was the classic SSR self-intersection artifact (the ray
  re-striking its own sphere), and a targeted fix for that (excluding hits
  within the source sphere's own radius of its center) was built and
  measured -- but it changed nothing visible at any reasonable exclusion
  radius, which was the tell that the real cause was elsewhere. A debug build
  that visualized `traceSSR()`'s raw hit UV directly showed a smoothly-varying,
  perfectly legitimate hit location in the mottled region; decoding those UVs
  back to screen pixels landed squarely on the reflected floor's own
  alternating blue/tan checker squares. The actual mechanism: reflection off
  a convex mirror doubles angular sensitivity relative to the surface itself,
  so near this sphere's grazing/silhouette angles, `hitUV` shifts by many
  screen pixels between adjacent fragments even though the surface normal
  barely changes. Sampling a single mip-0 texel per fragment against that
  rapidly-shifting UV (the bug's original form) undersampled the floor's
  sharp, high-frequency squares -- neighboring fragments landing on
  differently-colored squares reads as noisy/checkered, not a clean
  gradient -- the textbook reflection-aliasing failure mode, distinct from
  (and easily mistaken for) self-intersection. (Also independently confirmed
  self-intersection is geometrically impossible here in the first place: a
  sphere is strictly convex, so `reflect()`'s own construction guarantees the
  marched ray immediately enters the *outward* half-space of whatever point
  it leaves, and a convex surface's own supporting-hyperplane property means
  it can never re-enter that same half-space again.) Fixed the same way
  ordinary texture minification is fixed: `hdrResolveFramebuffer_` now
  carries a real mip chain (`Framebuffer`'s new `mipmappedColor` constructor
  flag + `generateColorMipmaps()`, called once every frame right after this
  buffer is resolved, before SSR reads it), and `pbr.frag` samples it with
  `textureGrad(uSSRColorBuffer, hitUV, dFdx(hitUV), dFdy(hitUV))` instead of a
  plain `texture()` call -- letting GL average over the actual texel
  footprint each fragment's reflection spans instead of a single point
  sample. Verified: the mottled patch is gone (replaced by a clean, smoothly
  darkening gradient) while the checkerboard reflection on the grid's other,
  less grazing spheres stays exactly as sharp as before (mip 0 is still what
  a near-zero derivative selects); `ENGINE_SSR_DISABLE=1` renders within
  single-digit-of-255 rounding noise of the pre-fix disabled render (RMSE
  ~2.4/255, well below visibility) -- mipmapping this buffer doesn't perturb
  the non-SSR path it's also read from. Clean rebuild is warning-free under
  `-Wall -Wextra`, and a Debug build's `GL_CHECK` drains zero GL errors across
  a full headless run.

- **Bug found post-verification (#2): an isolated false-hit blob survived
  the aliasing fix above.** A pixel-level before/after comparison of the
  mottled-patch fix (an ASCII luminance map of the same top-right sphere,
  not just an eyeballed screenshot) turned up a second, different defect the
  first fix never touched: a small, disconnected dark "island" sitting
  between the sphere's specular highlight and its shadow terminator,
  disjoint from the smooth shadow gradient around it -- present, pixel-for-
  pixel identical, in both the pre- and post-aliasing-fix screenshots, and
  completely absent with `ENGINE_SSR_DISABLE=1` in both. A debug build that
  colored each accepted `traceSSR()` hit by its own coarse-march step index
  (how far along the ray the crossing was found, alongside the existing
  raw-hitUV visualization the first bug's own investigation used) showed
  the mechanism directly: fragments just outside the island correctly found
  their hit late in the march (out on the reflected floor, matching their
  neighbors' smoothly-varying hitUV), while fragments inside it found a
  "hit" only 4-5 steps in -- a plateau of suspiciously early, near-identical
  hitUVs breaking up what should have been one continuous ramp. Decoding
  those early hitUVs back to screen pixels landed on an entirely ordinary
  patch of the same checkerboard floor -- a real surface, just reached far
  too early, and not the sphere itself (self-intersection is still
  geometrically impossible off a convex surface, as the first bug's own
  investigation already established). Root cause: `uSSRDepthMap` is SSAO's
  existing G-buffer depth, rendered at half the window's resolution purely
  for SSAO's own affordability (`kSSAODownsampleFactor`) and reused here
  specifically to avoid a second full-resolution geometry pre-pass (see this
  phase's own header comment) -- but that buffer is a *separate*
  rasterization of the same triangles at coarser pixel centers, not a
  downsampled copy of the full-res one the ray march otherwise reasons
  about, so right where depth changes fast across screen space (exactly the
  grazing/silhouette angles the first bug's convex-mirror angular
  amplification already made sensitive) it can legitimately disagree with
  the true surface by a small amount. `kSSRThickness`'s original 2-step
  margin was generous enough to accept that small disagreement as "still
  the surface" during an early march step -- where a fixed view-space margin
  covers a much larger fraction of the (so-far-tiny) distance traveled than
  the same margin does later on -- producing a too-early, wrong hit for
  whichever fragments landed squarely on the low-res buffer's own
  disagreement, while immediate neighbors narrowly avoided it and correctly
  kept marching to the true, far intersection everyone should share. A
  neighbor-texel depth-discontinuity check was tried first (rejecting a hit
  whose surrounding `uSSRDepthMap` texels disagreed sharply -- the standard
  false-hit-at-a-silhouette-edge guard) and measured to have *zero* effect,
  confirming this wasn't an edge at all, just an otherwise-smooth surface
  sampled at a slightly wrong position. Tightening `kSSRThickness` itself
  (from 2 steps' worth down to 0.1 steps' worth) is what actually closes the
  gap, verified by sweeping the margin (2.0/1.0/0.5/0.2/0.15/0.1/0.05 steps)
  and measuring the island's own region against the `ENGINE_SSR_DISABLE=1`
  render at each step: it converges to within single-digit-of-255 luminance
  by 0.1 and does not measurably improve further below that. Verified: the
  island is gone -- the same region now matches
  `ENGINE_SSR_DISABLE=1`'s smooth gradient there pixel-for-pixel in the
  ASCII luminance map, and within single-digit-of-255 RMSE numerically --
  while a full-frame diff against the pre-this-fix render shows the change
  confined to the sphere grid (as expected for an SSR-only fix) with the
  checkerboard reflections on the grid's other spheres visually unchanged
  (`kSSRRefineSteps`'s binary search, run immediately after any coarse
  accept, is what actually pins a legitimate hit down to sub-step precision
  regardless of how tight this gate is). Clean rebuild is warning-free under
  `-Wall -Wextra`.

- **Bug found in independent re-review (#3): the #2 fix above broke almost
  every legitimate SSR reflection in the scene, not just the false one it
  targeted.** Re-deriving `traceSSR()`'s math by hand (rather than trusting
  the #2 fix's own verification prose) showed `kSSRThickness` gates
  `sceneViewZ - currPos.z` -- the gap between the ray and the recorded
  surface *at whichever coarse step first lands behind it* -- and that gap
  has nothing to do with how "thick" the true surface is; it's simply how
  far past the true crossing that one ~0.107-unit step happened to overshoot,
  which for any ray traveling mostly along view-space Z (i.e. most
  reflections that aren't near-grazing) is routinely several times #2's own
  ~0.0107-unit margin, even against a perfectly ordinary, flat, correctly-
  sampled surface. Confirmed empirically with a debug build that colored
  every `traceSSR()` call site green (hit) or magenta (no hit): every sphere
  smooth enough for `roughnessFade`/`grazingFade` to even attempt a march
  came back almost solid magenta, reflecting only a thin ring right at the
  silhouette rather than the sharp checkerboard/sphere reflections a working
  SSR pass should show across most of each sphere's face. This also explains
  why #2's own `ENGINE_SSR_DISABLE=1` diff looked clean: at 0.1 steps'
  worth, SSR was already contributing almost nothing anywhere on the grid
  except that same thin silhouette ring, so a "no visible loss elsewhere"
  full-frame diff was never a meaningful check -- there was barely anything
  left to lose. The fix: keep the exact same `kSSRThickness` constant, but
  gate it against the *refined* (post-bisection) gap instead of the coarse
  one -- `kSSRRefineSteps`'s binary search already narrows the bracket
  toward wherever the crossing actually flips, and for an ordinary,
  consistently-sampled surface that drives the residual gap toward zero
  regardless of how much the initial coarse step overshot, so a tight margin
  now correctly accepts it. The #2 false-hit case remains correctly rejected:
  it comes from `uSSRDepthMap` disagreeing with the true surface over a
  discrete, roughly constant-within-one-low-res-texel span, so bisecting
  within that span keeps re-reading essentially the same wrong recorded
  depth while the ray position barely moves -- the residual gap does not
  collapse toward zero the way a real surface's does, and the tight
  post-refinement margin still rejects it. Verified with the same green/
  magenta hit-map (now predominantly green, matching a working SSR pass
  across the ground plane and every smooth sphere) and by re-running #2's
  own before/after luminance comparison on its previously-broken sphere
  (unchanged, confirming that fix still holds). `ENGINE_SSR_DISABLE=1`
  headless runs are unaffected (the disabled path never calls `traceSSR()`
  at all), and a Debug build's `GL_CHECK` drains zero GL errors across a
  full headless run with the fix in place.

- **Bug found in independent re-review (#4): the #3 fix above introduced a
  new, more visible swirling/donut-shaped reflection distortion on 4 of the
  8 spheres.** A full headless screenshot of the whole sphere grid (not just
  the one sphere each of #2/#3's own investigations had separately zoomed
  in on) showed every sphere smooth/metallic enough that a wrong specular
  term isn't masked by diffuse or `roughnessFade` displaying a bizarre
  spiraling, warped copy of the checkerboard floor centered on the sphere --
  nothing like a reflection, and confirmed SSR-specific by
  `ENGINE_SSR_DISABLE=1` rendering all 8 spheres cleanly. A debug build
  extending the raw-hitUV visualization #1/#2 already established (adding an
  out-parameter for the coarse crossing's own step index and view-space
  position) showed nearly *every* fragment on *every* sphere -- not just the
  4 visibly-swirling ones -- crossed at coarse step 1 or 2, with `hitUV`
  varying wildly and non-monotonically (a visible vortex in the raw hitUV
  colors themselves) exactly where the swirl shows up in the final shaded
  image. That's far too early to be this scene's real geometry
  (`kSphereRadius` = 0.14, `kSphereColSpacing` = 0.6 -- a genuine reflection
  target is essentially never found within the first couple of ~0.107-unit
  steps off a non-grazing sphere surface). The actual cause turned out to be
  hiding in the coarse march loop itself, not in `kSSRThickness`: #3's
  refactor (splitting "detect a crossing" from "judge a crossing," so
  `kSSRThickness` could move to the refined gap) accidentally deleted the
  crossing test itself along the way -- the loop's own comment still
  described "the ray has intersected real geometry once it goes behind...
  the actual surface stored there," but the code below it no longer checked
  that at all, unconditionally treating the *first* screen pixel with any
  non-background depth as a crossing whether or not the ray had actually
  gone behind it yet. Since a reflection ray's very first ~0.107-unit step
  off a sphere's surface almost always still projects to a screen pixel
  showing *some* real geometry (that same sphere's own silhouette, the floor
  right underneath it), this fired immediately for nearly every fragment;
  the subsequent bisection then refined between the ray's own origin and
  that first, essentially-arbitrary step -- a bracket so close to the
  reflecting fragment's own surface that it converged to a small, stable
  residual gap almost every time (passing `kSSRThickness`'s tight
  post-refinement margin exactly as #3 intended it to for a *real* surface),
  confidently accepting a hitUV that was really just wherever that first
  short, near-tangent step happened to land -- hypersensitive to the exact
  reflect direction per #1's own convex-mirror angular-amplification
  finding, which is what turns a smooth sweep across a sphere's face into a
  swirl. All 8 spheres ran through this same broken march equally; only 4
  showed it visually because the other 4 sit at a metallic/roughness
  combination where `roughnessFade` was already small or the diffuse term
  (`kD`) large enough to keep dominating the final blend regardless -- the
  bug was never confined to 4 spheres, only its visibility was. The fix:
  restore the missing `currPos.z <= sceneViewZ` crossing test in the coarse
  loop (so a step that's still in front of whatever's at its screen pixel
  goes back to being marched past, rather than accepted outright), while
  keeping #3's own still-correct insight of judging `kSSRThickness` against
  the refined gap rather than the coarse one. Verified with the same
  raw-hitUV debug build (now a smooth, monotonic gradient across every
  sphere's face, no vortex on any of the 8) and a full 8-sphere headless
  screenshot, cropped per-sphere: all 4 previously-swirling spheres are
  clean, the other 4 and #2's own previously-fixed sphere are unchanged, and
  `ENGINE_SSR_DISABLE=1` renders pixel-identical (RMSE 0) to the pre-fix
  disabled render. A Debug build's `GL_CHECK` drains zero GL errors across a
  full headless run with the fix in place.

With #4's fix in place, `traceSSR()`'s coarse march once again does what its
own comment always said it did (reject a step still in front of the
recorded surface, accept one that's gone behind it), judged against the
refined post-bisection gap (#3's insight, replacing the coarse-gap check #2
tightened) and sampling `uSSRColorBuffer` through its own mipmapped,
`textureGrad`-sampled path (#1) -- still reading `uSSRDepthMap` from SSAO's
existing half-resolution depth texture throughout (that was never the bug;
#2 traced the false hit to how tight a margin that lower-resolution buffer's
own small disagreements with the true surface needed, not to its resolution
itself).

- **Bug review #5 (cross-phase, found during the Phase 9-13g final review):
  bloom silently read a stale, pre-SSR mip chain.** #1's fix above gave
  `hdrResolveFramebuffer_` a real mip chain and rebuilt it
  (`generateColorMipmaps()`) once every frame, right after that buffer's
  first `resolveTo()` -- but only *once*: `renderSSRComposite()`'s own
  second `resolveTo()` a few lines later (needed so `hdrResolveFramebuffer_`
  reflects SSR's blended output, not just the pre-SSR first pass) only
  overwrites that texture's base (mip 0) level -- `glBlitFramebuffer` never
  touches anything past it -- leaving every mip above 0 holding whatever the
  *first* `generateColorMipmaps()` call built, i.e. this frame's pre-SSR
  image. Phase 11's bloom-extraction pass reads this same texture next, and
  is exactly the kind of read that can select a non-zero mip on its own:
  its target (`brightFramebuffer_`) is half `hdrResolveFramebuffer_`'s
  resolution (`kBloomDownsampleFactor`), so the fullscreen quad's own
  screen-space UV derivatives drive GL's implicit LOD selection above 0 --
  confirmed directly by temporarily stomping mip 1 with solid magenta and
  rendering with `ENGINE_SSR_DISABLE=1` (so nothing else touches the chain
  afterward): the *entire* bloomed image came back tinted magenta, proving
  bloom's downsample does sample non-zero mips in practice, not just in
  theory. The upshot: a bright SSR reflection (a smooth sphere mirroring a
  point light or a bright patch of environment) could bloom at its dimmer
  pre-SSR brightness, or fail to cross `kBloomThreshold` at all, even though
  the final tonemap pass right after (which samples mip 0 only, at 1:1
  resolution, so never triggers implicit LOD selection) shows the correct,
  post-SSR pixel. This project's own current sphere-grid/camera framing
  doesn't happen to put a strong enough SSR-only highlight in view to make
  the difference visible by eye (a before/after diff of the actual scene
  came back within 1/255 rounding noise) -- but the invariant it violates
  (every consumer of `hdrResolveFramebuffer_` should see the same finished,
  post-SSR image) is real and scene-independent, exactly the kind of latent
  bug a different camera angle or light placement would expose. Fixed by
  calling `generateColorMipmaps()` a second time, right after
  `renderSSRComposite()`'s own second `resolveTo()`, so the mip chain bloom
  reads is rebuilt from the same post-SSR base level the tonemap pass
  already sees. Verified: clean rebuild, zero warnings under `-Wall
  -Wextra`; headless runs (default, every individual `ENGINE_*` env var
  above, and a few combinations) all complete without a GL error and render
  pixel-identical to the pre-fix build in every mode that doesn't touch this
  code path (`ENGINE_SSR_DISABLE=1` and friends), with only sub-1/255
  rounding differences in the default (SSR-enabled) render.

### Phase 8a: a real component-based ECS

Phase 8a is a structural refactor, not a new rendering feature: nothing about
*what* gets rendered changes (same camera, same lighting, same `scene.obj`,
same PBR sphere grid, same ground plane), only *how the scene's entities are
represented* changes. It promotes Phase 6's `Entity` (`entity.hpp`'s own
header comment said explicitly that "a real ECS could replace it wholesale in
a later phase") into an actual, if deliberately small, component-based entity
system -- see `include/engine/ecs.hpp` for the full design writeup this
section summarizes.

- **The old design**: `Entity` was one fixed struct -- a `Transform` plus an
  optional `shared_ptr<Model>` -- and `Application::entities_` was a
  `std::vector<Entity>`. Adding any new kind of per-entity data (say, a
  future physics body) would have meant adding a third hardcoded field to
  `Entity` itself, growing that one struct indefinitely as new sub-phases
  (8b-8e) add new kinds of per-entity data.
- **The new design** (`include/engine/ecs.hpp`, header-only):
  - **`EntityId`** -- an opaque handle: nothing but a `std::uint32_t` index.
    Entities own no data of their own; everything about an entity lives in
    whichever component pools happen to have an entry for its id.
    Deliberately no generation/recycling counter -- this engine only ever
    *creates* entities (nothing calls a "destroy entity" that would free an
    index for reuse today), so the classic ECS "stale handle into a
    recycled index" hazard can't happen yet. A later phase that adds real
    entity destruction is the right place to add a generation field.
  - **`ComponentPool<T>`** -- one component type's storage: a dense
    `std::vector<T>` parallel to a `std::vector<EntityId>` of owners, plus a
    sparse entity-index -> dense-slot map for O(1) `add()`/`get()`/`has()`/
    `remove()` (the standard swap-and-pop sparse-set removal, so iterating
    and mutating both stay cheap as entities come and go, not just while
    there's exactly one).
  - **`EntityRegistry`** -- owns every component pool, type-erased
    (`std::shared_ptr<void>` keyed by `std::type_index`) so a brand new
    component type never requires touching `EntityRegistry` itself, just a
    call to `addComponent<NewType>(id, ...)`. `create()` allocates an id;
    `addComponent`/`getComponent`/`removeComponent`/`hasComponent<T>()` are
    the per-component-type API; `each<T>(fn)` calls `fn(EntityId, T&)` once
    per entity that has a `T`, which is how every render()-side call site
    below iterates.
  - **`Transform` reused directly as its own component payload** -- no
    `TransformComponent` wrapper struct, since `Transform` (position +
    quaternion rotation + scale + `getModelMatrix()`) is already plain data
    with no dependency on the old `Entity` type.
  - **`ModelComponent`** -- a one-field wrapper around the existing
    `shared_ptr<Model>` (still sourced from `ResourceManager`'s cache, see
    "Phase 6" above), kept as its own distinct type rather than registering
    `shared_ptr<Model>` itself as a bare component, so a future component
    that also happens to want to store *a* `shared_ptr<something-else>`
    can't collide with "the" model component by type alone.
  - **What this deliberately is NOT**: an archetype/sparse-set ECS in the
    AAA-engine sense. No archetypes (entities sharing a component *set*
    aren't packed into one contiguous table together), no compile-time
    multi-component view/query type, no systems scheduler. This scene has
    exactly one entity (a `Transform` + `Model` pair) today, and Phase 8a's
    own scope doesn't add a second one -- "iterate every entity with a
    `Model`, then look up its `Transform`" (`each<ModelComponent>` plus
    `getComponent<Transform>`) is a one-line composition, exactly as fast
    and far simpler to read than a generic multi-type view would be at this
    entity count. If a later phase's entity count/variety ever makes
    archetype packing pay for itself, `ecs.hpp` is the file to replace --
    the same way it replaced `entity.hpp`.
- **`Application` integration** (`application.hpp`/`.cpp`): `entities_` (a
  `std::vector<Entity>`) becomes `registry_` (an `EntityRegistry`). The
  constructor's scene setup goes from `Entity sceneEntity("scene", model);
  entities_.push_back(...)` to `registry_.create()` plus two `addComponent<T>`
  calls -- one `Transform`, one `ModelComponent`. The three call sites that
  used to do `for (const Entity& entity : entities_) { if (entity.model())
  {...} }` -- the shadow pass (`renderShadowPass()`), SSAO's geometry
  pre-pass (`renderSSAO()`), and the main Blinn-Phong draw pass (`render()`,
  which also threads the entity's world-space transform into frustum
  culling, "Phase 13b") -- all now do
  `registry_.each<ModelComponent>([&](EntityId id, ModelComponent& mc) { ... })`,
  looking up `registry_.getComponent<Transform>(id)` alongside each model.
  Every one of those three passes produces exactly the same draw calls, in
  the same order, as before -- only the storage/iteration mechanism changed,
  not the frame's shape. The PBR sphere grid (`sphereInstances_`) and the
  hand-built ground plane (`groundMesh_`/`groundMaterial_`) are untouched --
  they were never `Entity`-based (see "Phase 9"/"Phase 7a" above for why),
  and this phase is scoped to promoting `Entity`/`entities_` specifically.
- **`tests/`**: still the Phase 0 placeholder (`enable_testing()` +
  `add_subdirectory(tests)` stay valid, no test executable exists yet) --
  checked before starting this phase; no test harness existed to extend, so
  none was bolted on from scratch, per this phase's own scope.
- **Verify**: a clean `-DCMAKE_BUILD_TYPE=Debug` rebuild compiles with zero
  warnings under `-Wall -Wextra`. `ctest` (from the build dir) reports "No
  tests were found" -- unchanged from before this phase, confirming no test
  suite regressed (none exists to). The real proof this is a pure refactor:
  a pre-Phase-8a build (commit `cf893fc`, built in a separate `git worktree`
  so the working tree never needed stashing) and this phase's build were
  each run headlessly for 60 frames
  (`ENGINE_MAX_FRAMES=60 bash tools/run_headless.sh`) and their screenshots
  compared with ImageMagick's `compare -metric AE`/`RMSE`: **0 differing
  pixels, RMSE 0** -- pixel-identical. Both runs' logs also independently
  report `Frustum culling: 0/12 drawables culled this frame`, the same
  drawable count as before, confirming the ECS-driven draw loop still visits
  the same set of things frustum culling tests every frame (the `scene.obj`
  model's nodes, the ground plane, and the 8 PBR sphere instances). The
  screenshot itself was also inspected directly: the table/box/pyramid,
  ground plane, directional light glow, and 8-sphere PBR grid all render
  exactly as they did before this refactor.

### Phase 8b: scene serialization / level loading

Phase 8a made `registry_`'s entities data instead of a fixed `Entity`
struct, but that data still only ever came from one hardcoded call sequence
in `Application`'s constructor. Phase 8b adds a real save/load round trip
for it -- `include/engine/scene_serialization.hpp` -- so a scene can be
authored/edited as a file and reloaded, rather than requiring a C++ change
and a rebuild for every scene edit.

- **Format: JSON via nlohmann/json, FetchContent'd like GLFW/GLM/Assimp,
  not hand-vendored like GLAD/stb_image**. This project had no existing
  serialization format to reuse, so this phase had to pick one. The
  question this phase's own brief raised -- does the GLAD/stb_image
  "vendor it by hand" precedent apply to a JSON library too? -- comes down
  to *why* GLAD is hand-vendored (see `CMakeLists.txt`'s own "GL loader"
  comment): it's a *code generator* with no single canonical
  pre-generated file to pull a git tag for, not "a dependency this
  project prefers not to fetch". nlohmann/json has none of that problem --
  it's an ordinary, actively-maintained, MIT-licensed library with tagged
  releases, exactly the same shape of dependency GLFW/GLM/Assimp already
  are -- so it's `FetchContent`'d (`v3.11.3`) the same way, not vendored.
  Concretely it's also single-header (no extra build step, one
  `INTERFACE` CMake target), and its JSON output is human-readable/
  diffable, so `assets/scenes/default.json` is reviewable in a PR the same
  way `assets/models/scene.obj`'s text format already is -- a hand-rolled
  line-oriented format was considered and rejected for the same reason a
  hand-rolled parser usually loses to a real one: today's schema needs
  nested objects and optional fields (see below), which a line-oriented
  format would need real design work (escaping, nesting) to support the
  moment a later phase's component adds one more optional field, work
  JSON already did for free.
- **Schema**: `{ "entities": [ { "name": ..., "transform": {"position":
  [...], "rotation": [w,x,y,z], "scale": [...]}, "model": {"path": ...} },
  ... ] }` -- see `scene_serialization.hpp`'s own header comment for the
  full field-by-field writeup. Two design choices worth calling out:
  - Each entity is "an array of named component blocks" (`"transform"`,
    `"model"`), not a schema hardcoded to today's exactly-one-Model-per-
    entity shape -- Phase 8c-8e adding a new component type (e.g. a
    physics body) is a new named block + a new parse/serialize branch in
    `scene_serialization.cpp`, not a schema rewrite. `"model"` is optional
    for the same reason: an entity can have a `Transform` with no `Model`,
    matching `ecs.hpp`'s own "components are opt-in per entity" design.
  - Rotation is a quaternion (`[w, x, y, z]`, the same field order
    `glm::quat`'s own constructor and `Transform::rotation()` use), not
    Euler angles -- storing Euler angles here would silently reintroduce
    the exact gimbal-lock problem `transform.hpp` already documents
    avoiding, one file away from where that reasoning lives.
- **Two new ECS components** (`ecs.hpp`): `ModelComponent` gains a `path`
  field (the asset path its `model` was loaded from -- `Model` itself has
  no notion of "the path it came from", so serialization has nowhere else
  to recover a *reloadable* reference from) and a new `NameComponent`
  (a one-field wrapper around a human-readable name, opt-in like every
  other component, so a saved entity is identifiable by more than a
  meaningless-once-reloaded `EntityId` index).
- **API, split into two translation units sharing one header** --
  `include/engine/scene_serialization.hpp` declares all four functions;
  `src/scene_serialization.cpp` implements the pure-data half
  (`parseSceneRecords()`/`writeSceneRecords()`: JSON <-> `SceneEntityRecord`,
  no GL/`ResourceManager`/`EntityRegistry` dependency at all) and
  `src/scene_loader.cpp` implements the `EntityRegistry`-facing half
  (`loadScene()`/`saveScene()`, which add the one GL-touching step --
  turning a model *path* into an actual `ResourceManager::getModel()`
  result). This split exists specifically so
  `tests/scene_serialization_test.cpp` (see "Verify" below) can link
  against the pure half alone and round-trip real scene data without
  standing up a window or GL context.
- **`Application` integration** (`application.cpp`): the constructor's old
  hardcoded `registry_.create()` + two `addComponent<T>()` calls are now
  gated behind `ENGINE_LEGACY_SCENE` (unset by default) -- same
  getenv-gated before/after pattern this file already uses for e.g.
  `ENGINE_USE_PROCEDURAL_SKYBOX` (see "Phase 13e"). The default path calls
  `loadScene(registry_, kDefaultScenePath, resources_, *shader_)` against
  `assets/scenes/default.json`, a checked-in scene file describing the
  exact same one entity (Phase 5's `scene.obj`, rotated 12 degrees around
  Y) the hardcoded path built directly in C++ -- so the running scene
  genuinely comes from the new loader by default, not just a library that
  exists unused alongside an unchanged hardcoded path.
- **Malformed input**: `loadScene()`/`parseSceneRecords()` throw
  `std::runtime_error` (after `LOG_ERROR`'ing the specific reason) for
  every boundary failure this phase is responsible for -- a missing scene
  file, syntactically invalid JSON (relaying nlohmann's own parse-error
  message, which already names the byte offset and reason), a
  structurally-valid-JSON-but-wrong-schema file (e.g. `"entities"` missing
  or not an array, an entity missing `"name"`), and a `"model"."path"`
  that doesn't resolve to a file on disk (checked explicitly, by name, so
  a multi-entity scene file's error names *which* entity's asset reference
  was bad, not just a bare Assimp failure several stack frames down) --
  exactly this project's existing convention for every other resource
  load failure (`Shader`/`Texture`/`Model`'s own constructors already do
  this; see `log.hpp`'s `LOG_ERROR` and `main.cpp`'s top-level
  `catch (const std::exception&)`), applied to a new kind of external
  input rather than inventing a second error-handling convention for it.
- **Post-8b bug-review fix**: adversarial testing beyond the schema cases
  the original `parseSceneRecords()` doc comment enumerated found two gaps
  in the "throws `std::runtime_error` after `LOG_ERROR`" contract above,
  both in `scene_serialization.cpp`: (1) a `"position"`/`"rotation"`/
  `"scale"` array that was the right length but held a non-number element
  (e.g. `"position": ["a", "b", "c"]`) made `readVec3()`/`readQuat()`'s
  `.get<float>()` throw a raw, un-`LOG_ERROR`'d `nlohmann::json::type_error`
  instead -- the length/array-shape check never verified each element was
  actually numeric before converting it; and (2) a numeric literal too
  large to represent (e.g. `"position": [1e400, 0, 0]`) makes nlohmann
  itself throw `json::out_of_range` ("number overflow parsing...")
  *during* `file >> root`, which is a different exception subclass than
  `json::parse_error` and so wasn't caught by the `catch` clause wrapping
  that parse at all -- it escaped `parseSceneRecords()` entirely
  unwrapped. Neither crashed `engine_app` (`main()`'s top-level
  `catch (const std::exception&)` still catches both, since every
  `nlohmann::json::exception` is one), but both broke the documented
  `std::runtime_error`-with-context contract this phase's own header
  comment promises, and produced a bare nlohmann diagnostic instead of
  this file's usual "which file, which entity, which field" message. Fixed
  by (1) an `allElementsAreNumbers()` check added to both readers before
  any `.get<float>()` call, and (2) widening the parse-time `catch` from
  `json::parse_error` to `json::exception` (the common base of both
  subclasses) so every nlohmann-thrown error at that boundary gets the
  same wrapped, logged treatment. Verified against a from-scratch
  adversarial harness exercising both cases (plus the schema/optional-
  field/multi-entity/special-character cases already listed under
  "Verify" below) and reconfirmed pixel-identical (`0` AE) headless
  renders before and after the fix, since it only changes malformed-input
  handling, not any valid-input code path.
- **`tests/`**: no longer just the Phase 0 placeholder.
  `tests/scene_serialization_test.cpp` is a plain executable (no
  Catch2/GoogleTest fetched -- one test case doesn't yet justify a real
  framework's build-time cost) registered via `add_test()`, so `ctest`
  actually runs something for the first time this project. It: (1) writes
  a handful of fabricated `SceneEntityRecord`s (including one exercising
  every field and one relying on every optional-field default) out via
  `writeSceneRecords()`, reads them back via `parseSceneRecords()`, and
  checks every field survived exactly (name, position, rotation, scale,
  model path); (2) feeds `parseSceneRecords()` a missing file, a
  syntactically invalid JSON file, and a valid-JSON-wrong-schema file, and
  checks each throws rather than returning something empty/wrong. It links
  against `src/scene_serialization.cpp` directly (see the "two translation
  units" bullet above) plus this project's already-fetched GLM and
  nlohmann/json -- no GLFW, GL, or Assimp needed to build or run it.
- **Verify**: a clean `-DCMAKE_BUILD_TYPE=Debug` rebuild compiles with zero
  new warnings under `-Wall -Wextra`. `ctest` reports the new
  `scene_serialization_test` passing. Headless runs
  (`ENGINE_MAX_FRAMES=60 bash tools/run_headless.sh`) of this phase's build
  (loading `assets/scenes/default.json` by default) and of a pre-Phase-8b
  baseline (commit `73b7e7a`, built in a separate `git worktree`) were
  compared with ImageMagick's `compare -metric AE`/`RMSE`: **0 differing
  pixels, RMSE 0** -- pixel-identical, and both logs independently report
  `Frustum culling: 0/12 drawables culled this frame`, the same drawable
  count as before. A third run with `ENGINE_LEGACY_SCENE=1` set (the
  hardcoded-construction escape hatch) was also compared against the
  default loader-driven run: also 0 AE / RMSE 0, confirming both paths
  build the identical scene. The screenshot was inspected directly too:
  table/box/pyramid, ground plane, directional light glow, and the
  8-sphere PBR grid all render exactly as before. Malformed-input handling
  was exercised for real against the built `engine_app` (not just the
  standalone test): a missing `default.json`, an invalid-JSON
  `default.json`, a `default.json` referencing a nonexistent model path,
  and a valid-JSON-wrong-schema `default.json` were each tried in turn --
  every one printed a specific `[ERROR]` line naming the actual problem,
  then a `[ERROR] Fatal: ...` from `main()`'s top-level handler, then
  exited with status 1 and a clean `Window destroyed, GLFW terminated` --
  no crash, no hang, no silently-empty scene.

### Phase 8c: a Dear ImGui debug overlay

Phase 8a/8b gave `registry_` real components and a real file format; Phase
8c adds the first actual *tool* for looking at/poking a running scene while
it renders, instead of only ever reading log lines or comparing screenshots
after the fact -- a [Dear ImGui](https://github.com/ocornut/imgui) debug
overlay, drawn last in the frame, straight onto the window.

- **Vendoring**: Dear ImGui is `FetchContent`'d (git tag `v1.92.9b`, bumped
  to `v1.92.9b-docking` by Phase 14a to gain docking support -- see that
  phase's own section for why upstream needed a different tag, not just a
  config flag, for that) the
  same way GLFW/GLM/Assimp/nlohmann_json already are -- an ordinary,
  actively-maintained, tagged dependency, not a code generator with no
  canonical pre-generated file to pull (see "GL loader" below for why
  *that* precedent doesn't apply here either). Dear ImGui ships no
  `CMakeLists.txt` of its own, though -- by upstream design it's meant to be
  compiled directly into whatever project embeds it -- so `CMakeLists.txt`
  builds it as a small first-party static library target (`imgui`), the
  same shape `glad` below already uses for the same underlying reason (a
  dependency with no ready-made CMake target): `imgui.cpp`/`imgui_draw.cpp`/
  `imgui_tables.cpp`/`imgui_widgets.cpp` (the core widget/draw-list sources)
  plus `backends/imgui_impl_glfw.cpp`/`imgui_impl_opengl3.cpp` (this
  engine's own windowing library and a GL-core-profile renderer backend).
  `imgui_demo.cpp` is deliberately left out -- nothing here ever calls
  `ImGui::ShowDemoWindow()`.
- **Wiring** (`include/engine/debug_ui.hpp`/`src/debug_ui.cpp`): `DebugUI`
  is a thin RAII wrapper around ImGui's context + its GLFW/OpenGL3 backend
  lifecycle -- the same "small class owns some GL-adjacent global state"
  shape `Window`/`Skybox`/`ShadowMap` already establish. Critically, its
  constructor takes an `enabled` flag (from `ENGINE_SHOW_DEBUG_UI`, unset by
  default) that gates *everything*: when disabled, no ImGui context is
  created, no GLFW callbacks are installed, and `newFrame()`/`render()` are
  no-ops for the object's whole life -- not just "an invisible window", but
  "never calls a single ImGui function". `Application::renderDebugUI()`
  (the actual panel-building code, in `application.cpp`) is called last
  from `render()`, after the tonemap/bloom postprocess pass has already
  resolved the HDR-lit scene onto the default framebuffer -- so every ImGui
  widget lands straight on the final, already-tonemapped 8-bit image rather
  than participating in the HDR/bloom/SSR pipeline (an ImGui draw call
  going through Reinhard tonemapping would be nonsensical: its colors are
  already meant for direct display). Shutdown
  (`ImGui_ImplOpenGL3_Shutdown`/`ImGui_ImplGlfw_Shutdown`/
  `ImGui::DestroyContext`) happens in `DebugUI`'s destructor, before
  `window_`'s own GL context goes away (declaration order in
  `Application` guarantees this).
- **Why disabled-by-default is "never calls ImGui", not "an invisible
  window"**: this project's whole headless-verification story rests on
  comparing rendered screenshots across phases (`compare -metric AE`, see
  every prior phase's own "Verify" section) -- a debug overlay that's merely
  *invisible* (alpha 0, or drawn off past the edge) would still touch GL
  state, still allocate a font atlas texture, still install GLFW callbacks.
  Gating the whole subsystem behind one `enabled_` bool checked at the top
  of every `DebugUI` method means the default path is provably identical to
  a pre-Phase-8c build, not merely "should look the same" -- confirmed
  below by an actual pixel-diff, not just code inspection.
- **`ENGINE_SHOW_DEBUG_UI`**: same getenv-gated pattern as every other flag
  in `Application` (`ENGINE_SSAO_DISABLE`, `ENGINE_CLUSTER_DEBUG`, etc.) --
  read once at startup, unset by default. No separate "force off" variable
  exists, since unset already means off.
- **Why ImGui's GLFW callbacks don't break `InputState`'s polling**:
  `ImGui_ImplGlfw_InitForOpenGL(window, /*install_callbacks=*/true)`
  registers GLFW key/mouse/cursor/scroll callbacks -- this engine had
  registered *zero* of its own before this phase (`input.hpp`'s
  `InputState` is a per-frame **poll** of `Window::isKeyPressed()`/
  `getCursorPos()`, never callback-driven, see that header's own comment),
  so there's nothing for ImGui's callbacks to clobber. Polling and
  callbacks are independent GLFW mechanisms: `glfwGetCursorPos()` always
  returns the real, current cursor position regardless of whether ImGui's
  callbacks also fired for the same event, and regardless of
  `io.WantCaptureMouse` -- that flag is only a hint for an application to
  *choose* to ignore input elsewhere, not something that changes what a
  poll-based query returns. This does mean the camera keeps reading mouse
  deltas while the cursor is over an ImGui widget (a real, known UX
  wrinkle for a shipped tool -- gating `Camera`'s own input behind
  `ImGui::GetIO().WantCaptureMouse` would fix it), but that's out of scope
  here: seeing this matter at all requires the overlay visible *and* live
  camera control at the same time, neither of which this repo's own
  headless-only verification exercises.
- **The panel itself** (`Application::renderDebugUI()`, deliberately
  modest -- see this phase's own brief's "What NOT to do"):
  - **Frame Stats**: frame count plus ImGui's own smoothed frame time/FPS
    (`io.Framerate`, an exponential moving average ImGui maintains
    internally -- nothing this engine needs to compute itself).
  - **Render Passes**: four checkboxes bound directly, by address, to
    `ssaoDisabled_`/`ssaoDebugMode_`/`ssrDisabled_`/`clusterDebugMode_` --
    the existing Phase 13d/13f/13g debug/disable flags. These were already
    plain `bool` members `render()` re-reads every frame (previously only
    ever set once, from an env var, at startup); an `ImGui::Checkbox`
    bound to one of them makes it live-toggleable with *no* further
    plumbing -- no getter/setter, no conversion from a getenv-once flag to
    a "real" runtime one, since they were already runtime-mutable state.
  - **Scene Entities**: a minimal inspector over
    `registry_.each<Transform>(...)` -- every entity with a `Transform`
    (not only ones with a `Model`, since `ecs.hpp`'s own design makes
    components opt-in independently), labeled by its `NameComponent` when
    present (falling back to a bare `"entity N"` label otherwise, since
    `NameComponent` is itself opt-in), with position/rotation/scale
    editable via `ImGui::DragFloat3`. Rotation is shown/edited as Euler
    degrees (`glm::eulerAngles`/`glm::radians` convert to/from the stored
    `glm::quat` each frame) purely because that's what a human can drag
    meaningfully -- `Transform`'s own storage stays quaternion-based (see
    `transform.hpp`'s own header comment on why), only the UI boundary
    converts.
  - Deliberately **not** built: scene save/load buttons wired to Phase
    8b's `saveScene()`/`loadScene()`, gamepad/rebindable-input support
    (Phase 8d), or physics visualization (Phase 8e doesn't exist yet) --
    all explicitly out of this phase's scope.
- **A real bug caught mid-phase, worth recording**: the very first working
  version of this overlay produced a debug window with perfectly valid
  draw data (real vertex/index counts, sane on-screen clip rects, zero GL
  errors) that nonetheless never showed up in `tools/run_headless.sh`'s own
  screenshot. In-process `glReadPixels` probes (bypassing the external
  screenshot mechanism entirely) proved the window *was* rendering
  correctly -- a proper dark ImGui panel with real text -- and a manually
  captured screenshot (a longer, deliberate delay before the one `xwd`
  capture, rather than `run_headless.sh`'s own polling loop) confirmed it
  visually. The actual cause: `run_headless.sh`'s screenshot-polling loop
  accepts the *first* capture that merely clears a minimum file-size
  threshold -- which this scene's own 3D content already clears on frame
  0, before `ImGui::Begin()`'s very first call has even measured its own
  window size (logged at a placeholder 32x35 before settling to its real
  size one frame later). So the harness's screenshot almost always lands
  on a frame that predates the overlay's first real content, independent
  of whether the overlay is actually working. This is a pre-existing
  property of the harness's own capture heuristic (documented in its own
  comments as "first big-enough frame wins"), not something this phase
  changes or needs to fix -- it doesn't affect the default (hidden)
  path's own verification at all (see below), and a real interactive
  session doesn't have this timing quirk in the first place.
- **Verify**: a clean `-DCMAKE_BUILD_TYPE=Debug` rebuild compiles with
  zero new warnings under `-Wall -Wextra` -- including inside the `imgui`
  target's own compilation, so no narrow warning suppressions were needed
  there either. `ctest` still reports `scene_serialization_test` passing
  (unaffected by this phase). Three headless configurations were each run
  for 60 frames (`ENGINE_MAX_FRAMES=60 bash tools/run_headless.sh`) and all
  three completed cleanly with zero GL errors: the default (overlay
  hidden), `ENGINE_SHOW_DEBUG_UI=1` (forced on), and
  `ENGINE_SHOW_DEBUG_UI=0` (forced off, same as default). The default
  run's screenshot was pixel-diffed against a from-scratch rebuild of the
  pre-Phase-8c commit (`7e6eae1`), built in a separate `git worktree`:
  **0 differing pixels, RMSE 0** -- byte-for-byte identical PNGs
  (matching MD5 sums), confirming this phase's default behavior changes
  nothing about the rendered scene every prior phase's own screenshot
  verification depends on. The overlay's actual rendered content was
  separately confirmed by direct visual inspection (a manually-timed
  capture well past the harness's own early-frame quirk described above),
  showing the Frame Stats/Render Passes/Scene Entities panel exactly as
  designed, correctly positioned and styled.

### Phase 8d: an input action-mapping layer

Through Phase 8c, `input.hpp`'s own header comment said `InputState` was
deliberately *not* a general action-mapping/input-binding system -- just the
concrete fields `Camera` reads, filled by `pollInputState()` hardcoding one
`window.isKeyPressed(GLFW_KEY_*)` check per key in `input.cpp`. Phase 8d is
that later phase: a real, data-driven binding table now sits underneath
`pollInputState()`, and `Camera`'s own interface -- and `InputState`'s
existing fields -- are completely unchanged by the refactor.

- **`InputAction` + `InputActionMap`** (`include/engine/input_action_map.hpp`/
  `src/input_action_map.cpp`, new files): `InputAction` is a plain
  `enum class` (`MoveForward`, `MoveBackward`, `MoveLeft`, `MoveRight`,
  `MoveUp`, `MoveDown`, `Quit`, `ToggleDebugUI`). `InputActionMap` maps each
  action to a `std::vector<int>` of GLFW_KEY_* constants (`bindings_`) --
  `MoveUp`/`MoveDown` each get two entries (Space-or-E, LeftShift-or-Q),
  replacing what used to be a hardcoded `||` of two `isKeyPressed()` calls.
  Reassigning a binding is now a `setBinding()`/`addBinding()` call, not an
  edit to an `if` inside `pollInputState()` -- even though nothing yet
  exposes a runtime rebinding *UI* for it (see "What NOT to do" below).
  Deliberately narrow, matching Phase 8b's scene-schema "extensible shape,
  not speculative handlers" convention: a binding is a plain `int`, not an
  invented device-agnostic key/button type -- this engine still reads from
  exactly one input device (keyboard), so abstracting over a second
  (gamepad) that doesn't exist yet would be speculative.
- **Level- vs. edge-triggered actions, and why both exist**: `InputActionMap`
  tracks each action's down/up state across polls (`ActionState{down,
  wasDown}`), giving two query methods:
  - `isDown(action)` -- true if any bound key is *currently* held. Correct
    for movement (`MoveForward` etc.) and for `Quit`: holding Escape should
    keep reading as "down" every frame, and `Application::run()`'s
    `if (input.escapePressed) break;` is idempotent whether checked once or
    every frame while held, so level-triggering was never actually a bug for
    Quit -- just not the right choice for a *toggle*.
  - `justPressed(action)` -- true only on the single poll where the action
    transitions from not-down to down, false on every subsequent poll while
    the same key stays held. `ToggleDebugUI` needs this: a level-triggered
    read would flip the debug overlay on then instantly back off every
    single frame F1 is held down, rather than toggling it once per physical
    press. `update(isKeyDown)` re-samples every bound action once per call
    and rolls the previous "current" into "previous" -- so getting
    edge-triggering right just requires `InputActionMap` staying the SAME
    object across frames (it's an `Application` member, not a per-frame
    local), not any new machinery in `Window`/GLFW (which only ever offered
    current-state polling in the first place).
- **`InputState` stays Camera's stable interface**: `pollInputState()`
  (`input.cpp`) now takes `(const Window&, InputActionMap&)`, calls
  `actionMap.update(...)` once, then fills the exact same
  `moveForward`/`moveBackward`/.../`escapePressed`/`cursorX`/`cursorY`
  fields as before -- from `actionMap.isDown(InputAction::MoveForward)`
  etc. instead of a direct `GLFW_KEY_W` check. One new field,
  `toggleDebugUIPressed` (from `actionMap.justPressed(InputAction::
  ToggleDebugUI)`), was added for `Application` to read -- `Camera` never
  looks at it, so its own interface (`processMovement`/`processMouseInput`)
  is byte-for-byte unchanged from Phase 8c.
- **Wiring `ToggleDebugUI` into `DebugUI`**: `DebugUI`'s constructor used to
  be the *only* place its ImGui context could ever come into existence --
  gated entirely on `ENGINE_SHOW_DEBUG_UI` at startup, with no way to turn
  it on later. Phase 8d adds `DebugUI::setEnabled(bool)`: flips `enabled_`,
  and if this is the FIRST transition into `enabled=true` for an object that
  started disabled, lazily runs the same ImGui context/GLFW/OpenGL3 backend
  setup the constructor would have run immediately had it started enabled.
  `Application::update()` calls `debugUI_.setEnabled(!debugUI_.enabled())`
  whenever `input.toggleDebugUIPressed` is true -- so `ENGINE_SHOW_DEBUG_UI`
  now just sets the *initial* state and F1 toggles from there, the same
  "env-var-initialized, then runtime-mutable" combination
  `ssaoDisabled_`/`ssrDisabled_` already use (Phase 13d/13g's env vars, made
  live-toggleable by Phase 8c's own checkboxes). Critically, a run that
  starts disabled and is never toggled on -- every headless verification run
  today, since Xvfb has no real F1 keypress to trigger it -- still never
  calls a single ImGui function across its whole lifetime, so this phase
  doesn't weaken Phase 8c's own "off means truly zero ImGui calls" guarantee
  for the one path this repo's own tooling actually exercises every commit.
- **Bonus: a read-only "Input Bindings" panel** under the existing debug
  overlay, listing each `InputAction` and its current bound key(s) by name
  (e.g. `Move Up: Space or E`) straight from `inputActionMap_.bindingsFor()`
  -- so the binding table is genuinely inspectable at runtime, not just in
  source. No editing here; a full rebinding *menu* is explicitly out of
  scope for this phase (see below), so this is read-only.
- **What NOT to do (per this phase's own scope)**: no serializable rebinding
  config file or settings UI -- "configurable keys", not "a key-rebinding
  menu"; `setBinding()`/`addBinding()` make rebinding *possible*
  programmatically, which is enough. No gamepad/joystick support. No change
  to `Camera`'s public interface or to any of `InputState`'s pre-existing
  field names/meanings.
- **Verify**: a clean `-DCMAKE_BUILD_TYPE=Debug` rebuild compiles with zero
  new warnings under `-Wall -Wextra`. A new test,
  `tests/input_action_map_test.cpp`, follows Phase 8b's
  `scene_serialization_test` pattern exactly -- a plain executable
  registered via `add_test()`, linking only `src/input_action_map.cpp` (no
  `Window`/GL dependency at all) plus the already-fetched `glfw` target for
  its `GLFW_KEY_*` constants, so it needs no live window/GL context/GPU. It
  checks: the defaults match this engine's pre-Phase-8d hardcoded bindings
  exactly; a multi-key action (`MoveUp`) reads down via *either* bound key;
  `isDown()` stays level-triggered (true across repeated polls with a key
  held, false once released); and `justPressed()` fires exactly once across
  polls with the same key held down throughout, then fires again on a fresh
  press after a release -- a fake `isKeyDown` callable (a closure over a
  plain `std::vector<int>` of "these keys are down this poll") stands in for
  `Window::isKeyPressed()`. `ctest` reports both `scene_serialization_test`
  and `input_action_map_test` passing. Headless verification
  (`ENGINE_MAX_FRAMES=60 bash tools/run_headless.sh`) was run in both the
  default configuration and `ENGINE_CAMERA_DEMO=1` (the scripted camera
  orbit, since Xvfb has no real keyboard/mouse to drive free-fly input or
  F1), and both screenshots were pixel-diffed against a from-scratch rebuild
  of the pre-Phase-8d commit (`ea77312`) in a separate `git worktree`:
  **0 differing pixels in either mode, with matching MD5 sums** -- proving
  this refactor changes nothing about the rendered frame, confirmed by
  direct visual inspection of both screenshots as well (the same checkered
  ground/spheres/pyramid/cube scene, and the same four-waypoint orbit, as
  every prior phase's own screenshots).

### Phase 8e: basic physics/collision

Phase 8a gave `registry_` a real component registry; Phase 8b gave it a real
file format that already promised a physics body would arrive as "a new
named block + a new parse/serialize branch, not a schema rewrite" (see that
section's own wording). Phase 8e is that later phase: gravity plus
ground-plane collision for ECS entities, proven out by a new demo entity
that visibly falls and comes to rest -- the first phase since 8a/8b/8d that
changes what the default headless screenshot actually looks like, rather
than being a refactor verified pixel-identical against a prior commit.

- **`RigidBody` + `Collider`** (`include/engine/physics.hpp`, new file):
  two new component types, registered via `EntityRegistry::addComponent<T>()`
  exactly the way `ecs.hpp`'s own header comment said a physics body would
  be -- without touching `ecs.hpp` itself. `RigidBody` holds a `velocity`
  (`glm::vec3`) and a `useGravity` bool; deliberately no mass field, since
  gravity's acceleration doesn't depend on it and this phase does no
  entity-vs-entity impulse response (the one place mass would actually get
  read) -- a stored-but-never-consumed field is exactly what this codebase's
  own established style (see `ecs.hpp`'s `NameComponent` comment) avoids.
  `Collider` holds a single `halfExtent` float: an axis-aligned box
  `[center - halfExtent, center + halfExtent]` on every axis, `center` being
  the entity's own `Transform::position()`. A box, not a reuse of
  `mesh.hpp`'s Phase 13b `BoundingSphere`, specifically because this phase's
  demo object is a cube (see below) and the one collision resolved here is
  against a flat, Y-constant ground plane, which only ever needs the
  collider's own Y half-extent either way -- a box that visually matches the
  cube being dropped avoids the "corners poking through" or "resting with
  visible daylight underneath" artifacts a sphere-vs-cube shape mismatch
  would otherwise produce.
- **`stepPhysics()`** (`src/physics.cpp`, new file): the "system" that
  consumes both components, following `ecs.hpp`'s own
  `registry.each<T>(...)` pattern. For every entity with a `RigidBody`: adds
  `kGravityAcceleration * deltaTime` to `velocity.y` (only if `useGravity`),
  integrates `velocity * deltaTime` into that entity's `Transform` position
  -- semi-implicit ("symplectic") Euler, velocity updated from acceleration
  *before* it moves position, the standard basic-but-stable choice over
  naive explicit Euler -- then, only for entities that *also* have a
  `Collider`, checks the resulting position against a `groundY` parameter
  and, if the collider's bottom face would cross below it, snaps the entity
  to rest exactly on the surface (`position.y = groundY + halfExtent`) and
  zeroes `velocity.y`. Checking the already-integrated position (rather than
  sweeping the volume moved through this step, the way general continuous
  collision detection would) is enough to never tunnel through this one
  flat, infinite plane regardless of step size -- see `stepPhysics()`'s own
  comment for why that's specifically true for a flat-plane collider and
  wouldn't generalize to a second moving body. Depends on nothing but
  `ecs.hpp`/`transform.hpp` (both plain data, no GL/Window dependency at
  all), matching `scene_serialization.cpp`'s own "pure logic, no GL" split
  so `tests/physics_test.cpp` can link against this file alone.
- **Wired into `Application::update()`**: one `stepPhysics(registry_, ...,
  kGroundY)` call, reusing the same `deltaTime` already threaded through to
  `camera_.processMovement()` that frame rather than a second clock --
  placed before the camera-driving branches (physics doesn't read `camera_`
  at all) and before `render()` runs later that frame, so anything that
  moved is already at its new position by the time it's drawn. Clamped
  first to a new `kMaxPhysicsTimestep` (1/60 s, `application.cpp`): this
  project's own headless verification runs a Debug build's frame -- CSM's 3
  depth passes, clustered lighting's compute dispatch, SSAO's 3 passes,
  bloom, SSR, all under Mesa's `llvmpipe` software rasterizer -- at roughly
  0.2s of *real* elapsed time each (confirmed directly: 90 frames took
  18.3s, 30 frames took 6.0s, both ~0.2s/frame). Without a cap, a single
  frame's raw `deltaTime` would already be large enough to integrate this
  phase's own falling cube straight past the ground and settle it within
  the first frame or two -- leaving nothing for a multi-frame headless
  screenshot sequence to actually show falling at different heights. Camera
  movement deliberately keeps using the unclamped, real `deltaTime` (its
  speed staying tied to actual wall-clock time is correct there); only
  physics integration has a reason to decouple from it.
- **The demo entity**: `assets/scenes/default.json` gained a second entity,
  `falling_cube` -- a `Transform` (starting at `(0.0, 2.5, 1.1)`), a
  `ModelComponent` (a new small hand-authored asset,
  `assets/models/falling_cube.obj`/`.mtl`: an axis-aligned cube,
  half-extent 0.25, bright yellow so it reads as a distinct "thing being
  dropped" against `scene.obj`'s own table/box/pyramid materials and the
  neutral PBR sphere grid), a `RigidBody` (`gravity: true`,
  zero initial velocity), and a `Collider` (`halfExtent: 0.25`, matching the
  mesh's own half-extent exactly, so the entity's `Transform` position IS
  the physics center the mesh's local origin sits at). It needs zero new
  code in `render()`/`renderShadowPass()`/`renderSSAO()`: all three already
  iterate `registry_.each<ModelComponent>(...)` generically (Phase 8a), so
  the falling cube is shadowed, SSAO-sampled, frustum-culled, and lit
  exactly like every other `ModelComponent` entity, automatically, the
  moment `stepPhysics()` starts moving its `Transform`.
  - **Placement**: `(0.0, 2.5, 1.1)` in x/z sits in the open gap between
    `scene.obj`'s own table/box/pyramid (documented extents roughly
    x in `[-1.7, 1.0]`, z in `[-0.65, 0.5]`, expanding to about z = 0.61 at
    worst after the scene's own 12-degree Y rotation) and the PBR sphere
    grid (`kSphereGridDistanceFromCamera`/`kSphereGridHeight`, which works
    out to roughly x in `[1.05, 2.48]`, z in `[1.63, 3.0]` including sphere
    radius) -- clear of both by a comfortable margin on every side, and
    close enough to dead-center in the default camera's view that it stays
    on-screen (mostly out of frame at the very top at the start of its
    fall, coming fully into frame as it drops) rather than needing an
    extreme, frustum-edge position.
  - **Starting height and gravity, tuned together**: 2.5 (start) and 9.81
    m/s² (`kGravityAcceleration`) were picked, together with the 1/60s
    physics-step cap above, specifically so this phase's own
    `ENGINE_MAX_FRAMES=5/30/90` headless checkpoints land on three visually
    distinct, meaningful states (worked out by hand-simulating the same
    semi-implicit-Euler recurrence `stepPhysics()` runs): at frame 5 the
    cube has fallen only ~0.04 units (`y ≈ 2.459`, still mostly above the
    frame -- "barely started falling"); at frame 30 it's fallen to
    `y ≈ 1.233`, clearly airborne and roughly halfway down; by frame 41 it
    lands (`y = restY = groundY + halfExtent = 0.24`) and stays there, so at
    frame 90 it's long since settled, at rest, velocity zeroed. This is also
    why 9.81 (Earth-standard gravity) was kept rather than an arbitrarily
    tuned "faster" constant -- the physics-step clamp alone, not an
    unrealistic gravity value, is what makes a several-frame fall visible
    under this project's own coarse per-frame headless timing.
- **Scene schema extended, not rewritten**: `scene_serialization.hpp`'s
  schema gains two more independently-optional per-entity blocks --
  `"rigidBody": {"gravity": bool, "velocity": [x,y,z]}` and
  `"collider": {"halfExtent": n}` -- both with every one of their own
  fields optional too (`"rigidBody": {}` is a valid, at-rest body).
  `SceneEntityRecord` (`scene_serialization.hpp`) gained the matching plain
  fields (`hasRigidBody`/`rigidBodyGravity`/`rigidBodyVelocity`/
  `hasCollider`/`colliderHalfExtent`); `scene_serialization.cpp` (the pure
  JSON<->record half) parses/writes them with the same "absent means
  default, present-but-malformed throws" convention `readVec3`/`readQuat`
  already established, via two small new helpers (`readBool`/`readFloat`)
  -- and still has no dependency on `physics.hpp`/`ecs.hpp` at all.
  `scene_loader.cpp`'s `loadScene()`/`saveScene()` (the `EntityRegistry`-
  facing half) are the only place that turns those fields into/out of real
  `RigidBody`/`Collider` components, matching how `ModelComponent` already
  splits across the same two files.
- **Interaction with `ENGINE_LEGACY_SCENE` ("Phase 8b")**: that escape
  hatch's hardcoded C++ scene construction was deliberately NOT extended
  with a second entity mirroring `falling_cube` -- doing so would mean
  hand-duplicating in C++ exactly the entity data `default.json` already
  carries, defeating the flag's own purpose (isolating scene-*loading*
  regressions, not scene *content*). `InputActionMap`-driven camera control
  ("Phase 8d") and the Phase 8c debug overlay are both entity-count-
  independent, so they're unaffected under `ENGINE_LEGACY_SCENE=1`;
  `stepPhysics()` (unconditional
  every frame, see above) simply iterates zero `RigidBody` entities under
  that path instead of one -- confirmed by a headless run with
  `ENGINE_LEGACY_SCENE=1` set: it completes with zero errors and one fewer
  drawable logged by frustum culling than the default path, matching exactly
  the one entity `falling_cube` alone accounts for, everything else identical.
- **What NOT to do (per this phase's own scope)**: no general rigid-body
  solver, constraint system, or continuous collision detection beyond "a
  reasonable timestep can't skip through the one flat ground plane" (see
  `stepPhysics()`'s own comment on why a single-plane check already
  guarantees that). No entity-vs-entity collision -- ground collision plus
  gravity is the load-bearing requirement this phase actually needed; two
  falling objects separating when they overlap is a real nice-to-have this
  phase's own brief explicitly scoped out rather than adding for a single
  demo entity with nothing to collide with. No physics on the PBR sphere
  grid, the ground plane, or `scene.obj`'s own static furniture entity --
  none of those are meant to move, and `RigidBody` is opt-in per entity like
  every other component here, so nothing forces it onto them. No
  user-controlled physics/character controller -- out of "basic
  physics/collision" scope, that's a gameplay feature.
- **Verify**: a clean `-DCMAKE_BUILD_TYPE=Debug` rebuild compiles with zero
  new warnings under `-Wall -Wextra`. A new test, `tests/physics_test.cpp`,
  follows `scene_serialization_test`/`input_action_map_test`'s own "plain
  executable, links only the pure logic file it's testing" shape (no live
  GL context, no window, no GPU): it builds a real `EntityRegistry`, gives
  one entity a `Transform` + `RigidBody` + `Collider`, and asserts the exact
  hand-computed semi-implicit-Euler trajectory both after a handful of
  steps (still falling, above `restY`, moving downward) and after enough
  steps that it must have landed (`position.y == groundY + halfExtent`
  exactly, `velocity.y == 0.0` exactly) -- plus that a `RigidBody` with no
  `Collider` never collides (falls straight through `groundY`) and a
  `RigidBody` with `useGravity = false` never moves at all.
  `scene_serialization_test.cpp` gained a third fabricated entity exercising
  both new blocks with non-default field values, round-tripped through
  `writeSceneRecords()`/`parseSceneRecords()` like every other field.
  `ctest` reports all three tests (`scene_serialization_test`,
  `input_action_map_test`, `physics_test`) passing.
  Headless verification (`tools/run_headless.sh`) was run at
  `ENGINE_MAX_FRAMES=5`, `=30`, and `=90`, with every run's log confirming
  **zero `[ERROR]`/GL-error lines** (Debug builds actively drain
  `glGetError()` after every `GL_CHECK`-wrapped call, see `gl_debug.hpp`).
  Unlike Phase 8a/8b/8d, these screenshots are *not* pixel-diffed against a
  prior commit -- this phase deliberately changes the rendered frame (a new
  object now visibly falls and settles) -- so correctness was instead
  confirmed by direct visual inspection at all three checkpoints: at frame 5
  the cube is barely visible, cropped by the top edge of frame, having
  fallen only a hair from its start height; at frame 30 it's clearly
  airborne, roughly halfway down, passing in front of the pyramid; at frame
  90 it's resting flush on the checkered ground plane -- casting a contact
  shadow, no visible gap underneath and no clipping into the floor -- well
  clear of both `scene.obj`'s furniture and the PBR sphere grid in every
  frame.

### Phase 14a: editor window scaffolding + an empty ImGui docking shell

First sub-phase of a new, larger arc -- **"Phase 14: full editor UI"** --
moving this project from "an engine that renders one fixed demo scene" toward
"an engine with an actual editor around it" (Unity/Blender-style: a scene
hierarchy + asset browser on the left, a 3D viewport filling the center, an
inspector on the right). This phase is scaffolding only: a bigger, properly
resizable window and an empty Dear ImGui docking layout with four placeholder
panels. No real panel content, no render-to-texture viewport (Phase 14c), no
real scene hierarchy (Phase 14d), no real inspector (Phase 14e) -- those are
later sub-phases of this same arc, each following the same
build-verify-commit-then-check-in-before-push pattern as this one.

- **The vendored Dear ImGui tag had to change, not just its config flags**:
  this phase's own brief assumed docking was already merged into upstream
  Dear ImGui's `master` branch by v1.92.9b (the tag Phase 8c already
  vendors). It is not, and never has been -- `ocornut/imgui` has kept
  docking (`DockSpace`/`DockBuilder*`/`ImGuiConfigFlags_DockingEnable`,
  `ImGuiViewport`, etc.) on a permanently separate `docking` branch/tag
  family for its entire existence, released in lockstep with every ordinary
  tag rather than ever merged in. Verified directly before writing a single
  line of this project's own code: `git ls-remote --tags` against upstream
  shows a `v1.92.9b-docking` tag alongside the plain `v1.92.9b` this project
  already pins, and a clone of the latter has zero occurrences of
  `ImGuiConfigFlags_DockingEnable`/`DockBuilder*`/`DockSpaceOverViewport`
  anywhere in `imgui.h`/`imgui_internal.h`. `CMakeLists.txt`'s `GIT_TAG`
  switched to `v1.92.9b-docking` -- the exact same release, plus docking;
  nothing else about this project's ImGui usage or version changes.
- **Bigger, resizable window** (`main.cpp`): the default window size grows
  from Phase 1's 800x600 to **1600x900** -- big enough that all four
  dockspace panels below get genuinely usable screen space instead of a
  cramped sliver, while staying an ordinary 16:9 window a user can still
  move/tile/resize on their own desktop rather than a forced fullscreen.
  1600x900 (not 1920x1080) was chosen specifically to bound how much this
  phase grows headless-verification render time (see below). The literal
  800x600 default was never hardcoded inside `Window` itself -- it was
  always `main.cpp`'s call-site choice (`engine::Application app(800, 600,
  ...)`) -- so only that call site changed; `Window`'s constructor still
  takes width/height as plain parameters, unconstrained. `Window` also
  gained an optional `maximized` constructor parameter (sets the
  `GLFW_MAXIMIZED` hint before creation) -- **defaults to false** (an
  ordinary resizable window the user maximizes themselves), with
  `ENGINE_WINDOW_MAXIMIZED=1` as the documented opt-in for "start already
  maximized" instead, the same getenv-gated-off-by-default pattern as every
  other flag in this engine. No `GLFW_RESIZABLE` hint has ever been set
  (Phase 1 onward) -- GLFW's own default (resizable) already applied before
  this phase; what this phase actually adds on top is something reacting to
  a resize (next point).
- **Live resize doesn't crash or corrupt GL state**: `Window`'s constructor
  now also registers a `glfwSetFramebufferSizeCallback()` that immediately
  re-applies `glViewport()` to the new framebuffer size. This is deliberately
  redundant with `Application::render()`'s own per-frame
  `glViewport(0, 0, fbWidth, fbHeight)` call (confirmed, not changed, by this
  phase -- it already re-derives `fbWidth`/`fbHeight` fresh from
  `window_.getSize()` at the top of every `render()` call, never a value
  cached at construction), but the callback matters for one case the
  per-frame call alone doesn't cover: on at least Windows, an interactive
  drag-resize runs GLFW's event pump through a modal loop for the whole
  drag, during which this engine's own main loop (poll -> update -> render
  -> swap) never reaches its next `render()` call -- only a registered
  callback fires during that blocked period. **Explicitly out of scope for
  this phase**, and unnecessary for its own bar of "doesn't crash / doesn't
  corrupt GL state": actually redrawing new content live *during* a drag
  (so the window doesn't appear frozen while being resized) would require
  rendering from inside the callback itself, and every fixed-size render
  target this engine allocates (`hdrFramebuffer_`, SSAO's g-buffer, bloom's
  ping-pong buffers, the shadow maps) is still sized once, at construction,
  from the window's *initial* framebuffer size -- unaffected by the resize
  callback, not resized when the window is. A resize therefore cannot crash
  or corrupt any of those buffers, but it can make the final on-screen image
  look visually stretched relative to the window's new aspect ratio until
  those buffers are themselves resize-aware -- deferred to Phase 14c's
  render-to-texture viewport work, exactly as this phase's brief scoped it.
- **DebugUI (F1 diagnostic overlay, Phase 8c) vs. EditorUI (this phase's
  always-on dockspace)**: two classes, serving genuinely different purposes
  -- an occasional diagnostic HUD vs. persistent editor chrome -- but they
  cannot each independently own a full ImGui context/backend the way the
  brief's "alongside DebugUI" framing might first suggest. Dear ImGui
  supports multiple `ImGuiContext` instances in principle, but
  `imgui_impl_glfw`'s GLFW callbacks are installed **once per GLFWwindow\***,
  not once per context -- two independent
  `ImGui_ImplGlfw_InitForOpenGL(window, install_callbacks=true)` calls on
  the *same* window would have the second silently clobber the first
  context's own callback registration at the GLFW level. So there is
  exactly one live ImGui context driving this window's input, period, and
  the question is only which class owns it. Since `EditorUI`'s dockspace is
  unconditionally on every run (unlike `DebugUI`'s F1-toggled overlay), it's
  the natural, and now the only, owner: `EditorUI` (`editor_ui.hpp`/
  `editor_ui.cpp`) creates the ImGui context, sets
  `io.ConfigFlags |= ImGuiConfigFlags_DockingEnable`, and owns the
  `ImGui_ImplGlfw`/`ImGui_ImplOpenGL3` backend lifecycle -- unconditionally,
  no enabled/disabled gate, mirroring the shape `DebugUI` used to have
  itself before this phase. `DebugUI` was slimmed down to what it actually
  still needs to be: the `enabled_` bool `ENGINE_SHOW_DEBUG_UI`/F1 have
  always controlled, plus `setEnabled()`'s same log lines as before -- it no
  longer touches ImGui/GL at all itself. `Application::renderDebugUI()`
  (its actual panel-building code) is **completely unchanged** -- same
  Frame Stats/Render Passes/Input Bindings/Scene Entities content, same
  `ENGINE_SHOW_DEBUG_UI`/F1 gating -- it just now submits its `ImGui::`
  calls into whichever frame `EditorUI` already started, instead of
  bracketing its own separate `ImGui::NewFrame()`/`Render()` pair.
  `Application::render()`'s tail is now: `editorUI_.newFrame()` ->
  `editorUI_.renderDockspaceShell()` -> `renderDebugUI()` (a no-op unless
  enabled, exactly as before) -> `editorUI_.render()`.
- **The dockspace layout** (`EditorUI::renderDockspaceShell()`):
  `ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode)`
  covers the whole main viewport; the first time it ever runs (guarded by an
  internal `layoutBuilt_` flag, not re-run every frame -- `imgui.ini`
  persistence is left off, same reasoning as `DebugUI`'s own Phase 8c
  choice, so there's nothing on disk to restore instead), the DockBuilder
  API (`imgui_internal.h` -- still marked "not yet a stable public API"
  upstream, but the documented, standard way every Dear ImGui docking app
  sets up an initial layout; mirrors `imgui_demo.cpp`'s own
  `ShowExampleAppDockSpace()`) splits it into the approved-mockup regions:
  **Scene** (left column, upper), **Assets** (left column, lower),
  **Inspector** (right column), **Viewport** (center, whatever's left).
  Each panel is `ImGui::Begin()`/`End()` with one `ImGui::TextWrapped()`
  placeholder line (e.g. "Scene Hierarchy -- coming in Phase 14d") -- no
  real content, no Play/Pause/Restart or other toolbar chrome. Guarding on
  `layoutBuilt_` (checked once, not every frame) means a user's own later
  drag-to-resize/rearrange of these four panels is never stomped by this
  code on a subsequent frame.
- **The Viewport panel currently floats over the still-directly-rendered 3D
  scene, not a texture of it** -- exactly the documented, temporary,
  intermediate state this phase's brief called out rather than something
  this phase tried to prematurely solve. The 3D scene keeps rendering
  straight to the default framebuffer precisely as every prior phase left
  it, entirely independent of `EditorUI`; `ImGuiDockNodeFlags_PassthruCentralNode`
  only makes a dock node's background see-through when that node is
  **empty** -- since "Viewport" is docked *into* the central node (not
  merely left unoccupied), its own ordinary opaque window background covers
  that region regardless of the flag, hiding the 3D render behind it in
  practice. Confirmed directly (see the manual-capture screenshot described
  below): at any window size, the Scene/Assets/Inspector panels show the
  scene behind their own edges' gaps, but the Viewport panel's own interior
  is flat dark background, not the 3D render. Rendering the actual scene
  into that panel is exactly Phase 14c's job.
- **A real, reproducible headless-harness bug this phase's own content
  exposed** (not present in any prior phase, worth recording in full):
  `tools/run_headless.sh`'s polling loop accepted the *first* `xwd`+
  `convert` capture whose file size cleared `MIN_SCREENSHOT_BYTES=20000` --
  tuned against every prior phase's own "real content" always being a
  dense, colorful lit 3D render (hundreds of KB), safely distinguishable
  from Xvfb's blank root window (~241 bytes). This phase's own steady-state
  frame -- four mostly-text-on-dark-background dockspace panels covering
  most of the window -- PNG-compresses to only **~11-12KB**, *below* that
  old threshold: a real screenshot mistaken for a possibly-still-blank one.
  Worse, a direct frame-by-frame trace of a real headless run (`xwd`
  captured every 0.2s against the same running process, sizes logged) found
  the *first* frame with any real content at all momentarily showed the
  full 3D scene with **no dockspace at all** -- a one-frame gap before
  settling into the stable, dockspace-covered state every frame after it
  -- a known, benign Dear ImGui docking startup transient (a freshly
  created dock node's child window needs one frame to actually occupy its
  slot). That one transient frame happened to be large enough (a full,
  uncovered 3D render) to clear the *old* 20000-byte threshold, so the
  original "first passing screenshot wins" loop reliably grabbed that one
  anomalous frame instead of the stable one immediately following it --
  reproduced identically across multiple separate runs. Fixed by lowering
  `MIN_SCREENSHOT_BYTES` to 2000 (comfortable margin above the ~241-byte
  blank case, clears every real frame observed on either side of this
  phase) and requiring the size check to pass on **two consecutive** 0.2s-
  apart polls (not just one) before accepting -- skips past exactly that
  kind of single anomalous frame, at the cost of one extra 0.2s poll in the
  common case where there's nothing to debounce. This is a fix to shared
  verification infrastructure every phase's headless run depends on, called
  out explicitly rather than folded in silently.
- **Verify**: a clean `-DCMAKE_BUILD_TYPE=Debug` rebuild (after clearing the
  stale pre-docking `imgui-src`/`imgui-subbuild`/`imgui-build` FetchContent
  state so the new `v1.92.9b-docking` tag actually gets fetched) compiles
  with **zero new warnings** under `-Wall -Wextra`. `ctest` still reports
  all three tests passing (`scene_serialization_test`,
  `input_action_map_test`, `physics_test`) -- this phase touches no tested
  logic. `tools/run_headless.sh` (fixed per above) was run repeatedly at
  `ENGINE_MAX_FRAMES=60`: every run completed cleanly (exit 0), **zero
  `[ERROR]`/GL-error lines**, and the resulting screenshot is byte-identical
  across separate runs (12686 bytes each time) -- deterministic, showing the
  four dockspace panels in their correct approved-mockup positions with the
  Viewport panel's own dark interior in the center (see the point above on
  why the 3D scene isn't visible through it yet). `ENGINE_SHOW_DEBUG_UI=1`
  was separately confirmed to still show the exact same "Engine Debug"
  panel (Frame Stats/Render Passes/Input Bindings/Scene Entities, listing
  both `scene` and `falling_cube`) as a normal free-floating ImGui window
  layered over the dockspace, unaffected in content -- confirming `DebugUI`'s
  own behavior truly didn't change. A separate, un-clamped run directly
  exercised the *actual* default (1600x900, no `ENGINE_WINDOW_WIDTH`/HEIGHT`
  override, against a matching 1600x900 Xvfb screen) end to end: it also
  completed cleanly with zero GL errors and rendered the same dockspace
  layout correctly proportioned at the bigger size. That run measured
  **~0.51s/frame at 1600x900 vs. ~0.30s/frame at 800x600** in this Debug
  build on this project's llvmpipe software rasterizer (roughly 1.7x, not
  the full 3x the pixel-count increase might suggest, since several render
  passes -- bloom, SSAO -- are already downsampled/fixed-resolution) --
  confirming the decision below to keep the *headless verification* path
  pinned at 800x600 rather than accept that slowdown across every future
  phase's own headless runs.
  - **Why headless verification stays at 800x600 instead of widening to
    match**: `main.cpp` reads `ENGINE_WINDOW_WIDTH`/`ENGINE_WINDOW_HEIGHT`
    to override its own new 1600x900 default (same validated,
    warn-and-fall-back-on-garbage-input shape as `ENGINE_MAX_FRAMES`);
    `tools/run_headless.sh` now sets both to 800/600 before launching
    `engine_app`, and Xvfb's own virtual screen is left at its original
    800x600x24 (unchanged) to match. Widening Xvfb to 1600x900 instead (the
    other option this phase's brief allowed) was rejected specifically
    because of the measured ~1.7x-slower-per-frame result above: every
    render pass sized off the window's real framebuffer would run at that
    cost on the exact same 60s hard-kill timeout every *prior* phase's own
    headless verification was measured against (sized off Phase 13g's own
    ~20s/60-frames Debug-build cost) -- risking a regression to every
    existing phase's headless story, not just this one, for a resolution
    bump this phase's own placeholder-only panels don't substantively
    need. A real interactive run still gets the bigger, more usable 1600x900
    default; only this specific verification path opts back down.
  - **Live resize -- the honest limitation**: Xvfb has no real window
    manager or pointer device driving an actual interactive drag-resize, so
    this phase's live-resize claim rests on: (a) direct code inspection
    confirming `render()`'s per-frame `glViewport()` call already re-derives
    the current framebuffer size fresh every frame rather than a cached
    value (true before this phase too, just now verified rather than
    assumed); (b) the new `glfwSetFramebufferSizeCallback()` registered and
    exercised at both tested sizes (800x600 and 1600x900) without error,
    confirming the callback itself compiles, links, and fires correctly for
    the one resize GLFW *does* deliver headlessly (the initial
    window-creation size); and (c) no code path in either the callback or
    `render()`'s existing per-frame viewport call does anything unsafe with
    a changed size (0-sized viewports are well-defined in GL; no fixed-size
    FBO is touched by either). What this does **not** cover: an actual
    interactive drag-resize's full modal-loop behavior (relevant primarily
    on Windows -- see `window.hpp`'s own comment), which needs verification
    on a real desktop. Documented plainly rather than claimed as fully
    proven -- this matches this phase's own brief, which anticipated that
    exact limitation as an acceptable answer for a headless-only
    verification environment.

### Phase 14b: real parent/child transform hierarchy

Second sub-phase of the "Phase 14: full editor UI" arc. Pure engine/ECS work
with no UI of its own -- the Scene Hierarchy panel that will eventually
*display* this as a tree is Phase 14d, later. This phase's job is only to
make parenting real: moving/rotating a parent actually moves its children
with it, the way Unity/Blender's own parent/child transforms behave, as
opposed to a flat, non-transforming organizational "folder" grouping concept
-- the user's own explicit choice between the two, made before this phase's
design was written.

- **`Parent`, a new opt-in component** (`include/engine/transform_hierarchy.hpp`,
  new file): `struct Parent { EntityId id; };`, registered via
  `addComponent<Parent>()` exactly the way `ecs.hpp`'s own header comment
  says a new component type should be -- without touching `ecs.hpp` itself,
  the same way `RigidBody`/`Collider` (Phase 8e, `physics.hpp`) already
  didn't. `Parent` gets its own header for the same reason `RigidBody`/
  `Collider` did rather than living in `ecs.hpp` alongside `ModelComponent`/
  `NameComponent`, and deliberately drops the "Component" suffix those two
  carry -- `ecs.hpp`'s own comment explains that suffix exists specifically
  to avoid a bare `struct Model` colliding with the already-named `Model`
  class; there is no preexisting `Parent` type to collide with, so `Parent`
  follows `RigidBody`/`Collider`'s "self-contained concept, no suffix"
  treatment instead. An entity with no `Parent` component is a root: its own
  `Transform` **is** its world transform, unchanged from every phase before
  this one -- every entity that existed before Phase 14b keeps rendering
  exactly as it did unless it's specifically given a `Parent`.
- **`resolveWorldMatrix(EntityRegistry&, EntityId)`**
  (`src/transform_hierarchy.cpp`): walks from an entity up through `Parent`
  components, collecting each level's own local `getModelMatrix()`, then
  folds them together as `parentWorldMatrix * thisEntity'sOwnLocalMatrix`,
  recursively up the chain to whichever ancestor is itself a root -- an
  iterative walk (not real recursion), so the safety bounds below are loop
  bounds, not call-stack depth. Returns `glm::mat4(1.0f)` for an entity with
  no `Transform` at all, matching every render call site's own pre-existing
  fallback for that case.
  - **Cycle safety**: a visited-id set catches a cycle of any length
    (`A.parent = B, B.parent = A`, or longer) -- detected, it stops and
    treats the entity as an effective root from there, rather than looping
    forever.
  - **A hard depth bound** (`kMaxParentChainDepth = 64`) is a second,
    independent guard on top of the visited-set check, against a
    pathologically long (but not necessarily cyclic) chain -- belt-and-
    suspenders, not strictly required for correctness given the visited set
    alone, but cheap insurance against a future bug that grows the visited
    set without actually revisiting an id.
  - **Dangling parent safety**: a `Parent::id` that no longer names a live
    `Transform` (not reachable yet -- there's no entity-destroy UI before
    Phase 14f -- but designed for it anyway) is treated the same way: the
    entity becomes an effective root from that point, not a crash.
  - All three cases log a warning via `LOG_WARN` (`log.hpp`) **once per
    offending entity**, not once per frame -- `resolveWorldMatrix()` is
    called once per `ModelComponent` entity every single frame (see the
    `application.cpp` call sites below), so a persistent cycle/dangling
    reference would otherwise re-log identically 60 times a second for as
    long as the engine runs. A module-level `std::unordered_set` of
    already-warned entity indices keeps the message genuinely diagnostic.
- **Physics stays local-space-only -- a deliberate scope boundary, not an
  oversight** (`physics.cpp`'s own new Phase 14b comment states this
  explicitly): `stepPhysics()` is **not** made `Parent`-aware. A `RigidBody`
  entity's gravity integration and ground collision still read/write only
  its own entity's local `Transform`, exactly as Phase 8e left it -- no
  `#include` of `transform_hierarchy.hpp` was added to `physics.cpp` at all.
  A parented, physics-simulated entity (e.g. an object that should fall
  relative to a moving platform) would need gravity/collision to reason in
  the parent's own moving reference frame -- relative-velocity transforms,
  world-space-vs-local-space gravity, etc. -- which is real, separate scope
  Phase 8e's own "basic physics/collision, not a general solver" boundary
  doesn't call for and this phase declines to open. What **does** work: a
  *static* (no `RigidBody`) entity parented under a `RigidBody` entity
  correctly rides along with it **visually**, because `resolveWorldMatrix()`
  runs at render time, after `stepPhysics()` has already updated the
  parent's own local `Transform` for that frame -- no physics code needs to
  know parenting exists for that to work.
- **Wired into all three `getModelMatrix()` render call sites**
  (`application.cpp`): the shadow pass (`renderShadowPass()`), the SSAO
  g-buffer pass (`renderSSAO()`), and the main color pass (`render()`) each
  used to do
  `transform != nullptr ? transform->getModelMatrix() : glm::mat4(1.0f)`;
  all three now call `resolveWorldMatrix(registry_, id)` instead, so a
  parented entity renders, casts shadows, and appears in the SSAO g-buffer
  at its correct **world** position rather than its raw local one. An
  entity with no `Parent` resolves to exactly its own `getModelMatrix()`, so
  this is behavior-preserving for every entity that existed before this
  phase.
- **Scene JSON schema: an optional `"parent"` field, by name, not by raw
  `EntityId`** (`scene_serialization.hpp`/`.cpp`, `scene_loader.cpp`) --
  exactly the "new named block, not a schema rewrite" extension pattern this
  schema's own comment predicted, the same way Phase 8e's `"rigidBody"`/
  `"collider"` blocks were. `"parent": "some_other_entity_name"` names
  another entity in the same file by its `"name"` field, never by a raw
  numeric index -- an `EntityId` is a fresh, monotonically-increasing index
  handed out at *load* time (`EntityRegistry::create()`), meaningless across
  a save/load round-trip, which is the exact reason `NameComponent` exists
  at all.
  - **A child can be listed before its parent in the file.**
    `parseSceneRecords()` (pure data, no `EntityRegistry`) only validates
    that every non-empty `"parent"` name matches some *other* record's
    `"name"` **somewhere in the file**, in a second pass after the main
    per-entity parsing loop -- it doesn't (and can't) resolve an `EntityId`
    yet. `loadScene()` (the `EntityRegistry`-facing half) does the actual
    resolution, in its own second pass after every record's entity has been
    created, via a `name -> EntityId` map built during the first pass. This
    two-pass shape is required specifically *because* forward references are
    allowed; `assets/scenes/default.json` demonstrates it for real, not just
    in a test (see below).
  - **A `"parent"` naming an entity absent from the whole file throws**
    `std::runtime_error` (after `LOG_ERROR`) at the `parseSceneRecords()`
    layer -- this project's established "validate at the boundary, fail with
    a specific message" convention, same treatment every other malformed-
    input case in that function already gets.
  - **A cycle of `"parent"` names** (`A` parents `B`, `B` parents `A`) passes
    this existence-only schema validation (both names genuinely exist) --
    catching that is `resolveWorldMatrix()`'s job at read time, not this
    schema layer's; see the cycle-safety bullet above.
  - **`saveScene()`** round-trips `"parent"` back to the *other* entity's own
    name, but only when that entity still resolves to a live `Transform` --
    a dangling `Parent` (not reachable yet, see above) is deliberately
    **omitted** from the saved file rather than round-tripping a name that
    would just fail `parseSceneRecords()`'s own validation on the next load.
- **`assets/scenes/default.json`** gains one new entity,
  `"parented_demo_cube"` -- reuses the existing `falling_cube.obj` mesh
  (scaled to 0.4x, not itself physics-simulated: no `"rigidBody"`/`"collider"`
  block of its own) parented to the existing `"falling_cube"` entity, at a
  small local offset (`[0.5, 0.3, 0.0]`) so it renders as a smaller cube
  riding just off to the side of the real, gravity-simulated one. This is
  deliberately the *physics-riding-along* case `physics.cpp`'s own Phase 14b
  comment describes, not just a parenting demo: `falling_cube` is the one
  entity in this scene with a `RigidBody`, so parenting under it is what
  actually exercises "a static entity's world position tracks a moving
  physics body because `resolveWorldMatrix()` runs at render time, after
  `stepPhysics()` already moved the parent's `Transform` for that frame" --
  parenting under a second static entity (e.g. `"scene"`) would only exercise
  name resolution, not that interaction. Deliberately listed **before**
  `"falling_cube"` in the `"entities"` array (`"falling_cube"` is the last
  entity in the file), so the shipped scene file itself demonstrates the
  forward-reference case above, not only a synthetic test. `falling_cube`'s
  own physics demo (position, velocity, collider) is untouched.
- **Tests, 3 -> 4**:
  - `tests/scene_serialization_test.cpp` gained a `"parent"` round-trip case
    (a 4th/5th record pair, deliberately pushed **child-before-parent** into
    the vector so `writeSceneRecords()` preserves that same file order,
    exercising the two-pass resolution above rather than only the case where
    files happen to list parents first) and a negative case (a `"parent"`
    naming nothing in the file throws).
  - `tests/transform_hierarchy_test.cpp` (new): a hand-computable 3-level
    hierarchy (root -> middle -> leaf, each level given a **non-identity
    position, rotation, and scale**, not just a translation, so a wrong
    composition order or a dropped rotation/scale would show up as a
    mismatch) asserted against `expectedWorld = root.local * middle.local *
    leaf.local`, computed independently of `resolveWorldMatrix()`'s own
    implementation via plain `glm` matrix multiplication; an entity with no
    `Transform` resolving to identity; a dangling-parent case; a two-entity
    cycle case; and a chain deliberately longer than `kMaxParentChainDepth`
    -- for both of the last two, the real assertion is that the test binary
    *returns at all* rather than hanging or crashing (`ctest` would report a
    timeout/crash otherwise), with a finite-matrix check as a secondary
    sanity check on top.
  - **A real bug caught by writing the hierarchy test, worth recording**:
    the first version of the 3-level-hierarchy test held the `Transform&`
    reference each `addComponent<Transform>()` call returns across the
    *next* `addComponent<Transform>()` call on the same registry/pool. Per
    `ecs.hpp`'s own `ComponentPool<T>` design, `add()` can reallocate its
    backing `std::vector<T>` on any insertion into the same pool, silently
    invalidating an earlier call's returned reference -- exactly the hazard
    every real call site in this engine already avoids by using the
    reference immediately and never keeping it past a sibling entity's own
    `addComponent<Transform>()` call. The symptom was every hand-computed
    expectation failing with plausible-looking-but-wrong numbers, not a
    crash (nothing here is checked/sanitizer-instrumented for
    use-after-realloc on its own). Fixed by capturing each level's own
    `glm::mat4` immediately after setting its fields, before any sibling
    `addComponent<Transform>()` call could invalidate the reference it came
    from -- not by changing `ecs.hpp` itself, since every non-test call site
    in this engine already follows the "use immediately, don't hold" rule
    this test had violated.
- **Verify**: a clean `-DCMAKE_BUILD_TYPE=Debug` rebuild compiles with
  **zero warnings** under `-Wall -Wextra`. `ctest` reports **4/4 passing**
  (`scene_serialization_test`, `input_action_map_test`, `physics_test`,
  `transform_hierarchy_test`) -- see the note above on how the new test's
  own bug was caught and fixed before it ever reached this point.
  `ENGINE_MAX_FRAMES=90 bash tools/run_headless.sh` completes
  cleanly with zero `[ERROR]` lines and `Frustum culling: 0/14 drawables
  culled` (up from 13 pre-this-phase, the one new entity). Because Phase
  14a's always-on dockspace shell now covers the entire window in the
  ordinary screenshot, the render itself was additionally verified with
  `EditorUI::renderDockspaceShell()`'s call temporarily commented out for a
  manual capture only (reverted before committing -- `git diff
  src/application.cpp` was checked afterward to confirm no trace of that
  temporary change remains): the new cube renders clearly, sitting in open
  ground space near the pyramid, correctly offset **and rotated** by the
  parent `scene` entity's own ~12° Y rotation (confirmed both visually and
  by logging the resolved world matrix's translation column during manual
  iteration: local `(-2.2, 0.25, 1.4)` resolved to world
  `(-1.860848, 0.25, 1.826813)` -- the same magnitude in the XZ-plane
  (confirming a rotation, not a translation-only bug: `sqrt(2.2^2 + 1.4^2) ==
  sqrt(1.860848^2 + 1.826813^2)`, both ~2.608). A from-scratch rebuild of
  pre-14b commit `3ee24e9` in a separate
  `git worktree`, captured the same way, pixel-diffed against this phase's
  own capture at zero (>15-intensity-unit) differing pixels outside one
  small, localized region matching exactly where the new cube and its
  shadow appear -- confirming the rest of the scene (table/box/pyramid,
  ground, lighting, and, separately, the falling-cube physics demo, whose
  own position/velocity/collider are byte-identical in `default.json`) is
  pixel-unaffected. `ENGINE_LEGACY_SCENE=1` was separately confirmed to
  still run cleanly (12 drawables, no parenting demonstrated -- unchanged
  from before this phase, per that flag's own documented "isolate scene-
  loading problems, not scene-content problems" scope).
- **Post-14b bug-review fix: the demo data didn't match the demo it was
  documented as**. `physics.cpp`'s own Phase 14b comment (see above) says
  `"parented_demo_cube"` is parented to `"falling_cube"` "for exactly that
  demonstration" -- i.e. a static entity visually riding along with a
  *moving, physics-simulated* one, the whole point of the paragraph it sits
  in. The `default.json` this phase actually shipped instead parented it to
  `"scene"` -- a second static entity with no `RigidBody` at all, so nothing
  in that file exercised the physics-riding-along interaction the comment
  claims demonstrates it (it still validly exercised forward-reference name
  resolution, just not the physics case). Two ways to close that gap:
  reword the comment to say `"scene"`, or repoint the data to `"falling_cube"`
  as the comment already said. Fixed by the latter -- `"scene"` is a strictly
  weaker demonstration (an entity riding along with something that never
  moves proves nothing about the render-after-physics ordering
  `resolveWorldMatrix()`/`stepPhysics()` depend on), so matching the data to
  the comment's original intent is strictly more thorough than matching the
  comment to the data, and both are made consistent either way. Also
  shrunk the child's scale (0.6x -> 0.4x, clearly smaller than its
  gravity-simulated parent) and local offset (to `[0.5, 0.3, 0.0]`, a small
  offset that reads as "attached to" rather than "coincidentally near" the
  parent) now that the offset is relative to a moving cube rather than a
  fixed point in the room. `"parented_demo_cube"` is still listed **before**
  `"falling_cube"` in the file (`"falling_cube"` is now the last entity), so
  the forward-reference demonstration this phase's own tests/comments
  describe is unchanged. Re-verified headlessly with the same
  `renderDockspaceShell()`-disabled-then-reverted capture technique as the
  original phase (confirmed reverted via `git diff src/application.cpp`
  showing no changes to that file), at three wall-clock-timed captures
  (~2s, ~10s, ~18s after launch): the smaller cube visibly tracks
  `falling_cube` downward frame-to-frame and comes to rest attached to its
  upper corner once `falling_cube` settles on the ground plane -- the
  demonstration `physics.cpp`'s comment describes is now real, not just
  documented. `ctest` still reports 4/4 passing and
  `tools/run_headless.sh` still completes with zero `[ERROR]` lines and
  `0/14 drawables culled` (the JSON edit changes values, not entity/model
  count).
  - **Independently re-verified, no other bugs found**: hand-derived the
    3-level-hierarchy matrix composition
    (`resolveWorldMatrix(leaf) = root.local * middle.local * leaf.local`,
    root-then-descendant left-to-right) against `transform_hierarchy.cpp`'s
    actual fold direction (`chain` collected leaf-to-root, then folded via
    `rbegin()..rend()` -- root-to-leaf) and confirmed the multiplication
    order is genuinely parent-then-local, not reversed. Constructed an
    additional adversarial 3-entity cycle (`A.parent=B, B.parent=C,
    C.parent=A`, none of which is a 2-cycle the existing test already
    covers) by hand-tracing the algorithm rather than as new committed test
    code: each of the three possible starting points terminates via the
    visited-id check and warns once for a different "offending" entity (the
    one whose own `Parent` link closes the loop from that starting point),
    confirming the guard generalizes past the 2-entity case the shipped test
    exercises. Confirmed the self-parent case (an entity naming itself as
    `"parent"`) passes `parseSceneRecords()`'s name-existence check (a name
    trivially matches itself) and is instead caught by
    `resolveWorldMatrix()`'s general visited-set guard on the very first
    loop iteration (`current`'s own index is already in `visited` before the
    walk starts) -- no special-cased self-parent check was needed or is
    missing. Confirmed the warned-once log dedup sets are keyed by the
    *offending* entity's index in all three cases (cycle, dangling-parent,
    max-depth), not the id originally passed in, so a second, distinct
    offending entity is never incorrectly suppressed by an earlier warning
    -- and confirmed by inspection these sets are never cleared or bounded,
    which is fine only because there's no entity-destruction path yet to
    make an entity index repeat across the same run (already the exact
    caveat `transform_hierarchy.hpp`'s own header comment names). Confirmed
    all three `application.cpp` render call sites
    (`renderShadowPass()`/`renderSSAO()`/`render()`) consistently call
    `resolveWorldMatrix()` for every `ModelComponent` entity -- the
    `SphereInstance` procedural grid's own `instance.transform.getModelMatrix()`
    calls in those same functions are a separate, non-ECS system with no
    `Parent` support and are correctly left alone, not a missed call site.
    Confirmed duplicate entity names interacting with name-based `"parent"`
    lookup behave exactly as `scene_loader.cpp`'s own comment states: the
    last-created record with a given name wins `loadScene()`'s
    `idByName` map, so a `"parent"` naming an ambiguous (duplicated) name
    resolves to whichever of those entities happened to be parsed last --
    the same pre-existing "names aren't unique" looseness Phase 8b's own
    review already found, now also reachable through `"parent"` rather than
    a new problem this phase introduced.

### Phase 14c: real render-to-texture Viewport panel

Third sub-phase of the "Phase 14: full editor UI" arc. Phase 14a's Viewport
panel was placeholder text floating over a 3D scene still rendered straight
to the window's default framebuffer, unaffected by the dockspace at all; this
phase makes that real -- the 3D scene now renders into an off-screen color
target sized to the Viewport panel's own on-screen rectangle, displayed
inside that panel via `ImGui::Image()`. This is the deferral Phase 14a's own
commit explicitly called out ("live resize... can make the final on-screen
image look visually stretched... until those buffers are themselves
resize-aware -- deferred to Phase 14c's render-to-texture viewport work");
that deferral ends here.

- **What "the render resolution" means, redefined**: before this phase, every
  offscreen render target this engine owns (`hdrFramebuffer_`,
  `hdrResolveFramebuffer_`, bloom's `brightFramebuffer_`/
  `pingpongFramebuffer0_`/`1_`, SSAO's `ssaoGBuffer_`/`ssaoRaw_`/
  `ssaoBlurred_`) was sized once, at construction, from `window_.getSize()` --
  the OS window's own real framebuffer size -- and the final tonemap pass
  wrote straight onto the default framebuffer at that same size. After this
  phase, all of that tracks two new `Application` members,
  `viewportWidth_`/`viewportHeight_`: the Viewport ImGui panel's own content-
  region size, which is almost always smaller than (and a different aspect
  ratio than) the whole window, since Scene/Assets/Inspector also occupy
  part of it. The window's real size still matters for exactly one thing:
  ImGui's own chrome (the dockspace host + all four panels), rendered
  separately, at the very end of `render()`, straight onto the default
  framebuffer at `window_.getSize()` -- unrelated to the 3D pipeline's own
  resolution from this phase on.
- **Why last frame's size, not this frame's -- and why that's correct, not a
  bug**: `Application::render()` needs to know the Viewport panel's size
  *before* it does any 3D rendering work this frame (so it can size/resize
  every render target and pick the right projection aspect ratio), but Dear
  ImGui only reports a panel's actual laid-out content-region size *while*
  that panel's own `ImGui::Begin()`/`End()` runs -- and this engine's own
  panel-submission call (`editorUI_.renderDockspaceShell()`) has always run
  at the *tail* of `render()`, after the whole 3D pass, so that ImGui's
  chrome lands on top of the already-tonemapped image. Restructuring so the
  ImGui frame starts *before* the 3D pass would just relocate the same
  chicken-and-egg problem (you'd then need the Viewport panel's `ImGui::Image()`
  call, submitted before the 3D render that frame has even happened, to show
  *this* frame's still-nonexistent output) without actually removing the one
  frame of latency anywhere in the loop. So `EditorUI` now exposes
  `viewportWidth()`/`viewportHeight()` -- the panel's own
  `ImGui::GetContentRegionAvail()`, recorded every time
  `renderDockspaceShell()` runs -- and `Application::render()` reads those at
  its very top, before its 3D pipeline, applying *last* frame's reported size
  to *this* frame's render. The panel's size only actually changes when a
  user drags a divider or redocks a panel (rare after the initial layout
  settles), so in practice this reads as "pick a size once, keep using it" --
  the same one-frame-stale "read last frame's laid-out size, size this
  frame's render target to it" pattern every real engine editor's own
  render-to-texture viewport uses (Unity, Unreal, Godot all have this same
  property), not a defect specific to this implementation.
- **`resizeViewportTargetsIfNeeded()` (`Application`, called first thing in
  `render()`)**: reads `editorUI_.viewportWidth()`/`viewportHeight()`; if
  either is `> 0`, adopts it into `viewportWidth_`/`viewportHeight_` (0 means
  "the Viewport panel hasn't rendered a single frame yet, or a user dragged a
  divider all the way shut" -- see the degenerate-size note below); either
  way, clamps both to `>= 1` via `std::max` before anything else touches
  them, since a 0-sized FBO is undefined/invalid in OpenGL. Then, only if
  that size actually differs from `hdrFramebuffer_`'s own current
  `width()`/`height()` (used as this whole group's single "are we already
  built right" reference point, since every target in the group is always
  rebuilt together), move-assigns a freshly constructed `Framebuffer` over
  every one of: `hdrFramebuffer_`, `hdrResolveFramebuffer_`,
  `brightFramebuffer_`/`pingpongFramebuffer0_`/`1_` (at
  `viewportWidth_ / kBloomDownsampleFactor` etc., `std::max(1, ...)`-clamped),
  `ssaoGBuffer_`/`ssaoRaw_`/`ssaoBlurred_` (same clamp, `kSSAODownsampleFactor`),
  and the new `viewportColorFramebuffer_` (below) -- then calls
  `recomputeClusterAABBs()` (next point). `Framebuffer`'s own pre-existing
  move-assignment operator (Phase 7b) does exactly the right thing here: it
  frees the old instance's GL handles before taking ownership of the new
  one's, so "rebuild at a new size" is just "construct a temporary at the new
  size, move-assign it over the member" -- no new `Framebuffer` API needed at
  all. Shadow maps were checked and confirmed independent of any of this (see
  the Phase 14a-era header comment on `shadowCascades_`/`ShadowMap`): they're
  a fixed resolution unrelated to window or viewport size, so nothing about
  them changes this phase.
- **Cluster light culling's AABBs are no longer a true one-time
  precompute**: `ClusterLightCuller::computeClusterAABBs()`'s own header
  comment (Phase 13d) reasoned that a cluster's view-space AABB is a pure
  function of the projection matrix + screen size in pixels, and since this
  engine's window size never changed after construction, that meant
  "computed once, ever" was correct. The projection's aspect ratio (and the
  screen size fed alongside it) is exactly what changed meaning this phase --
  it's now `viewportWidth_`/`viewportHeight_`'s aspect, not the window's --
  so a stale AABB set built against the wrong screen size would silently
  desync from `basic.frag`/`pbr.frag`'s own `gl_FragCoord`-based cluster-index
  computation (which *does* now use the new, correct `uScreenSize`), making
  per-fragment shading sample the wrong cluster's light list. Fixed by
  factoring the old constructor-only computation out into a new
  `Application::recomputeClusterAABBs()` method, called both from the
  constructor (seeding the very first, placeholder-sized AABB set, exactly as
  before) and from `resizeViewportTargetsIfNeeded()` every time the viewport
  actually resizes -- in practice, this means it runs twice total in a normal
  run: once at construction (against the window's initial size, before
  `editorUI_` has ever reported a real panel size) and once more the first
  time the real Viewport panel size is known (frame 1's
  `resizeViewportTargetsIfNeeded()` call, per the point above), then never
  again unless a user actually resizes the panel later.
- **A new dedicated Framebuffer, `viewportColorFramebuffer_`**: the final
  tonemap/bloom-composite postprocess pass used to write straight onto the
  default framebuffer (`glBindFramebuffer(GL_FRAMEBUFFER, 0)`, at the
  window's own size); it now writes into this new, single-sample, no-special-
  flags `Framebuffer` instead, sized at `viewportWidth_`/`viewportHeight_` --
  the same shape as `brightFramebuffer_`/`pingpongFramebuffer0_`/`1_` (Phase
  11), just full-resolution rather than downsampled. `Framebuffer` gained one
  small new accessor for this phase, `colorTextureId()` -- the raw GL texture
  handle, not bound to any texture unit -- since every existing accessor
  (`bindColorTexture(unit)`) exists to bind the texture as a side effect of
  "a shader is about to sample it", and `ImGui::Image()` instead needs the
  bare id/`ImTextureRef` directly; this is the first `Framebuffer` consumer in
  this engine that isn't one of this engine's own shaders.
- **Displaying it -- `EditorUI::renderDockspaceShell()` gained a parameter**:
  now takes the viewport color texture's raw GL id, and the Viewport panel's
  body calls `ImGui::Image(id, contentRegion, uv0, uv1)` sized to fill
  `ImGui::GetContentRegionAvail()` -- filling the panel exactly, whatever its
  current size -- instead of `TextWrapped()`. **The UV flip gotcha, confirmed
  visually, not assumed**: OpenGL's texture coordinate origin is the
  bottom-left texel (the convention every one of this engine's own fullscreen
  shader passes already relies on -- e.g. `postprocess.frag` sampling
  `hdrResolveFramebuffer_` with unflipped `[0,1]` UVs has always reproduced
  the scene right-side-up), but Dear ImGui's `ImGui::Image()` treats UV
  `(0,0)` as an image's top-left corner, the same convention its own font
  atlas uses. Handing it this engine's render target with the default
  `uv0=(0,0)/uv1=(1,1)` would display the 3D scene upside down; passing
  `uv0=(0,1)/uv1=(1,0)` instead flips it back to right-side-up. Caught and
  fixed by actually looking at the first headless screenshot taken with this
  change (it showed the scene correctly oriented once the flip was in place,
  inverted without it) rather than assuming the "obvious" default UVs were
  correct -- exactly the kind of check this phase's own brief called out as
  the highest-risk, most important thing to verify empirically.
- **The degenerate-size guard, and the documented choice for what happens
  visually**: the Viewport panel's content region can be `0` in some
  dimension on the very first frame (before ImGui's docking layout has
  settled) or if a user drags a panel divider all the way shut.
  `resizeViewportTargetsIfNeeded()` handles this by *not updating*
  `viewportWidth_`/`viewportHeight_` at all when `editorUI_`'s reported size
  is `<= 0` in either dimension -- keeping whatever size those members
  already held (seeded at the window's own initial size by `Application`'s
  in-class default member initializers, so this is never actually `0` even
  on frame one) -- rather than ever clamping down to a jarring `1x1` render
  or skipping the 3D render entirely and showing nothing. The chosen behavior
  is therefore: **keep rendering at the last known-good size**; the Viewport
  panel's displayed image simply doesn't change that frame. `EditorUI`'s own
  `ImGui::Image()` call has an independent, narrower guard for the same
  underlying condition -- it skips the call (leaving the panel body blank)
  only if the *panel's own* content region is currently `<= 0`, which can
  only actually happen before `Application` has ever rendered a single valid
  frame to hand it a texture.
- **`uScreenSize` and the aspect ratio -- every reference to the window's
  size in the 3D pipeline itself is gone**: `render()`'s aspect ratio
  (feeding `Camera::getProjectionMatrix()`), the CSM cascade split
  computation, the `glViewport()` calls bracketing the shadow pass, and both
  `shader_`/`pbrShader_`'s own `uScreenSize` uniform uploads (clustered
  lighting's per-fragment cluster-index computation) all switched from
  `window_.getSize()` to `viewportWidth_`/`viewportHeight_`. The one
  remaining `window_.getSize()` read in `render()` is the new tail block that
  rebinds the default framebuffer and clears it (an arbitrary neutral dark
  gray, visible only in any thin gaps between docked panels) before
  `editorUI_`'s own ImGui calls draw the chrome -- that one genuinely does
  need the window's real size, since ImGui's chrome covers the whole window,
  not just the Viewport sub-region.
- **Verify**: a clean `-DCMAKE_BUILD_TYPE=Debug` rebuild compiles with
  **zero new warnings** under `-Wall -Wextra`; `ctest` still reports all
  **4/4** tests passing (`scene_serialization_test`, `input_action_map_test`,
  `physics_test`, `transform_hierarchy_test` -- this phase touches no tested
  logic). `tools/run_headless.sh` at `ENGINE_MAX_FRAMES=60` (800x600 window,
  same as every prior phase's headless path) completed cleanly with **zero
  `[ERROR]`/GL-error log lines** across repeated runs, logging
  `Viewport resized: rebuilding offscreen render targets at 431x565 (was
  800x600)` on frame 1 -- confirming the Viewport panel's real reported
  content-region size at this window size and dockspace-column-width split
  (left column 22%, right 28%, leaving roughly the center 50% minus panel
  padding/borders for Viewport) is **431x565**, with bloom/SSAO's own
  downsampled targets correctly rebuilt at half that, **215x282**. The
  resulting screenshot is a real, dense, colorful render again --
  **~262KB**, back in the same size range every pre-Phase-14a phase's own
  screenshot was (Phase 14a/14b's own screenshots, with the Viewport panel
  still just flat dark background, were ~11-13KB) -- and, inspected
  directly: the 3D scene (checkerboard ground plane, the dark table with its
  red-and-black pyramid and blue box, the 2x4 PBR sphere grid, all correctly
  lit with visible soft shadow gradients under the table/pyramid/box and
  contact-AO darkening under each sphere, and a soft bloom halo around the
  point light source) renders right-side-up, correctly proportioned (round
  spheres, not stretched into ovals by an aspect mismatch) inside the
  Viewport panel's own rectangle, while Scene/Assets/Inspector around it
  still show their own unchanged Phase 14a/14b placeholder text. A second run
  with `ENGINE_SHOW_DEBUG_UI=1` confirmed `DebugUI`'s own "Engine Debug"
  panel (Frame Stats/Render Passes/Input Bindings/Scene Entities, listing
  `parented_demo_cube`/`scene`/`falling_cube`) still layers correctly on top
  of the dockspace, unaffected in content, exactly as Phase 14a left it.
- **Post-14c bug-review fix: a small enough window crashed the process at
  startup, before a single frame ever rendered**. The constructor's own
  initializer list built `brightFramebuffer_`/`pingpongFramebuffer0_`/`1_`
  and `ssaoGBuffer_`/`ssaoRaw_`/`ssaoBlurred_` at `viewportWidth_ /
  kBloomDownsampleFactor` etc. *without* the `std::max(1, ...)` floor
  `resizeViewportTargetsIfNeeded()`'s own copy of this same division already
  had, on the theory (recorded in this phase's original commit as a code
  comment) that "this engine has never run at a window size small enough for
  `/ 2` of that to reach 0, so no clamp is needed on this one-time initial
  value." That theory was never actually tested against this engine's own
  documented `ENGINE_WINDOW_WIDTH`/`ENGINE_WINDOW_HEIGHT` override knobs
  (Phase 14a) -- `main.cpp`'s own validation for both only rejects `<= 0`,
  so `ENGINE_WINDOW_WIDTH=1` (or `=1` on either dimension) is an explicitly
  supported, accepted value, and `viewportWidth_`/`viewportHeight_` seed
  directly from `window_.getSize()` before any resize-path clamp ever runs.
  At width (or height) `1`, `1 / kBloomDownsampleFactor` (`2`) floors to `0`
  in the constructor's own unclamped initializer, so `Framebuffer`'s
  constructor builds a `0`-sized FBO, `glCheckFramebufferStatus()` never
  reports it complete, and `Framebuffer` throws -- crashing the whole
  process (`Fatal: Framebuffer: HDR framebuffer incomplete`) before
  `Application::run()`'s main loop ever starts. Confirmed by actually
  running `engine_app` at `ENGINE_WINDOW_WIDTH=1 ENGINE_WINDOW_HEIGHT=1`
  (and separately at `1x600`/`600x1`) before this fix -- all three crashed
  the same way. Fixed by factoring the division into one shared
  `clampedDownsampleDimension(dimension, factor)` helper
  (`std::max(1, dimension / factor)`) used by *both* the constructor's own
  initializer list and `resizeViewportTargetsIfNeeded()`, closing the gap
  between the two instead of leaving the constructor as a second,
  un-clamped copy of logic the resize path already had right. Re-verified:
  `ENGINE_WINDOW_WIDTH=1 ENGINE_WINDOW_HEIGHT=1` (and `1x600`/`600x1`) now
  run cleanly to completion with zero `[ERROR]` log lines; a normal
  `800x600` run still resizes to `431x565` exactly once (no regression to
  the steady-state "resize once, then stable" behavior above); `ctest`
  still 4/4.

<br>

<details>
<summary><strong>Everyday shorthand vs. what's actually true</strong> (click to expand)</summary>
<br>

Saying "the Viewport panel renders the 3D scene" is the natural way to
describe this phase, and it's how the rest of this section talks about it --
but it can make it sound as though `EditorUI`/Dear ImGui does the rendering,
or that the panel's own size takes effect immediately. Neither is quite
right: `EditorUI` only ever displays a texture `Application` already finished
rendering elsewhere (`viewportColorFramebuffer_`, built by the ordinary
shadow/SSAO/HDR/bloom/SSR/tonemap pipeline exactly as before, just now
writing to this new target instead of the default framebuffer) -- it has no
3D rendering code of its own, then or now. And "the panel's size" driving
that render is always *last* frame's reported size, one frame stale, for the
structural reason explained above -- there is no way, within Dear ImGui's
immediate-mode API as this engine uses it, for a frame's own 3D render to be
sized from a content-region query that hasn't happened yet within that same
frame.
</details>

### Phase 14d: a real Scene Hierarchy panel, click-to-select, and a viewport selection outline

Fourth sub-phase of the "Phase 14: full editor UI" arc. The Scene panel's
placeholder text ("Scene Hierarchy -- coming in Phase 14d") is replaced by a
real, clickable tree built from `registry_`'s actual entities, and selecting
a row now draws a dashed-rectangle-plus-corner-brackets outline around that
entity in the Viewport panel. Still no Inspector content (Phase 14e) and no
right-click Create/Delete (Phase 14f) -- this phase is scoped to the tree
itself, selecting a row, and the outline that selection drives.

- **Real nesting, not a flat "folder" label**: Phase 14b's own commit
  explicitly chose real `Parent`-component parent/child transform grouping
  over a flat, non-transforming organizational "folder" concept (see that
  phase's own README section) -- this phase's tree honors that choice
  exactly. `buildSceneTree()` (`include/engine/scene_hierarchy.hpp`/
  `src/scene_hierarchy.cpp`, new) walks `registry_`'s actual `Parent`
  components to build a forest of `SceneTreeNode`s; an entity with children
  parented to it *is* the tree's own "folder" -- there is no second,
  invented grouping concept alongside it. This mirrors
  `resolveWorldMatrix()`'s own reading of `Parent` (`transform_hierarchy.hpp`)
  almost exactly, just building a tree to display instead of composing a
  matrix to draw with, including the same "dangling/self-referential Parent
  doesn't crash" posture:
  - A `Parent` pointing at an id this same call doesn't also see (dangling,
    or something with no `Transform`) is treated as "this entity is a root"
    -- the same fallback `resolveWorldMatrix()` gives a dangling parent.
  - A cycle of `Parent` links (of any length) is broken at whichever entity
    in the cycle `buildSceneTree()` happens to reach first: that one becomes
    an extra top-level root, and the rest of the cycle nests normally
    beneath it until the link back to the first entity is reached and
    simply dropped instead of re-adding a node for it. There's no single
    "correct" tree shape for a cycle -- the guarantee that actually matters,
    and the one this phase's own test enforces, is that every entity
    `registry.each<Transform>()` visits appears in the returned forest
    **exactly once**, and building the tree always terminates.
  - **A real bug caught by this phase's own new unit test**: the first
    implementation marked an id "visited" only once `build()` had already
    started recursing into it, which let a two-entity cycle (A parents B, B
    parents A) produce THREE nodes instead of two -- A as a root, B nested
    under it, and a second, duplicate copy of A nested under B, instead of
    being dropped there. Fixed by marking an id visited at the exact moment
    it's *claimed* (by whichever caller -- a root-level loop iteration, or
    `build()`'s own per-child loop -- reaches it first), strictly before
    recursing into it, so a second attempt to claim the same id anywhere
    else in the same call always finds it already taken and skips it,
    rather than only guarding re-entry into a call already in progress.
  - Every entity with a `Transform` component is a row (not just ones with a
    `ModelComponent`) -- the same enumeration `Application::renderDebugUI()`'s
    own pre-existing "Scene Entities" panel already uses, since a `Parent`
    only makes sense on something `resolveWorldMatrix()` can resolve a world
    matrix for in the first place, and a future Transform-only entity (an
    empty grouping node, or a future light/camera entity) is exactly the
    kind of thing a real hierarchy panel still needs to list.
  - Naming matches `saveScene()`'s own placeholder convention exactly
    (`scene_serialization.hpp`): an entity's `NameComponent` if it has one,
    else `"entity_<index>"`.
  - `scene_hierarchy.cpp` depends only on `ecs.hpp`/`transform.hpp`/
    `transform_hierarchy.hpp`'s `Parent` struct -- none of them GL- or
    ImGui-touching -- the same "pure logic, its own small file" split
    `transform_hierarchy.cpp`/`physics.cpp` already established, which is
    what let `tests/scene_hierarchy_test.cpp` (new; `ctest` is now **5/5**)
    exercise the nesting/naming/dangling-parent/cycle-safety logic above
    directly, without a live GL context or Dear ImGui frame -- the same
    "plain executable, links only the file it's testing" shape as
    `transform_hierarchy_test`/`physics_test`.
- **Click-to-select, drawn via ordinary `ImGui::TreeNodeEx()`**:
  `EditorUI::renderDockspaceShell()` (`src/editor_ui.cpp`) walks the tree
  returned by `buildSceneTree()` every frame (a handful of entities, so
  there's no reason to cache/diff it against a previous frame) and renders
  each node as an `ImGui::TreeNodeEx()` -- `ImGuiTreeNodeFlags_Leaf` for a
  childless entity (no expand arrow), `ImGuiTreeNodeFlags_Selected` when it's
  the current selection (ImGui's own built-in highlight/left-accent styling
  -- this phase's brief explicitly doesn't require hand-matching the
  mockup's exact hex values, just "visibly selected"), and
  `ImGui::IsItemClicked()` on that same row to update the selection. No icon
  glyphs (folder/mesh/light/camera, per the approved mockup): Dear ImGui's
  default font (no custom font atlas is built anywhere in this engine) only
  carries the ASCII/Latin-1 glyph range, nowhere near an icon font's Unicode
  private-use/symbol code points -- vendoring one is real, separate scope
  this phase's brief doesn't ask for. Indentation, the tree-node's own
  expand/collapse arrow, and the selected-row highlight carry "group vs.
  leaf" and "this row is selected" instead -- functionally equivalent to the
  mockup's own icons/highlight for this phase's actual bar (a real tree +
  click-to-select), just without pixel-identical iconography.
- **Where the selection lives, and why**: a new `std::optional<EntityId>
  selectedEntity_` member on `Application` (`std::nullopt` = no selection,
  this phase's own documented default/starting state) -- not on `EditorUI`,
  even though `EditorUI` is the class that actually mutates it in response to
  a click. `EditorUI` is, and has been since Phase 14a, "just a Dear ImGui
  wrapper" over data `Application` owns (`registry_`, the viewport texture,
  ...) -- `selectedEntity_` gets the exact same treatment: `renderDockspaceShell()`
  gained `EntityRegistry& registry` and `std::optional<EntityId>&
  selectedEntity` parameters (passed by reference so a click inside that one
  call updates `Application`'s own member directly), and `Application`
  exposes it back out via one const `selectedEntity()` getter -- not a
  mutable reference -- specifically so Phase 14e's real Inspector panel (this
  phase's own brief requires this be "exposed in a way a later phase can
  consume") can read what's currently selected without an awkward reach into
  private state or a second parallel copy of the same optional.
  - **One frame of latency, same shape and same reason as `viewportWidth_`/
    `viewportHeight_`** (Phase 14c): `render()` reads `selectedEntity_` and
    builds this frame's outline from it BEFORE calling
    `editorUI_.renderDockspaceShell()`, which is the call that could change
    `selectedEntity_` in response to a click on this same frame's Scene
    panel. So a newly-clicked row's outline first appears the *following*
    frame, not the same one -- there is no other order Dear ImGui's
    immediate-mode API supports here either: the 3D pass whose output the
    outline projects has to run before the one ImGui frame that could change
    what it's projecting even exists yet, exactly Phase 14c's own
    "chicken-and-egg" reasoning, just for selection state instead of panel
    size.
- **The outline's screen-space projection**: `Model` gained a new public
  `boundingSphere(rootTransform)` method (`model.hpp`/`model.cpp`) --
  aggregates every mesh's own local `BoundingSphere` (Phase 13b) across every
  node of the model (not just the root node's meshes; `scene.obj`'s "scene"
  entity alone has three, one per node) into one sphere: centered on the
  unweighted centroid of every individual mesh sphere's own center, radius
  set to the farthest any one mesh sphere's own surface reaches from that
  centroid. Not the tightest possible enclosing sphere (that's a harder,
  separate computational-geometry problem), but always a conservative
  superset of the model's real extent -- exactly the same bias
  `BoundingSphere::transformed()` already has for frustum culling, and
  exactly what this outline needs: a screen-space rectangle a little larger
  than the object's exact silhouette reads fine; one that's too small and
  clips into the object would not.
  - `Application::render()`'s new `computeSelectionOutlineNDC()` (local to
    `application.cpp`) takes the selected entity's `Model::boundingSphere()`,
    already transformed into world space by `resolveWorldMatrix()` (Phase
    14b -- so a parented entity's outline tracks its resolved WORLD
    position, matching where it's actually drawn, not its raw local one),
    and projects it using the "project center +/- radius along the camera's
    own view-right/view-up axes" technique this phase's own brief calls
    out: the sphere's center, and two more points offset from it by its own
    radius along `camera_.right()`/`camera_.up()` (two new `Camera` getters,
    `camera.hpp` -- both vectors already computed every frame,
    just not previously exposed), are each projected through the camera's
    view-projection matrix and divided by `w`; the resulting NDC offsets
    from the center's own NDC position become the rectangle's half-width/
    half-height. Chosen over unprojecting a world-space AABB's 8 corners for
    a concrete reason: this engine already has a per-entity `BoundingSphere`,
    not an AABB, and a sphere's own screen-facing silhouette IS exactly
    "center offset by radius along the two axes perpendicular to the view
    direction" -- an AABB's 8 corners would first need to be derived from
    this same sphere anyway, for a shape that doesn't need to be any tighter
    than the sphere already is. Returns "no meaningful outline this frame"
    (not a crash, not a wrong rectangle) if the center or either offset point
    projects behind the camera (`clip.w <= ~0`), the same "documented
    non-error case" treatment as "nothing selected".
- **Displaying it -- the same window's own draw list, not the global
  foreground list**: `EditorUI::renderDockspaceShell()` gained a `const
  SelectionOutline* outline` parameter (`nullptr` = no selection this frame,
  no rectangle drawn) and, right after the Viewport panel's `ImGui::Image()`
  call (Phase 14c), maps `outline`'s NDC rect onto that panel's own current
  on-screen pixel rectangle -- captured via `ImGui::GetCursorScreenPos()`
  (before `ImGui::Image()` advances the cursor) plus the same
  `ImGui::GetContentRegionAvail()` the image itself is sized to, since the
  Viewport panel does **not** fill the whole window (Scene/Assets/Inspector
  occupy the rest) -- and draws a dashed rectangle (`addDashedRect()`) plus
  four small L-shaped corner brackets (`addCornerBrackets()`, both new
  file-local helpers in `editor_ui.cpp`) via `ImGui::GetWindowDrawList()`.
  Deliberately the *window's own* draw list, not
  `ImGui::GetForegroundDrawList()`: both composite on top of `ImGui::Image()`
  either way (a window's own draw commands rasterize in submission order;
  the foreground list draws on top of every window), but only the window's
  own draw list is automatically clipped by Dear ImGui to that window's
  visible rectangle -- the foreground list isn't clipped to any one window
  at all, so a selection near the Viewport panel's own edge could otherwise
  paint a stray dashed-line fragment over whatever panel is docked next to
  it. Confirmed by this phase's own headless screenshot (see Verify below),
  not just assumed.
- **A verification aid: `ENGINE_DEBUG_SELECT=<entity name>`**: unset by
  default, same getenv-gated-off-by-default shape as every other `ENGINE_*`
  flag in this project, but -- unlike the boolean ones -- carries a *value*
  (the target entity's `NameComponent` string) rather than being a plain
  on/off switch. Headless Xvfb has no real mouse to click a Scene Hierarchy
  row with, so this pre-selects an entity by name once, in the constructor,
  after the scene has finished loading (so `findEntityByName()`'s linear
  search over the `NameComponent` pool has something to find) -- an
  unmatched name is logged as a warning and leaves the selection unset
  rather than crashing or pointing `selectedEntity_` at a bogus id. Kept in
  the shipped binary rather than removed after this phase's own review,
  matching every other verification-oriented `ENGINE_*` flag already
  documented in this file (`ENGINE_CAMERA_DEMO`, `ENGINE_FRUSTUM_CULL_DEMO`,
  `ENGINE_CLUSTER_DEBUG`, ...) -- a reusable regression-check tool, not
  one-off scaffolding.
- **Verify**: a clean `-DCMAKE_BUILD_TYPE=Debug` rebuild compiles with
  **zero new warnings** under `-Wall -Wextra`. `ctest` now reports **5/5**
  tests passing -- the 4 pre-existing tests plus this phase's own new
  `scene_hierarchy_test` (see above). `tools/run_headless.sh` at
  `ENGINE_MAX_FRAMES=60` completed cleanly with **zero `[ERROR]`/GL-error
  log lines** across repeated runs. Two screenshots were taken and inspected
  directly:
  - **Default (no selection)**: the Scene panel shows a real tree -- `scene`
    and `falling_cube` at the top level, `parented_demo_cube` visibly
    indented one level under `falling_cube` (matching Phase 14b's own
    parenting) -- with no row highlighted and no outline anywhere in the
    Viewport panel, exactly this phase's documented default state.
  - **`ENGINE_DEBUG_SELECT=falling_cube`**: the constructor logs
    `"ENGINE_DEBUG_SELECT=\"falling_cube\": pre-selecting entity 2"`; the
    Scene panel's `falling_cube` row renders with ImGui's selected-row
    highlight; and the Viewport panel shows a teal dashed rectangle with
    corner brackets tightly bounding `falling_cube`'s on-screen fragment --
    at this frame, most of the still-falling cube's own bounding sphere sits
    above the camera's visible frame, so only a sliver of its checkered
    underside pokes into view at the very top of the Viewport panel, and the
    outline's own top edge is correctly clipped exactly at the panel's own
    top border (not bleeding upward into the Scene panel or the window's
    title/menu area) while its bottom/left/right edges tightly bound the
    visible sliver -- confirming both that the projection lands in the right
    place at the right size, and that the window-draw-list clipping
    reasoning above holds up in practice, not just in theory.
  - A third run with `ENGINE_SHOW_DEBUG_UI=1` confirmed `DebugUI`'s own
    "Engine Debug" panel (Frame Stats/Render Passes/Input Bindings/Scene
    Entities) still layers correctly on top of the dockspace, listing all
    three entities, unaffected in content -- exactly as every prior Phase 14
    sub-phase left it.
- **Post-14d bug-review fix: `buildSceneTree()` had no depth bound, unlike
  the `resolveWorldMatrix()` guard it was meant to mirror**. This phase's own
  cycle-safety design (above) explicitly set out to mirror
  `resolveWorldMatrix()`'s two guards (`transform_hierarchy.hpp`) -- and did,
  correctly, for cycles: hand-tracing both a 2-entity cycle (A parents B, B
  parents A) and a 3-entity cycle (A parents B, B parents C, C parents A)
  through `Builder::build()`'s actual code confirms each yields the right
  node count with no duplicates. But `resolveWorldMatrix()` has a *second*,
  independent guard beyond its own visited-set cycle check --
  `kMaxParentChainDepth` (64), enforced via an iterative walk specifically
  *not* real recursion, "regardless of how deep/cyclic a malformed chain is"
  (that file's own comment) -- and `buildSceneTree()`'s `Builder::build()`
  had no equivalent: it recurses with a genuine C++ call per tree level, so a
  parent chain deeper than 64 (or a cycle longer than 64) would recurse the
  real call stack that deep too, risking an actual stack overflow that
  `resolveWorldMatrix()` was specifically designed to never risk for the same
  conceptual input. Today's scenes only nest one level deep, so this was
  latent, not reachable -- but per this project's own established precedent
  (designing the cycle/dangling-parent guards themselves for a
  not-yet-reachable case, since there's no entity-destruction or reparenting
  UI before Phase 14f either), a hand-edited scene file or a later
  reparenting feature could reach it. Fixed by threading a `depth` parameter
  through `build()` and reusing `kMaxParentChainDepth` (imported from
  `transform_hierarchy.hpp`, not re-declared) as the same bound: a child
  beyond the depth limit is left unclaimed rather than recursed into,
  letting the function's own pre-existing "claim any still-unvisited id as
  an extra root" pass (previously only reached by cycles) pick it up and
  give it a fresh depth budget of its own -- proven correct by hand-tracing
  that a single linear pass over `ids` still claims every entity exactly
  once regardless of chain length or creation-order/chain-order mismatch,
  the same "no single correct tree shape, but every entity appears exactly
  once" guarantee the cycle guard already provided. A new
  `scene_hierarchy_test` case (mirroring `transform_hierarchy_test`'s own
  "Deep-chain guard" case exactly) builds a `kMaxParentChainDepth + 20`-deep
  non-cyclic chain and confirms all entities still appear exactly once,
  split across more than one top-level forest entry.
- **Post-14d bug-review fix: the outline's screen-space rectangle had no
  upper bound before reaching `addDashedRect()`'s per-segment loop**.
  `computeSelectionOutlineNDC()`'s `clip.w <= 1e-4f` guard only rejects a
  point essentially at or behind the camera -- it does nothing for a
  selected entity's bounding sphere merely being *very close* to the camera,
  which nothing prevents (this engine's free-fly camera has no
  camera-vs-scenery collision, so it can fly right up to or through a
  selected object). A small-but-positive `w` there is a perfectly finite
  divide -- never NaN/Inf -- but can inflate an ordinary-sized offset into
  the thousands, and that finite-but-huge NDC rect reached
  `addDashedRect()`/`addCornerBrackets()` (`editor_ui.cpp`) completely
  unclamped: their own cost is proportional to the rectangle's on-screen
  perimeter, so an unbounded rect could turn one frame's dashed-outline draw
  into hundreds of thousands of `AddLine()` calls -- a real per-frame
  hitch (sustained for as long as the camera stayed that close), not a
  crash, but not the "degrades gracefully" behavior this phase's own design
  otherwise achieves. Dear ImGui's window-clipping (this phase's own
  clipping design, above) only trims what's actually *rasterized* -- it does
  nothing for the CPU-side cost of first generating that many segments.
  Fixed with a `kMaxOutlineNdcExtent` (50) clamp on the outline's NDC extents
  in `computeSelectionOutlineNDC()` itself, before either drawing helper ever
  sees the rectangle -- generous enough to never visibly affect any normal
  on-screen selection (the actually-visible NDC range is only `[-1, 1]`),
  while bounding the worst case to a small, constant number of dash
  segments regardless of how close the camera gets.
  - Re-verified after both fixes: a clean `-DCMAKE_BUILD_TYPE=Debug` rebuild
    still compiles with zero new warnings; `ctest` still **5/5** (including
    the new deep-chain case); `tools/run_headless.sh`-style runs at
    `ENGINE_MAX_FRAMES=60` across the default/`ENGINE_DEBUG_SELECT=falling_cube`/
    `ENGINE_DEBUG_SELECT=<unknown name>`/`ENGINE_SHOW_DEBUG_UI=1` combinations
    all completed cleanly with zero GL-error log lines, matching this
    section's original Verify results exactly.

### Phase 14e: a real, live Inspector panel

Fifth sub-phase of the "Phase 14: full editor UI" arc. The Inspector panel's
placeholder text ("Inspector -- coming in Phase 14e") is replaced by a real
editor for whatever entity Phase 14d's Scene Hierarchy panel currently has
selected: a fully live Transform section, a read-only Material section, and
a Physics section that exposes this engine's real static/dynamic
architectural split for the first time as something the Inspector can
actually flip. Still no real Delete/Browse (Phase 14f/14g) -- both shown
greyed-out for visual completeness against the approved mockup, wired to
nothing.

- **Transform -- fully live, Rot-Y only, not full XYZ Euler**: Position and
  Scale are ordinary `ImGui::DragFloat3`s writing straight back into the
  selected entity's real `Transform::setPosition()`/`setScale()` every frame
  they're dragged -- unconditionally safe, since (unlike `ModelComponent`'s
  `Model`) `Transform` is a genuinely per-entity `ComponentPool<Transform>`
  entry (`ecs.hpp`), never shared across entities. Rotation is a single
  `DragFloat("Rot Y", ...)`, matching the approved mockup exactly, rather
  than DebugUI's own pre-existing "Scene Entities" panel's full XYZ Euler
  `DragFloat3` (`Application::renderDebugUI()`, Phase 8c) -- a deliberate,
  *checked* choice, not an assumption: every entity in
  `assets/scenes/default.json` today stores a rotation quaternion whose only
  nonzero imaginary component is `y` (a pure rotation around world Y --
  `"scene"`'s own ~12-degree tilt included), confirmed by hand-decoding each
  entity's quaternion in the scene file before writing this. A single Rot-Y
  field is therefore an honest, lossless representation of every rotation
  this scene actually contains, exactly matching the approved mockup instead
  of over-building a second full-Euler control this scene has no use for.
  `glm::eulerAngles()` -- the same decomposition DebugUI's own panel already
  uses -- reads out just its `.y` component; editing the field always
  *replaces* the whole rotation with a fresh `angleAxis(Y)` quaternion, so a
  hypothetical future non-Y rotation would lose its pitch/roll the moment
  this field is touched (the standard Euler gimbal-lock caveat, restated
  in-code) -- there is no UI path that can create one today, so this is a
  documented, not-yet-reachable limitation, the same posture this project
  already takes with `buildSceneTree()`'s cycle guards.
- **Material -- read-only by design, not an oversight**: the panel shows a
  selected entity's `Model::primaryMaterial()` (new, `model.hpp` -- the first
  mesh's material, or `defaultMaterial_` as a fallback; not a full per-mesh
  material list, which is real, separate scope) as a disabled `ColorEdit3`
  swatch (tint), shininess, and the diffuse texture's real resolved path +
  dimensions (`Texture` gained a `path()` accessor, `texture.hpp`/`.cpp`,
  purely for this display -- nothing GL-facing reads it). This is
  deliberately **not** editable this phase, and the reason is a real,
  concrete footgun rather than a hypothetical one: `model.hpp`'s own header
  comment already establishes that `Model` instances are cached and *shared*
  across every entity that loads the same asset path via `ResourceManager`
  -- and this project's own default scene already exercises that exact
  sharing (`"parented_demo_cube"` and `"falling_cube"` both load
  `assets/models/falling_cube.obj`, see `assets/scenes/default.json`). A live
  `tint`/`shininess` `DragFloat` bound directly to `Material`'s own public
  mutable fields (`material.hpp`) would have been a one-line change -- and
  would have silently repainted *every* entity sharing that cached `Model`
  the instant one of them was edited from the Inspector. `material.hpp` and
  `model.hpp` both carry a new Phase 14e comment spelling this out for
  whoever adds real per-entity material editing later (which needs an actual
  per-entity material clone/override step first -- there isn't one today),
  and the in-panel text states the same reason plainly, not just in a code
  comment nobody using the editor would ever see. "Browse..." is a disabled
  placeholder button (Phase 14g's job).
- **Physics -- the real static/dynamic split, wired through one new
  function**: `physics.hpp`/`physics.cpp` gain `setEntityStatic(registry, id,
  makeStatic)` -- the exact function the Inspector's "Static (Immovable)"
  checkbox calls, deliberately living in the physics module itself rather
  than as inline ImGui-adjacent code, for the same reason this function's own
  header comment gives: `stepPhysics()` only ever iterates
  `registry.each<RigidBody>()` (Phase 8e), so "static" in this engine's own
  architecture already **is** "has no RigidBody" -- there is no separate
  `isStatic` flag anywhere to set, and there never was. Toggling that
  membership is exactly this module's own domain, no different in kind from
  `stepPhysics()` itself reading/writing these same two component types:
  - `makeStatic = true`: removes the entity's `RigidBody` (if any) via
    `EntityRegistry::removeComponent<RigidBody>()`, then ensures it has a
    `Collider` -- adding one with `Collider{}`'s own struct default
    (`halfExtent = 0.25`) only if it doesn't already have one; an existing
    Collider's `halfExtent` is left completely untouched.
  - `makeStatic = false`: adds a default-constructed `RigidBody` back (zero
    velocity, `useGravity = true`) if it doesn't already have one, leaving
    any `Collider` untouched either way -- an entity can be dynamic with or
    without a Collider, exactly as `stepPhysics()` already tolerates (see
    `physics_test.cpp`'s own pre-existing "RigidBody with no Collider" case).
  - **One checkbox covers all three starting states**, including turning a
    no-physics-at-all entity into a physics object for the first time: the
    Inspector computes `isStatic = !hasRigidBody && hasCollider` fresh every
    frame, so an entity with *neither* component (`"scene"` and
    `"parented_demo_cube"` both start this way today) reads as unchecked too,
    labeled `"Static (Immovable) (no physics yet)"`. Checking it calls
    `setEntityStatic(..., true)`, which -- since there's no `RigidBody` to
    remove -- just adds a `Collider`, landing exactly on this phase's new
    Collider-only static state; unchecking it afterward adds a `RigidBody`
    back, making it fall. This is the concrete mechanism that demonstrates
    the new static-collider path on an entity that had never exercised it
    before, not just re-flipping `"falling_cube"`'s pre-existing physics.
  - `Collider Half-Extent` (`ImGui::DragFloat` bound directly to
    `Collider::halfExtent`) is shown whenever a Collider exists, in **both**
    the static and dynamic states; `Use Gravity` (bound to
    `RigidBody::useGravity`) only when dynamic -- matching the approved
    mockup exactly. A dynamic entity with a `RigidBody` but no `Collider`
    (Phase 8e's own tested combination) shows `"No collider on this
    entity"` instead of a half-extent field, rather than inventing an
    "add a collider" button for a case this phase's own brief says to keep
    simple.
  - **The caveat this phase's brief explicitly requires stating plainly**:
    the in-panel Physics section always shows a note that this engine has no
    entity-vs-entity collision system yet (`physics.hpp`'s own "What this
    deliberately IS / IS NOT" list) -- only per-entity gravity plus a single
    flat ground-plane check for `RigidBody` entities. A Collider-only static
    entity is a real, architecturally correct state (nothing ever iterates
    it to move it, by construction), but nothing currently collides *against*
    it either; it is not yet load-bearing for gameplay, and the UI says so
    rather than implying otherwise.
- **Verifying the toggle's actual EFFECT, not just its UI state**: a checkbox
  rendering in the right state doesn't by itself prove `addComponent`/
  `removeComponent` were actually called correctly. Two new env vars, the
  same getenv-gated-value shape as `ENGINE_DEBUG_SELECT`, force
  `setEntityStatic()` at startup (after the scene has finished loading) on a
  *named* entity: `ENGINE_DEBUG_FORCE_STATIC=<name>` /
  `ENGINE_DEBUG_FORCE_DYNAMIC=<name>` (`application.cpp`). Both call the
  exact same production `setEntityStatic()` the Inspector checkbox itself
  calls -- not a parallel hand-rolled toggle -- and record the target entity
  in a new `physicsVerifyEntity_` member so `update()` can `LOG_INFO` its
  `Transform::position().y` every `kPhysicsVerifyLogFrameInterval` (10)
  frames, right after `stepPhysics()` runs. Two runs at `ENGINE_MAX_FRAMES=60`
  prove both directions numerically, not just visually:
  - `ENGINE_DEBUG_FORCE_STATIC=falling_cube`: `y` reads exactly `2.500000` at
    every logged frame (0, 10, 20, ..., 50) -- compare the *default* run's
    own `falling_cube`, which had already fallen to `y = 2.484` by around
    frame 50. The Inspector screenshot for this same run shows `Static
    (Immovable)` checked and `Position` reading `2.500` (frame-latency-static,
    matching selectedEntity_'s own one-frame-behind design), with no `Use
    Gravity` toggle shown (no `RigidBody`).
  - `ENGINE_DEBUG_FORCE_DYNAMIC=scene`: `y` reads `-0.000000`, `-0.149942`,
    `-0.572385`, `-1.267327`, `-2.234769`, `-3.474712` at frames 0/10/20/30/40/50
    -- strictly decreasing, matching free-fall under gravity with no ground
    collision (`"scene"` has no `Collider`, so it falls straight through --
    the same documented behavior `physics_test.cpp`'s own "RigidBody with no
    Collider" case already covers). The Inspector screenshot for this run
    shows `Static (Immovable)` unchecked, `Use Gravity` checked, and `"No
    collider on this entity"` in place of a half-extent field.
  - `tests/physics_test.cpp` also gained four new focused cases exercising
    `setEntityStatic()` directly (dynamic -> static stops `stepPhysics()`
    from moving it; static (Collider-only) -> dynamic starts falling
    immediately; an entity with neither component becomes static-from-
    scratch with the Collider struct's own default half-extent; and calling
    `makeStatic = true` on an already-static entity is idempotent -- an
    existing custom `halfExtent` is never reset back to the default) -- all
    running with no live GL context, the same "plain executable, links only
    the pure logic file it's testing" shape `physics_test`'s pre-existing
    cases already use. `ctest` stays **5/5** (no new test *executable* --
    these are new cases inside the existing `physics_test`, not a sixth
    target).
- **Verify**: a clean `-DCMAKE_BUILD_TYPE=Debug` rebuild compiles with **zero
  new warnings** under `-Wall -Wextra`. `ctest` reports **5/5** (including
  the four new `physics_test` cases above). `tools/run_headless.sh` at
  `ENGINE_MAX_FRAMES=60` completed cleanly with **zero `[ERROR]` log lines**
  across six separate runs (default/no-selection,
  `ENGINE_DEBUG_SELECT=falling_cube`, `ENGINE_DEBUG_SELECT=parented_demo_cube`,
  `ENGINE_DEBUG_FORCE_STATIC=falling_cube`, `ENGINE_DEBUG_FORCE_DYNAMIC=scene`,
  and `ENGINE_SHOW_DEBUG_UI=1`), all inspected directly as screenshots:
  - **No selection**: the Inspector shows `"Inspector -- select an entity in
    the Scene panel to view/edit it."`, matching the pre-14e placeholder's
    own tone rather than a blank panel.
  - **`falling_cube` selected**: Transform shows its live, still-falling
    position; Material shows the checker texture's real resolved path
    (`.../assets/textures/checker.png`, `256x256`) and shininess `32.0`;
    Physics shows `Static (Immovable)` **unchecked**, `Use Gravity`
    **checked**, and `Collider Half-Extent` **0.250** -- exactly matching
    `assets/scenes/default.json`'s own `"collider": {"halfExtent": 0.25}`.
  - **`parented_demo_cube` selected** (no physics components today): Physics
    shows `Static (Immovable)` unchecked with the explanatory
    `"This entity has no physics components yet..."` text, confirming the
    "turn any entity into a physics object" affordance renders sensibly for
    the exact entity this phase's own brief calls out as the best
    demonstration case.
  - The two force-toggle runs and the `ENGINE_SHOW_DEBUG_UI=1` run are
    described in the "actual EFFECT" bullet above and were all visually
    confirmed alongside their log output -- including that Phase 14d's Scene
    Hierarchy tree, click-to-select highlighting, and viewport selection
    outline (the dashed teal rectangle) all still render correctly,
    unaffected by this phase's changes.
- **Post-14e bug-review: adversarial pass found no functional bug, but
  closed two real test-coverage gaps `setEntityStatic()`'s adversarial cases
  exposed.** An independent skeptical review specifically targeted: the
  Rot-Y field's "lossless for every entity today" claim (re-decoded every
  quaternion in `assets/scenes/default.json` by hand against
  `scene_serialization.cpp`'s own confirmed `[w, x, y, z]` storage order --
  every one of the three entities' rotations really is pure-Y, `x = z = 0`
  exactly, not just numerically close); `setEntityStatic()`'s behavior under
  repeated static/dynamic toggling and against a pre-existing non-default
  `Collider::halfExtent`; `Model::primaryMaterial()`'s const-reference
  read-only contract (grepped for any other write path into `Material::tint`/
  `shininess` -- there is none); the Scene Hierarchy click-handler and
  Inspector's read of `selectedEntity_` within one `renderDockspaceShell()`
  call (strictly sequential, same thread, same optional reference -- no
  staleness window exists within a frame); and all three
  `ENGINE_DEBUG_FORCE_STATIC`/`_DYNAMIC` edge cases (unmatched name, both set
  to the *same* entity, both set to *different* entities), each confirmed by
  an actual headless run's log output, not just code reading. All of it held
  up. The one real gap: `tests/physics_test.cpp`'s four Phase 14e cases never
  exercised `setEntityStatic()` on a RigidBody-with-no-Collider entity
  (Phase 8e's own tested `stepPhysics()` combination, but untested through
  this newer function specifically) or a repeated
  static-\>dynamic-\>static-\>dynamic sequence on one entity -- the exact
  shape a user clicking the Inspector checkbox back and forth produces. Two
  new cases close both: the first confirms toggling static on a
  Collider-less RigidBody still adds a sane default-halfExtent Collider and
  actually stops the entity at the ground; the second flips one entity
  static/dynamic four times in a row and confirms a freshly-revived
  `RigidBody` never carries stale velocity from an earlier dynamic phase
  (guaranteed by `ComponentPool::remove()`'s sparse-set erase fully dropping
  the old slot -- see `ecs.hpp` -- rather than merely asserted), the
  surviving `Collider` keeps its original custom `halfExtent` untouched
  across all four flips, and the entity actually falls under gravity again
  once dynamic. `ctest` stays **5/5** (still no new test executable -- two
  more cases inside `physics_test`, six Phase 14e cases total now). Also
  added a one-time clarifying comment in `application.cpp` (no behavior
  change) spelling out what the code already deterministically does when
  both debug env vars name the same entity: `ENGINE_DEBUG_FORCE_DYNAMIC` always
  wins, since it's applied second in a fixed order -- confirmed by running
  both set to `"falling_cube"` and observing it fall exactly like the
  `FORCE_DYNAMIC`-only run, not stay pinned like the `FORCE_STATIC`-only run.
  A clean `-DCMAKE_BUILD_TYPE=Debug` rebuild still compiles with **zero new
  warnings**.

### Phase 14f: real object creation and deletion

Sixth sub-phase of the "Phase 14: full editor UI" arc. Closes the two
placeholders every earlier Phase 14 sub-phase left behind on purpose: the
Inspector's "Delete Object" button (Phase 14e's own header comment: "real
deletion is Phase 14f's job") is now wired to a real, generic entity
destruction primitive, and the Scene panel gains a real Create menu
(Cube/Sphere/Plane/Empty) -- Phase 14d's own scope was tree/selection/outline
only, so this menu is entirely net-new, not a rewire of anything that
already existed.

- **`EntityRegistry::destroyEntity(EntityId)` -- a small polymorphic base,
  not a parallel callback registry**: this closes a gap Phase 8a's own header
  comment flagged from the start ("nothing calls a 'destroy this entity'...
  a landmine for later phases") and Phase 14a's own review re-flagged.
  `ecs.hpp`'s `pools_` map used to be `unordered_map<type_index,
  shared_ptr<void>>` -- a `void*` has no `remove()` to call, so there was no
  way to reach every pool generically from one loop without `EntityRegistry`
  hardcoding a member per component type (exactly the enumeration-that-
  silently-breaks-when-someone-adds-a-type problem this phase's own brief
  calls out). Two designs could close it: (a) a minimal `ComponentPoolBase`
  with one pure-virtual `remove(EntityId)`, `ComponentPool<T>` inheriting it,
  `pools_` upgraded to `shared_ptr<ComponentPoolBase>`; or (b) a parallel
  `vector<function<bool(EntityId)>>` of type-erased erasure callbacks
  recorded once per type in `pool<T>()`. (a) was chosen: `pools_` already
  needs one type-erased handle per type, and a vtable pointer gives that for
  free -- reusing the map that already exists is less to keep in sync than
  maintaining a second parallel list that must never drift from it, and
  virtual dispatch is the ordinary idiom for "call the right per-type
  behavior through a type-erased handle," not a novel mechanism. `pool<T>()`
  itself is otherwise unchanged; `ComponentPool<T>::remove()`'s own
  sparse-set swap-remove logic (Phase 8a) is untouched, just now also an
  `override`. `destroyEntity(id)` itself is a two-line loop: call
  `remove(id)` on every pool `pools_` currently holds, whatever `T` each one
  is -- a **no-op**, not an error, on any pool the destroyed entity never had
  a component in (the overwhelmingly common case for most pools on any given
  call), so a future component type someone adds via
  `addComponent<NewType>(...)` (this file's own long-established "no
  `EntityRegistry` change needed" contract) is automatically covered here
  too, with zero edits to `destroyEntity()` itself required. `EntityId`
  still carries no generation counter -- indices are still never recycled
  (`create()` only ever increments), so a stale `EntityId` after
  `destroyEntity()` reads as "valid, but every `getComponent<T>()` returns
  `nullptr`," which every real call site already treats safely (see Phase
  14e's own defensive Inspector fallback, added specifically anticipating
  this).
- **Orphan-to-root, not cascading delete, for a deleted entity's children --
  documented, not left implicit**: `transform_hierarchy.hpp`/`.cpp` gain
  `destroyEntityOrphaningChildren(registry, id)`, the function the Inspector's
  Delete button and `ENGINE_DEBUG_DELETE` (below) both actually call --
  `ecs.hpp`'s own `destroyEntity()` deliberately knows nothing about `Parent`
  at all (it must not `#include transform_hierarchy.hpp`, or Phase 8a's own
  "components are opt-in, this file never needs to know about a specific
  one" layering breaks), so deciding what happens to a deleted entity's
  children is this function's own, one level up. **Chosen: orphan, not
  cascade.** A deleted entity's direct children (`Parent.id == id`) become
  top-level roots instead of being destroyed along with it. Cascading delete
  is equally defensible in the abstract, but orphaning is the safer default
  specifically because this editor has **no undo yet** (real, separate
  scope) -- a user who deletes one entity and only then notices it had
  children loses exactly that one entity, a recoverable mistake (re-parent
  the orphans, or delete them too, deliberately), not an unrecoverable
  cascading wipeout of a subtree they may not have realized was nested
  underneath it. Only **direct** children are touched -- a grandchild keeps
  its own existing `Parent` link to its (now-orphaned, still alive) parent
  untouched, so the rest of a multi-level subtree keeps moving together
  exactly as before, just rooted one level higher.
- **Orphaned children keep their current WORLD position, not their old LOCAL
  one -- no visible jump**: naively just removing a child's `Parent`
  component would silently reinterpret its existing LOCAL transform
  (relative to the now-gone parent) as its new WORLD transform, teleporting
  it the instant the parent's own accumulated transform stops composing in.
  Instead, each direct child's current world transform is resolved via the
  existing `resolveWorldMatrix()` (Phase 14b) *before* the parent is
  destroyed, decomposed back into position/rotation/scale via
  `glm::decompose()` (`glm/gtx/matrix_decompose.hpp`, opted into via
  `GLM_ENABLE_EXPERIMENTAL` only in `transform_hierarchy.cpp`, not the
  header), and written straight into the child's own `Transform` before its
  `Parent` component is removed. `glm::decompose()`'s own rotation
  convention was **verified, not assumed**, with an offline round-trip probe
  (build `M = T * mat4_cast(rot) * S` from a known non-identity rotation --
  this engine's own `Transform::getModelMatrix()` composition -- decompose
  it, rebuild from the decomposed values, and diff against `M`): the
  decomposed quaternion is used **as-is**, with no conjugate/inverse (the
  direct rebuild reproduces `M` exactly, 0.0 max per-element diff; a
  conjugated rebuild is off by ~2.5) -- a real, checkable fact this section
  records so nobody re-derives it by trial and error later. `tests/
  transform_hierarchy_test.cpp` gained three new Phase 14f cases: a rotated,
  translated parent's direct child keeps its exact pre-delete world position
  after being orphaned (checked two ways -- the raw stored `Transform`
  field, and re-resolving through `resolveWorldMatrix()` itself, proving the
  now-parentless child is treated as a real root end-to-end, not just
  numerically patched); a grandchild under a deleted grandparent stays
  correctly parented to its own (now-orphaned) direct parent, untouched; and
  a childless entity destroys cleanly with no special-cased "no children"
  branch. `ctest` grows to **6/6** -- five existing targets plus a new
  `ecs_test` (below), and `transform_hierarchy_test` itself grows from 5 to
  8 cases.
- **`ecs_test` -- a new test target, because `ecs.hpp` has no matching
  `ecs.cpp` to attach a case to**: `ecs.hpp` is entirely header-only/
  templated (every earlier phase's own test target links against some
  `src/*.cpp` alongside its own test file -- there is no such file for
  `ecs.hpp` to link). `tests/ecs_test.cpp` `#include`s only the header, using
  its own small test-only component types (`Position`/`Velocity`/`Tag`) --
  deliberately **not** `Transform`/`ModelComponent`/`NameComponent` -- so the
  test actually exercises `destroyEntity()`'s generic, any-`T` design rather
  than only ever proving it against this engine's own three real component
  types. Four cases: destroying an entity clears every pool it was actually
  in; destroying one entity leaves an *unrelated* entity's own component
  *values* (not just presence) intact across the sparse-set swap-remove that
  moves data on delete; destroying a component-less entity is a harmless
  no-op that doesn't touch anything else; and calling `destroyEntity()` twice
  on the same id is idempotent.
- **A real Create menu -- "+ Create" button plus a matching right-click,
  Cube/Sphere/Plane/Empty, Point Light/Directional Light/Camera shown but
  disabled**: the Scene panel (`editor_ui.cpp`) gains a small "+ Create"
  button, and right-clicking the panel's own background opens the identical
  popup (`ImGui::BeginPopupContextWindow()` with
  `ImGuiPopupFlags_NoOpenOverItems`, so right-clicking an existing tree row
  still only selects it, undisturbed -- confirmed by headless screenshot,
  not assumed). `EditorUI::renderDockspaceShell()` now **returns** a
  `CreateEntityKind` (`kNone` every frame except the one a real item was
  clicked) instead of gaining a fourth reference parameter -- `EditorUI` still
  builds no entities itself (it has no `ResourceManager`/`Shader`/`Camera` to
  build one from); `Application::render()` acts on a non-`kNone` return value
  via the new `spawnEntityFromCreateMenu()`, the real production entry point
  both the menu's own click handler and `ENGINE_DEBUG_CREATE` (below) call.
  "Cube" reuses the existing `assets/models/falling_cube.obj` (Phase 8e) --
  one box mesh is enough. "Sphere"/"Plane" are two new checked-in assets
  generated by `tools/generate_primitive_meshes.py` (a UV sphere and a flat
  quad, printed as plain OBJ text, no external mesh library -- see that
  script's own header comment for the exact generation command and the
  sizing rationale), loaded through the ordinary `resources_.getModel()`
  cache exactly like every other asset -- there is no procedural-Mesh-to-
  `Model` pathway in this engine (`Model`'s only constructor loads from a
  file path via Assimp), and this phase deliberately doesn't invent one,
  matching the phase brief's own "smallest, most consistent-with-existing-
  architecture" guidance. Every generated face's winding is **verified**
  against its own target outward normal by the generator itself (reversed in
  the output if wrong) -- confirmed independently after generation too, with
  a second, throwaway script that recomputes each face's geometric normal
  from its stored vertex positions and dot-products it against the face's
  own stored `vn`: **0 bad-winding faces, 0 degenerate faces**, across both
  `sphere.obj` (160 faces) and `plane.obj` (1 face). "Empty" is `Transform` +
  `NameComponent` only, no `ModelComponent` -- a real, parentable
  organizational entity (matching the user's own earlier explicit choice of
  real `Parent`-component grouping over a flat, non-transforming "folder"
  concept -- see `transform_hierarchy.hpp`'s own Phase 14b header comment),
  not a label that isn't really an entity at all. A freshly created entity is
  positioned a fixed distance in front of the camera's current facing
  direction, floored above the ground plane (`kCreateEntityDistanceFromCamera`/
  `kCreateEntityMinHeight`, `application.cpp`) so it never spawns overlapping
  existing geometry or buried in the ground, and named via a new
  `uniqueEntityName()` helper -- `"Cube"`, then `"Cube (1)"`, `"Cube (2)"`,
  ... the first time the base name already collides with an existing
  `NameComponent` -- the simple "append a counter" scheme the phase brief
  itself calls sufficient, not a bulletproof-uniqueness guarantee (matching
  this project's own established Phase 8b tolerance for duplicate names
  elsewhere). **Point Light/Directional Light/Camera are shown, matching the
  originally approved mockup, but `BeginDisabled()`'d with an explanatory
  tooltip** -- the same treatment this project's own Inspector already
  established for "Browse..." (Phase 14e) and, until this very phase,
  "Delete Object" itself -- rather than either half-building a Light/Camera
  ECS component or silently dropping the menu items the mockup shows. Each
  needs real, substantial, separate scope this phase's own brief explicitly
  declines to take on: a new `Light` component plus rewiring `basic.frag`/
  `pbr.frag`'s shading to read lights from the ECS instead of
  `application.cpp`'s existing fixed `kPointLights`/`kSpotLights` arrays, and
  for a camera, an "which entity is the active camera" concept this engine
  has no notion of anywhere today. Deferred to a natural follow-up phase --
  there is no currently-tracked phase number for it yet, so none is invented
  here.
- **`ENGINE_DEBUG_CREATE=<cube|sphere|plane|empty>` /
  `ENGINE_DEBUG_DELETE=<entity name>`**: the same getenv-gated-value shape as
  every other `ENGINE_DEBUG_*` var this arc has added, letting a headless run
  (no real mouse to click "+ Create" or "Delete Object" with) exercise the
  **exact same production functions** the real UI calls --
  `spawnEntityFromCreateMenu()`/`destroyEntityOrphaningChildren()` -- rather
  than a parallel hand-rolled path, the same precedent
  `ENGINE_DEBUG_FORCE_STATIC`/`_DYNAMIC` already set for `setEntityStatic()`.
  Applied in the constructor right after the scene finishes loading (CREATE,
  so a single run can combine it with `ENGINE_DEBUG_SELECT` naming the same
  fresh entity) and last among every `ENGINE_DEBUG_*` entity-name lookup
  (DELETE, so it can remove anything an earlier step in the same run just
  named/created/selected) -- deleting the currently-selected entity also
  clears `selectedEntity_`, the identical behavior the real button's own
  click handler has.
- **Verify**: a clean `-DCMAKE_BUILD_TYPE=Debug` rebuild compiles with
  **zero new warnings** under `-Wall -Wextra`. `ctest` reports **6/6**
  (`ecs_test` new; `transform_hierarchy_test` 5 -> 8 cases; every other
  target unchanged). `tools/run_headless.sh` at `ENGINE_MAX_FRAMES=60`
  completed cleanly with **zero `[ERROR]` log lines** across nine separate
  runs -- default baseline; `ENGINE_DEBUG_CREATE` for each of
  cube/sphere/plane/empty; `ENGINE_DEBUG_CREATE=cube` combined with
  `ENGINE_DEBUG_SELECT=Cube`; `ENGINE_DEBUG_DELETE=falling_cube` (a parent,
  with a live child); `ENGINE_DEBUG_DELETE=parented_demo_cube` (a leaf
  child); and `ENGINE_DEBUG_FORCE_STATIC`/`ENGINE_DEBUG_SELECT` /
  `ENGINE_DEBUG_FORCE_DYNAMIC` together, confirming those still work
  unaffected -- all inspected directly as screenshots, not just logs:
  - **Create**: the real (non-debug-overlay) Scene panel shows the new
    "+ Create (right-click for the same menu)" affordance; after
    `ENGINE_DEBUG_CREATE=cube` + `ENGINE_DEBUG_SELECT=Cube`, the tree shows
    `scene` / `falling_cube` (still correctly nesting `parented_demo_cube`
    underneath it, Phase 14d's own nesting completely undisturbed) / `Cube`,
    with `Cube` highlighted selected; the Inspector shows it as a real "Mesh
    entity" with a sensible in-front-of-camera Transform position, the
    correct (reused `falling_cube.obj`) yellow-tinted checker material, and
    no physics components yet; the Viewport shows the new cube rendered with
    its dashed selection outline, positioned in front of the existing scene
    without overlapping it. `ENGINE_DEBUG_CREATE=sphere`/`=plane` each show
    up correctly in the debug overlay's own "Scene Entities" list and render
    correctly in the Viewport -- the sphere a smoothly-lit teal-green ball
    (correct winding: a real specular highlight, no black backface patches),
    the plane a flat light-gray quad facing the camera (also correct
    winding -- a reversed quad would render black/invisible from this
    angle). `ENGINE_DEBUG_CREATE=empty` shows `Empty` added to the entity
    list with the Viewport pixel-identical to the no-create baseline (no
    mesh drawn, exactly as designed).
  - **Delete**: `ENGINE_DEBUG_DELETE=falling_cube` -- the debug overlay's
    entity list drops from `parented_demo_cube` / `scene` / `falling_cube`
    to just `parented_demo_cube` / `scene`; the Viewport shows
    `parented_demo_cube` still present, in very nearly its prior on-screen
    location (not teleported to the origin, not vanished) -- the orphan
    behavior's "no visible jump" property holding up in the actual rendered
    scene, not just the unit test. `ENGINE_DEBUG_DELETE=parented_demo_cube`
    (deleting the *child*, not the parent) -- the entity list drops to just
    `falling_cube` / `scene`, with `falling_cube` itself rendering completely
    unaffected, confirming deleting a leaf doesn't disturb its parent.
  - **Delete Object button, visually**: a separate run at a taller window
    size (so the Inspector's full Physics section and the button below it
    are both on-screen at once) with `falling_cube` selected shows "Delete
    Object" rendered as a normal, **enabled** button -- no more greyed-out
    `BeginDisabled()` styling, no more "Deletion is Phase 14f." caption below
    it.
  - **Regression check**: `ENGINE_DEBUG_FORCE_STATIC=falling_cube` +
    `ENGINE_DEBUG_SELECT=falling_cube` (`ENGINE_SHOW_DEBUG_UI=1`) and
    `ENGINE_DEBUG_FORCE_DYNAMIC=scene` both log and render exactly as Phase
    14e's own README section already describes -- confirming this phase's
    `ecs.hpp`/`editor_ui.cpp` changes didn't disturb either debug aid.
- **Post-14f bug-review fix: a child whose world matrix failed to decompose
  was silently left with a dangling `Parent`, reintroducing the exact
  teleport bug this function exists to prevent**.
  `destroyEntityOrphaningChildren()`'s own `registry.each<Parent>(...)` walk
  (`transform_hierarchy.cpp`) resolved each direct child's world matrix and
  ran it through `glm::decompose()`, but only pushed the child into its
  `orphans` vector `if (glm::decompose(...))` succeeded -- and the loop right
  after, the one that actually calls `removeComponent<Parent>()`, only ever
  ran for entries that made it into `orphans`. `glm::decompose()` can fail
  (return `false`) for a fully degenerate matrix -- confirmed, not assumed,
  by running a handful of scales through it directly: a zero on any single
  scale axis (e.g. `(0, 1, 1)`) reliably fails, while the Inspector's own
  0.01 clamp floor does not, so this was never reachable through the
  Inspector itself (Scale is clamped to `[0.01, 100]`, Phase 14e's own
  comment) but *is* reachable through a hand-edited or malformed
  `assets/scenes/*.json` (`scene_loader.cpp` applies no such clamp on load,
  and `Transform::setScale()` itself is a plain unclamped setter) -- and
  will become more relevant once real Save Scene functionality (Phase 15e)
  makes hand-editing scene files less the only way to reach it. On that
  decompose-fails path, the child's `Parent` component was left pointing at
  the about-to-be-destroyed entity, which was then destroyed via
  `registry.destroyEntity(id)` a few lines later -- leaving the child with a
  dangling `Parent`. `resolveWorldMatrix()`'s own "dangling parent -> treat
  as effective root" fallback (this file's header comment) then kicked in
  next frame and reinterpreted the child's stale LOCAL transform as if it
  were a WORLD transform: a visible teleport, on the one path
  `destroyEntityOrphaningChildren()` exists specifically to prevent. Fixed by
  unconditionally orphaning every direct child regardless of decompose
  success, gating only the Transform-overwrite on a new
  `hasNewWorldTransform` flag on the (renamed-in-spirit, same-named)
  `Orphan` struct: every child is now pushed into `orphans` unconditionally,
  so the post-loop `removeComponent<Parent>()` call -- itself now
  unconditional rather than reachable only for decompose successes -- runs
  for all of them; the position/rotation/scale overwrite loop only actually
  calls `setPosition()`/`setRotation()`/`setScale()` when
  `hasNewWorldTransform` is true, otherwise leaving the child's existing
  Transform completely untouched (its current LOCAL values simply become
  its new, if-slightly-wrong, effective ones -- the same "a visible jump in
  this one unreachable-today edge case beats silently discarding data or
  aborting the whole delete" tradeoff this function's own header comment
  already accepted for the decompose-succeeds case; only the failure case's
  *orphaning* was ever actually broken). `transform_hierarchy_test.cpp`
  gained a new case building a parent with `setScale(glm::vec3(0.0f, 1.0f,
  1.0f))` (confirmed via a standalone check to actually make
  `glm::decompose()` return `false` on this codebase's own T*R*S matrix
  layout) and a normal child under it, then asserts the child is still a
  real root (no `Parent` component) and its Transform is untouched and fully
  finite after `destroyEntityOrphaningChildren()` -- both per this project's
  own established "prove the regression test actually catches the bug"
  practice: reverting just this fix (via `git apply -R` on the isolated
  `transform_hierarchy.cpp` diff, keeping the new test) reproduces the
  failure exactly (`FAIL: the child of a degenerate-scale parent is still
  orphaned to root even though its world matrix failed to decompose -- the
  core Post-14f bug-review fix`, `transform_hierarchy_test: 1 check(s)
  failed`), and re-applying the fix (`git apply` on the same diff) restores
  a clean pass across all 9 `ctest` targets. The already-working
  decompose-succeeds path (this phase's own two `destroyEntityOrphaningChildren()`
  test cases above) still passes unchanged, confirming the refactor didn't
  disturb it. `ENGINE_DEBUG_DELETE=falling_cube` (the normal,
  non-degenerate case exercised by this section's own headless run above)
  re-run against the fixed code logs the same `"destroyed entity ... "`
  line and renders `parented_demo_cube` in the same on-screen position as
  before, with zero `[ERROR]` lines -- confirming no regression on the path
  that was already correct.

### Phase 15a: real Point Light entities

Phase 14f's own Create menu left "Point Light"/"Directional Light"/"Camera"
`BeginDisabled()`'d, with a tooltip explaining each needed real, separate
scope beyond a menu item. This phase closes the smallest of those three:
point lights, since `basic.frag`/`pbr.frag` already treat them as a
`uNumPointLights`-counted array (see `application.cpp`'s pre-existing
`kPointLights` table), not a single fixed slot the way the directional
"sun" light is -- making a point light ECS-driven doesn't need a new
"active X" concept the way Directional Light and Camera still do, so those
two stay disabled, with updated tooltips explaining why each is still its
own follow-up.

- **`light.hpp`/`light.cpp`** (new): a `PointLight` component (color +
  the same `constant`/`linear`/`quadratic` attenuation triple
  `kPointLights` already uses -- no position field, the same "Transform
  already has it" precedent `physics.hpp`'s `Collider` sets), a
  `PointLightSample` struct (a `PointLight`'s fields plus the position
  pulled from that entity's own `Transform` -- field-for-field identical to
  `application.cpp`'s old private `PointLightData`, which it replaces), and
  `collectPointLights(registry, maxTotal, out)`: appends one
  `PointLightSample` per entity that has both a `Transform` and a
  `PointLight` to `out`, capped at `maxTotal` (extras silently skipped,
  warned once). Depends on nothing but `ecs.hpp`/`transform.hpp`/`log.hpp`
  -- no GL dependency, so `tests/light_test.cpp` exercises it with no live
  GL context, the same "plain executable, links only the pure logic file
  it's testing" shape `physics_test.cpp`/`transform_hierarchy_test.cpp`
  already establish.
- **`application.cpp`**: `render()` now builds one `pointLightSamples`
  vector every frame -- `kPointLights`' 3 fixed, hand-tuned entries
  (completely unchanged; every prior phase's own screenshot-verified
  lighting baseline still holds) seeded first, then `collectPointLights()`
  appends any live ECS point lights, up to `kMaxPointLights` (8, matching
  `MAX_POINT_LIGHTS` in both fragment shaders). That one list now feeds
  both the clustered-lighting cull (`clusterLightCuller_.cullLights()`) and
  both shaders' `uPointLights` uniform upload, replacing the old
  function-local `static` cache the cluster-culling code used to keep for
  `kPointLights` -- correct pre-Phase-15 (that table never changed at
  runtime) but wrong now that a user can create/delete point lights mid-run;
  it's rebuilt fresh every frame instead. `kSpotLights` keeps its own
  `static` cache -- spot lights aren't part of this phase's scope, still a
  fixed compile-time table.
- **`editor_ui.cpp`**: the Create menu's "Point Light" item is real now
  (returns `CreateEntityKind::kPointLight`, no longer `BeginDisabled()`'d);
  "Directional Light"'s tooltip no longer says "same reasoning as Point
  Light" (it isn't, any more) and now explains the single-fixed-uniform-vs.
  -array distinction above. The Inspector gains a new "Light" section
  (`ColorEdit3` + three `DragFloat`s for the attenuation triple), shown only
  for a selected entity with a `PointLight` component, fully live-editable
  like Transform -- not read-only like Material, since a `PointLight` is a
  genuinely per-entity `ComponentPool<PointLight>` entry with none of
  Material's shared-Model-cache mutation hazard.
- **`application.cpp`'s `spawnEntityFromCreateMenu()`**: `kPointLight`
  follows Empty's shape (Transform + NameComponent, no ModelComponent --
  this engine has no light-gizmo mesh to draw) plus a freshly
  `addComponent<PointLight>()`'d component at its own struct defaults
  (plain white, `kPointLights`' own (1.0, 0.7, 1.8) attenuation profile) --
  an immediately-visible starting point the new Inspector section can
  retune. `ENGINE_DEBUG_CREATE` gains a `pointlight` value, calling this
  exact same production function so a headless run can prove it without a
  real mouse, the same precedent every other `ENGINE_DEBUG_CREATE` value
  already established.
- **Deliberately not done this phase** (same "real, substantial, separate
  scope" reasoning Phase 14f itself gave for deferring all three): scene
  serialization for `PointLight` -- `saveScene()` exists
  (`scene_serialization.cpp`) but nothing in this engine's UI calls it yet
  (only `loadScene()` runs, once, at startup), so schema support for a
  component no live code path would ever write is exactly the kind of
  speculative, nothing-consumes-it addition this codebase's own established
  style avoids (see `ecs.hpp`'s `NameComponent`/`physics.hpp`'s
  mass-field comments for the same instinct applied elsewhere). A future
  phase that adds a real "Save Scene" UI action should extend
  `SceneEntityRecord`/`parseSceneRecords()`/`writeSceneRecords()` for
  `PointLight` (and `scene_loader.cpp`'s registry-building side) at that
  point, not before.
- **Verify**: a clean rebuild compiles with **zero new warnings** under
  `-Wall -Wextra`. `ctest` reports **7/7** (`light_test` new). A baseline
  `tools/run_headless.sh` run (`ENGINE_MAX_FRAMES=60`, no debug env vars)
  logs the exact same clustered-lighting occupancy as before this phase
  (2136 -> 2155/2304 clusters occupied as the camera settles, avg ~3.58
  lights/occupied cluster) and renders pixel-identical to the pre-Phase-15
  scene -- confirming `kPointLights`' unchanged 3 entries plus zero ECS
  point lights in the default scene really do reproduce the old fixed-array
  behavior exactly. A second run with `ENGINE_DEBUG_CREATE=pointlight` +
  `ENGINE_DEBUG_SELECT="Point Light"` + `ENGINE_SHOW_DEBUG_UI=1` logs
  `Created entity "Point Light" (index 3) via the Scene panel's Create
  menu`, zero `[ERROR]` lines, and the clustered-lighting average rises to
  ~4.44 lights/occupied cluster -- direct proof the new light is actually
  reaching the cluster culler, not just sitting inertly in the registry.
  Inspected as a screenshot: the debug overlay's Scene Entities list shows
  `Point Light` alongside `scene`/`falling_cube`/`parented_demo_cube`; the
  Inspector shows it selected, labeled "Empty entity (no model)", with a
  live Transform and a new "Light" section showing its white default
  color; the Viewport shows a visibly brighter hotspot near the light's
  spawn position (in front of the camera, per the existing Create-menu
  placement heuristic) that isn't present in the baseline screenshot.

### Phase 15b: real Directional Light entities

Phase 15a closed the smaller of the two Create-menu light gaps it inherited
from Phase 14f; this phase closes the harder one. Unlike a point light,
`basic.frag`/`pbr.frag` read a single fixed `uLightDirection`/`uLightColor`
uniform pair (`application.cpp`'s `kLightDirection`/`kLightColor`), not a
`uNumX`-counted array, and that same pair is also this engine's ONE
shadow-casting light (`renderShadowPass()`/`computeCascades()`, built
directly from it). So a Directional Light entity can't just "grow an array
with live data" the way Point Light could -- there has to be a notion of
which ONE entity (if any) is actually driving that single uniform pair/
shadow frustum this frame, which this engine had no notion of before now.

- **`light.hpp`/`light.cpp`**: a new `DirectionalLight` component
  (`direction` + `color`, mirroring `kLightDirection`/`kLightColor`'s own
  meaning field-for-field) and `resolveActiveDirectionalLight(registry,
  active, fallback)`: returns `active`'s own `DirectionalLight` component
  when `active` is a valid entity that actually has one and its direction
  hasn't degenerated to (at or near) the zero vector, else `fallback`
  unchanged. Deliberately NOT an array-oriented `PointLightSample`-shaped
  type -- there is still only ever one active directional light in the
  actual rendering pipeline, so the resolved per-frame value has exactly the
  component's own shape, reused directly rather than duplicated into a
  bespoke sample struct. `direction` is a plain field, NOT derived from the
  entity's own `Transform::rotation()` the way a first glance at
  `PointLight`'s "reuse the Transform" precedent might suggest: the
  Inspector's Transform section only ever edits a single Rot-Y degree field
  (Phase 14e), which can never express `kLightDirection`'s own steep
  downward pitch, so a direct `direction` field (the same precedent
  `application.cpp`'s pre-existing `SpotLightData` already set) is the only
  form that's actually editable to something useful. `DirectionalLight{}`'s
  own defaults are deliberately NOT `kLightDirection`/`kLightColor`'s values
  (unlike `PointLight{}` mirroring `kPointLights`' shared attenuation
  profile) -- a point light is additive, so matching an existing light's
  tuning is a fine "immediately visible" starting point, but a directional
  light REPLACES `kLightDirection`/`kLightColor` outright once active, so
  identical defaults would make a freshly created-and-activated entity
  visually indistinguishable from having done nothing at all. Instead it
  defaults to a steeper "high noon" angle (still on `kLightDirection`'s own
  `+x, +z` side, so its shadow stays camera-visible per that constant's own
  Phase 7a comment) paired with a cool "moonlight" tint, so creating one is
  immediately, visibly a different light. Depends on nothing but
  `ecs.hpp`/`transform.hpp` -- no GL dependency, so `tests/light_test.cpp`
  (extended, not a new file -- same translation unit as
  `collectPointLights()`) exercises it with no live GL context either.
- **`application.hpp`/`application.cpp`**: a new `activeDirectionalLight_`
  member (`std::optional<EntityId>`, default `std::nullopt`) -- "which
  entity is active" resolves to the simplest rule that fits an engine with
  no multi-select UI concept anywhere yet: the most recently Create'd
  Directional Light entity, set unconditionally in
  `spawnEntityFromCreateMenu()`'s own new `kDirectionalLight` case (Transform
  + NameComponent, no ModelComponent -- the same shape `kPointLight` already
  established -- plus a freshly `addComponent<DirectionalLight>()`'d
  component at its own defaults). `render()` now resolves
  `resolveActiveDirectionalLight(registry_, activeDirectionalLight_.value_or(EntityId()),
  DirectionalLight{kLightDirection, kLightColor})` once, before
  `computeCascades()` runs, and reuses that single result for the cascade
  build and both `shader_`/`pbrShader_` `uLightDirection`/`uLightColor`
  uploads -- so the shadow frustum and the shading math can never disagree
  about which light this frame is actually using. A stale
  `activeDirectionalLight_` (its entity later deleted) is never explicitly
  cleared -- `resolveActiveDirectionalLight()` already tolerates a stale id
  exactly the way `getComponent<T>()` does everywhere else in this codebase,
  so it silently and correctly falls back to `kLightDirection`/`kLightColor`
  the next frame with no cleanup required. `ENGINE_DEBUG_CREATE` gains a
  `directionallight` value, calling this exact same production function.
- **`editor_ui.hpp`/`editor_ui.cpp`**: the Create menu's "Directional Light"
  item is real now (returns `CreateEntityKind::kDirectionalLight`, no longer
  `BeginDisabled()`'d) -- only "Camera" stays disabled, for the reason its
  own (unchanged, still accurate) tooltip already gives. The Inspector gains
  a live "Light" section for a selected `DirectionalLight` entity
  (`ColorEdit3` + `DragFloat3` for direction, no per-axis clamp -- see that
  section's own comment for why a floor here would be wrong for a direction,
  unlike Point Light's `Constant` field) that also shows whether THIS entity
  is the currently active one -- without that line, two Directional Light
  entities would render identical Inspector sections while only one of them
  actually affects the scene, a distinction no other component in this
  engine's Inspector has to account for.
  `renderDockspaceShell()`/`renderInspectorPanel()` both gain one new
  read-only `activeDirectionalLight` parameter (Application's own
  `activeDirectionalLight_`, threaded through the same way `outline` already
  is) to make that comparison.
- **Deliberately not done this phase** (same reasoning Phase 15a gave for
  its own equivalent gaps): Camera stays deferred -- a structurally similar
  "which entity is active" problem, but for this engine's actual rendered
  view (a free-fly `Camera` object, not an ECS entity) rather than one
  uniform pair, different enough in kind to stay its own separately-scoped
  follow-up, not folded into this one. No explicit "Set Active" UI control
  either -- "most recently created" is the whole mechanism; a richer rule
  would need a multi-select concept this engine doesn't have anywhere else
  yet, and building one just for this would be exactly the kind of
  speculative UI surface this codebase's own established style avoids. No
  scene serialization for `DirectionalLight`, for the identical "nothing
  consumes it yet" reason Phase 15a gave for `PointLight`.
- **Verify**: a clean rebuild compiles with **zero new warnings** under
  `-Wall -Wextra`. `ctest` reports **7/7** (`light_test` extended with four
  new `resolveActiveDirectionalLight()` cases: active-entity-wins,
  no-active-falls-back, stale/missing-component-falls-back, and
  degenerate-zero-length-direction-falls-back). A baseline
  `tools/run_headless.sh` run (`ENGINE_MAX_FRAMES=60`, no debug env vars)
  logs the byte-identical clustered-lighting occupancy Phase 15a's own
  baseline recorded (2136 -> 2155/2304 clusters occupied, avg 3.565543 ->
  3.581903 lights/occupied cluster) and zero `[ERROR]` lines -- confirming
  a scene with zero Directional Light entities (this engine's own default
  scene, still true today) renders exactly as it did before this phase, no
  regression to the one shadow-casting light every prior phase's own
  screenshot baseline depends on. A second run with
  `ENGINE_DEBUG_CREATE=directionallight` + `ENGINE_DEBUG_SELECT="Directional
  Light"` + `ENGINE_SHOW_DEBUG_UI=1` logs `Created entity "Directional
  Light" (index 3) via the Scene panel's Create menu`, zero `[ERROR]` lines,
  and the SAME clustered-lighting stats as the baseline (a directional light
  is correctly outside the point/spot cluster budget entirely, unlike Phase
  15's point light). Inspected as a screenshot: the debug overlay's Scene
  Entities list shows `Directional Light` alongside
  `scene`/`falling_cube`/`parented_demo_cube`; the Inspector shows it
  selected, labeled "Empty entity (no model)", with a live Transform and a
  new "Light" section whose color swatch reads `(140, 179, 255)` -- exactly
  `DirectionalLight{}`'s own default cool tint; the Viewport shows a
  dramatically different lighting result from the baseline (the warm,
  low-angle sun's bright hotspot and long shadows are replaced by a cooler,
  more overhead look), direct visual proof `resolveActiveDirectionalLight()`
  is actually feeding the shader/shadow pass, not just sitting inertly in
  the registry.

### Phase 15c: real Camera entities

The third and last of Phase 14f's own `BeginDisabled()`'d Create-menu gaps.
Unlike either light, a Camera entity turns out not to need an "active"
resolution mechanism at all: `basic.frag`/`pbr.frag` don't read a
CameraComponent, and this engine's actual rendered view has come from
`Application::camera_` -- a free-fly `engine::Camera` object (`camera.hpp`),
completely independent of the ECS -- since Phase 3, unchanged by this phase.
So closing this gap the same "smallest correct increment" way Phase 15a/15b
did meant something narrower than either of them: a real, inspectable,
creatable `CameraComponent` entity that exists in the scene and does
nothing else yet. A full "possess this entity, free-fly control transfers
to it" feature -- deciding what happens to keyboard/mouse input when a
possessed camera exists, whether/how `camera_` itself gets redirected, and
eventually a camera-switching keybind and/or multiple viewports -- is real,
substantial, separate scope of its own, and nothing in this engine's UI can
exercise any part of it yet (no camera-switching keybind, no multi-viewport,
nothing that would ever read "which entity is the active camera" except a
hypothetical future feature). Building any of that now would be exactly the
kind of speculative, nothing-consumes-it complexity this codebase's own
established style avoids (see `physics.hpp`'s own `RigidBody` mass-field
comment, and Phase 15a/15b's own "no scene serialization for a component
nothing writes yet" precedent above) -- so this phase deliberately does not
build it, the same way Phase 15a/15b themselves deferred Camera in the first
place.

- **`camera_component.hpp`** (new, `include/engine/`): a `CameraComponent`
  holding the three purely-optical properties `engine::Camera`'s own
  `getProjectionMatrix()` actually depends on -- `fovYDeg`/`nearPlane`/
  `farPlane`, defaulting to `Camera`'s own 60.0/0.1/100.0 values verbatim
  (`camera.hpp`'s `fovYDeg_`/`nearPlane_`/`farPlane_`). Deliberately its own
  header, not folded into `light.hpp` even though that file already narrates
  this whole Point-Light-then-Directional-Light-then-Camera arc: a camera
  isn't a light by any stretch (nothing about it feeds `basic.frag`/
  `pbr.frag`'s lighting math), so growing `light.hpp` to also mean "and also
  Camera" would leave that file's own name not matching its contents for no
  real benefit -- the same "a genuinely different KIND of thing gets a new
  header" precedent `light.hpp`'s own top comment already sets by having
  split off from `physics.hpp`/`ecs.hpp` in the first place. Deliberately
  does NOT mirror `Camera`'s `position_`/`yawDeg_`/`pitchDeg_` -- the same
  "Transform already has it" precedent `PointLight`'s own "no position
  field" comment establishes -- nor `movementSpeed_`/`mouseSensitivity_`,
  which describe how free-fly *input* drives a camera, not what a camera
  optically *is*, and this entity never receives input (see above). No
  matching `camera_component.cpp` -- like `ecs.hpp`'s own `NameComponent`,
  this is a plain data struct with no logic function of its own to give one
  a body.
- **`editor_ui.hpp`/`editor_ui.cpp`**: the Create menu's "Camera" item is
  real now (returns `CreateEntityKind::kCamera`, no longer
  `BeginDisabled()`'d) -- closing out all three of Phase 14f's own deferred
  items. Unlike Phase 15b's `kDirectionalLight`, this gains **no** new
  `renderDockspaceShell()` parameter: there is nothing analogous to
  `activeDirectionalLight` to thread through, since nothing in this engine's
  rendering pipeline reads a CameraComponent at all. The Inspector gains a
  new "Camera" section (`DragFloat` for FOV/near/far, `ImGuiSliderFlags_
  AlwaysClamp`-floored the same way `PointLight`'s own `Constant` field is,
  so a near plane can never reach zero/negative and corrupt
  `glm::perspective()`), fully live-editable like the two Light sections
  before it, with an explicit caption stating it is **not** wired to this
  engine's actual rendered view -- a documented gap, not a silent one, the
  same treatment this whole phase's own scope decisions get.
- **`application.hpp`/`application.cpp`**: `spawnEntityFromCreateMenu()`'s
  new `kCamera` case follows the identical Transform + NameComponent (no
  ModelComponent -- no camera-gizmo mesh either) shape `kPointLight`/
  `kDirectionalLight` already established, plus a freshly
  `addComponent<CameraComponent>()`'d component at its own struct defaults.
  Unlike `kDirectionalLight`, that addComponent call is the *entire*
  post-switch effect -- no second statement, no member assignment. There is
  **no `activeCameraEntity_`-shaped member anywhere in this class**: `camera_`
  (the real free-fly `Camera`) and its own `processMovement()`/
  `processMouseInput()` calls in `update()` are completely untouched by this
  phase. `ENGINE_DEBUG_CREATE` gains a `camera` value, calling this exact
  same production function.
- **Deliberately not done this phase**: everything the "actual design
  problem" discussion above already names -- no possession/control-transfer
  mechanism, no camera-switching keybind, no multi-viewport, no
  `activeCameraEntity_` concept of any kind. Also no scene serialization for
  `CameraComponent`, for the identical "nothing consumes it yet" reason
  Phase 15a/15b gave for `PointLight`/`DirectionalLight`. A future phase
  that actually wants ECS-driven view control should build it on top of
  this phase's real (if inert) ECS foundation -- deciding then what happens
  to input when a camera entity is "possessed" and how `camera_` itself gets
  redirected -- not before.
- **Verify**: a clean rebuild (`rm -rf build && cmake -B build -S . &&
  cmake --build build -j`) compiles with **zero warnings** under `-Wall
  -Wextra`. `ctest` reports **8/8** (`camera_component_test` new -- default-
  value coverage proving `CameraComponent{}`'s fields genuinely match
  `Camera`'s own defaults, plus `EntityRegistry` round-trip/independence
  coverage, the same shape `ecs_test.cpp` already established for a
  component with no logic function of its own to unit-test directly). A
  baseline `tools/run_headless.sh` run (`ENGINE_MAX_FRAMES=60`, no debug env
  vars) logs the byte-identical clustered-lighting occupancy Phase 15a/15b's
  own baseline recorded (2136 -> 2155/2304 clusters occupied, avg 3.565543
  -> 3.581903 lights/occupied cluster) and zero `[ERROR]` lines -- and, run
  three times independently with no code involved in this phase touched at
  all, produced screenshots differing from each other by only 5-25 pixels
  out of 480,000 (`compare -metric AE`, none visually distinguishable) --
  this engine's own inherent software-rasterizer (llvmpipe) run-to-run
  least-significant-bit noise, present with or without this phase's changes,
  and the yardstick the next paragraph's own comparison is measured against.
  A second run with
  `ENGINE_DEBUG_CREATE=camera` + `ENGINE_DEBUG_SELECT="Camera"` +
  `ENGINE_SHOW_DEBUG_UI=1` logs `Created entity "Camera" (index 3) via the
  Scene panel's Create menu`, zero `[ERROR]` lines, and the exact SAME
  clustered-lighting stats as the baseline -- direct proof a Camera entity
  contributes nothing to the lighting/rendering pipeline, the expected,
  correct result for this phase, not a bug. Inspected as a screenshot: the
  debug overlay's Scene Entities list shows `Camera` alongside
  `scene`/`falling_cube`/`parented_demo_cube`; the Inspector shows it
  selected, labeled "Empty entity (no model)", with a live Transform and a
  new "Camera" section reading `60.0 deg` FOV (`CameraComponent{}`'s own
  default); the Viewport's rendered content is unaffected by the Camera
  entity specifically -- a pixel `compare` of the portion of the Viewport
  panel NOT covered by the F1 debug overlay window does differ from the
  plain baseline (990 pixels out of 78,000 in the region checked, a small
  anti-aliasing jitter around sphere silhouettes, larger than the 5-25-pixel
  run-to-run noise floor established above), but a THIRD run with
  `ENGINE_SHOW_DEBUG_UI=1` alone -- no `ENGINE_DEBUG_CREATE`, no Camera
  entity at all -- reproduces that identical jitter (0 differing pixels
  against the Camera run's own screenshot, in that same region). So the
  jitter traces entirely to enabling the debug overlay itself (extra ImGui
  draw calls shifting per-frame timing enough to nudge software-rasterizer
  antialiasing by roughly a pixel here and there) -- pre-existing engine
  behavior wholly unrelated to Phase 15c's own changes, not something a
  Camera entity's presence causes.

With Phase 15c, the "Light/Camera" arc Phase 14f's own Create menu opened is
now complete: all three originally-`BeginDisabled()`'d items (Point Light,
Directional Light, Camera) are real, creatable, inspectable ECS entities.
What differs, deliberately, is how far each one actually reaches into this
engine's existing systems -- Point Light plugs straight into an existing
array-shaped uniform, Directional Light needed a brand-new "which one is
active" concept because it replaces a single fixed uniform pair, and Camera
needed neither, because nothing downstream reads it yet. Each phase in this
arc closed exactly the gap in front of it and named the next one explicitly,
rather than guessing ahead at scope the engine wasn't ready to use.

### Phase 15d: a real Asset Browser

The Assets panel had been a single placeholder line since Phase 14a itself
(`ImGui::TextWrapped("Asset Browser -- coming in a later Phase 14 sub-phase.")`)
-- untouched through the entire Phase 14b-14f/15a-15c arc while every other
editor panel (Scene, Viewport, Inspector) grew real content. With the
Light/Camera arc closed, this was the next actual gap, so it becomes its own
whole top-level phase (16), not another Phase 14 sub-phase or folded under
15 -- the numbering convention this project's own "Development history"
section has followed consistently: each closed arc's own last sub-phase
letter is where the next genuinely new arc starts a fresh whole number, the
same way Phase 8's a-e sub-phases gave way to Phase 9, and 15a-15c now give
way to 16.

Scope, decided by actually reading what's on disk (`ls -R assets/`) rather
than assuming: `assets/` holds `models/` (4 `.obj`+`.mtl` pairs),
`textures/` (flat PNGs plus `skybox/` and `hdri/` subfolders), `shaders/`
(29 GLSL files), and `scenes/` (one file, `default.json`) -- no separate
`materials/` directory exists at all (materials live inline in each
model's own `.mtl` file, loaded as part of the model, not as a standalone
asset type). Only `models/` and `textures/` are browsable this phase -- see
`asset_browser.hpp`'s own header comment for the full reasoning, briefly:
they're the only two asset kinds `ResourceManager` (`resource_manager.hpp`)
actually loads/caches BY PATH for something a level designer places into a
scene (`getModel()`/`getTexture()`). `shaders/` is renderer implementation
detail -- every path under it is a `resolveAssetPath()`'d constant baked
into `application.cpp`, never something picked per-entity through a
browser. `scenes/` is the currently-loaded level's own save/load file
(`scene_serialization.hpp`), not content placed *into* a level the way a
model or texture is -- `ResourceManager` has no "Scene" cache entry at all,
which is the real, checked boundary this allowlist mirrors (one entry short
of ResourceManager's own three -- Shader excluded for the identical
"engine machinery, not placeable content" reason as `shaders/` itself).

- **`asset_browser.hpp`/`asset_browser.cpp`** (new, `include/engine/` +
  `src/`): `AssetTreeNode` (name + assets/-relative path + isDirectory +
  children) and `buildAssetTree(assetsRoot)`, a pure `std::filesystem` walk
  with no GL/ImGui/ecs.hpp dependency at all -- same "pure data vs.
  GL/ImGui-facing" split `scene_hierarchy.hpp` established for
  `buildSceneTree()`, just walking real directories instead of the ECS.
  `directory_iterator`'s enumeration order is unspecified by the standard,
  so every directory's entries are sorted (subdirectories before files,
  alphabetical within each group) before children are built, making the
  output -- and the Assets panel's own on-screen order -- deterministic
  regardless of filesystem/platform. A missing category directory
  (`assets/models/` or `assets/textures/` not existing at all) is silently
  skipped, not an error: neither is required to exist.
- **`editor_ui.hpp`/`editor_ui.cpp`**: the Assets panel's placeholder line
  is replaced by a real tree. `EditorUI` gains two private members:
  `assetTree_` (the `std::vector<AssetTreeNode>` forest, built exactly
  **once**, in the constructor, via `buildAssetTree(resolveAssetPath(
  "assets"))` -- see below for why once, not every frame) and
  `selectedAssetPath_` (a `std::optional<std::string>` keyed by
  `AssetTreeNode::relativePath`, the row currently highlighted).
  `renderAssetTreeNode()` mirrors `renderSceneTreeNode()`
  (Phase 14d) almost line for line -- same `TreeNodeEx()` flag set, same
  "click anywhere on the row selects it" `IsItemClicked()` check, same
  no-icon-glyphs reasoning (this engine's Dear ImGui build carries no icon
  font -- see `renderSceneTreeNode()`'s own comment) -- kept as a genuinely
  separate function rather than templated over "anything tree-shaped"
  because the two node types share no base, their selection state has
  different types and different owners, and the two panels are likely to
  diverge further once either grows real functionality. One deliberate
  difference: asset-tree nodes do **not** get `ImGuiTreeNodeFlags_
  DefaultOpen` the way scene-tree nodes do -- this engine's own scene has a
  handful of entities (expand-by-default costs nothing there), but
  `assets/textures/skybox/` and `assets/textures/hdri/` already show real
  subfolder structure a level designer would want collapsed by default as a
  project's content grows.
- **Caching, not a per-frame rebuild**: unlike `buildSceneTree()` (rebuilt
  fresh every `renderDockspaceShell()` call, because ECS entities can be
  created/deleted any frame through the Scene panel's own Create menu),
  `assetTree_` is built exactly once, in `EditorUI`'s constructor. Nothing
  in this engine writes into `assets/` at runtime today -- no import
  feature exists (see below) -- so the tree cannot change between one frame
  and the next during a single run, and re-walking the filesystem 60+ times
  a second against a tree already known to be unchanged would be pure waste
  -- the same "match the cache lifetime to what actually varies" reasoning
  `resource_manager.hpp`'s own header comment gives for its own
  get-or-load cache, applied here as "build once" instead of "load once,
  reuse forever."
- **Selection is local, cosmetic state, not threaded through like
  `selectedEntity`**: `selectedAssetPath_` lives on `EditorUI` itself, never
  passed to or read by `Application`. Nothing outside the Assets panel
  reads it this phase -- no Inspector wiring, no viewport hookup, no
  drag-and-drop (see below) -- so there is none of `selectedEntity`'s own
  cross-panel/cross-frame reason (`application.hpp`'s Phase 14d comment) to
  make it live anywhere but here.
- **Deliberately not done this phase** (the same conservative default this
  whole project's "smallest correct increment" discipline expects, checked
  against each item on its own merits rather than assumed):
  - **Drag-and-drop into the Scene/Viewport.** A real, separate, much
    bigger feature -- no drag-and-drop exists anywhere in this engine's UI
    today, and building the first one as a side effect of an Asset Browser
    phase would bury a substantial new capability inside what should be a
    read-only browsing phase.
  - **Wiring the Material Inspector's `Browse...` button to this browser.**
    That button's own existing comment (`editor_ui.cpp`, Phase 14e)
    already documents exactly why it's `BeginDisabled()`'d: `Model`
    instances (and therefore their `Material`s) are cached and shared via
    `ResourceManager` across every entity that loaded the same asset path,
    so letting the Inspector swap which model/material a live, shared
    `Material` reference points at would silently repaint every other
    entity sharing that same cached asset -- a real hazard, not a
    hypothetical one, given `parented_demo_cube` and `falling_cube` already
    share `assets/models/falling_cube.obj` in this project's own default
    scene. Nothing about *building a read-only browser* changes that
    hazard -- the button stays exactly as `BeginDisabled()`'d as Phase 14e
    left it, comment untouched.
  - **Importing new files into `assets/` from outside the project.** This
    phase browses what's already there; it doesn't add to it. No file
    dialog, no copy-into-`assets/` operation.
  - **Thumbnails/previews.** A real separate feature of its own -- texture
    thumbnails would need their own GPU-upload/caching path (loading every
    texture in `assets/textures/` just to preview it, whether or not it's
    ever used in the scene, is a meaningfully different cost profile than
    `ResourceManager`'s own "load on first actual use" contract), and
    models have no single obvious preview image at all. Text labels (no
    icon glyphs, matching `renderSceneTreeNode()`'s own precedent) are
    enough to identify a row this phase.
- **Post-16 bug-review fix: an unreadable entry anywhere under
  assets/models/ or assets/textures/ crashed the WHOLE ENGINE at startup**.
  The first-pass `buildNode()` used the throwing
  `fs::is_directory(path)`/`fs::directory_iterator(path)` overloads (no
  `std::error_code` out-param) for everything below the top-level category
  check, even though that top-level check itself already used the
  non-throwing overloads correctly. Reviewer reproduced it directly: a
  self-referencing symlink one level under a scratch `assets/models/` (a
  realistic on-disk state -- a stray symlink loop from an install artifact
  or backup tool, not a hypothetical) makes resolving that entry's type hit
  `ELOOP` ("Too many levels of symbolic links"), which
  `fs::is_directory(path)` surfaces as an uncaught `std::filesystem_error`.
  Because `buildAssetTree()` runs inside `EditorUI`'s constructor, inside
  `Application`'s own constructor, that exception propagated all the way
  out of `Application`'s constructor -- `main.cpp`'s top-level `try`/`catch`
  turned it into `LOG_ERROR("Fatal: ...")` + `EXIT_FAILURE`, taking down the
  entire engine over a filesystem hiccup confined to one asset subdirectory,
  not a cosmetic Assets-panel-only bug. Fixed by routing every filesystem
  query in `buildNode()` through the `std::error_code`-taking overload, the
  same discipline the top-level category check already modeled: a
  directory's own `is_directory()` check, its `directory_iterator`
  construction, advancing that iterator (`operator++`/`increment()` can
  *also* throw mid-enumeration, not only at construction -- easy to miss,
  since a plain range-based `for` silently calls the throwing
  `operator++()`), and the sort comparator's own `is_directory()` calls
  (used only for display ordering) are all now guarded. An entry whose type
  can't be determined at all is left out of the returned tree entirely (with
  a `LOG_WARN` noting what was skipped and why) rather than aborting the
  whole walk -- see `asset_browser.hpp`'s own new "Unreadable entries never
  abort the walk" comment for the documented contract, and
  `asset_browser.cpp`'s `buildNode()`/`tryIsDirectory()` for the mechanics.
  `asset_browser_test.cpp` gained a matching case: a scratch
  `assets/models/` containing a real file plus a self-referencing symlink
  now asserts `buildAssetTree()` returns normally (wrapped in a
  `try`/`catch` that fails the test outright if it ever throws again) with
  the readable sibling present and the broken symlink silently absent --
  confirmed to actually exercise the new code path via the executable's own
  stdout (`[WARN] Asset Browser: skipping unreadable entry "models/
  self_loop"...`), not just a passing exit code.
- **Post-16 bug-review fix (second pass): a permission-denied subdirectory
  rendered as a silently-empty folder, with no diagnostic anywhere**. The
  first-pass fix above reached for
  `fs::directory_options::skip_permission_denied` when constructing
  `buildNode()`'s `directory_iterator`, reasoning that it would let an
  EACCES-on-open failure degrade for free. It does degrade -- but that
  flag's own documented standard-library behavior is to swallow the
  failure and hand back zero entries WITHOUT ever setting `iterEc`, so
  `buildNode()`'s own error-handling branch never even ran for this case.
  The practical effect: a permission-denied directory rendered in the
  Assets panel as an ordinary, childless leaf row -- no expand arrow, no
  `LOG_WARN`, nothing -- genuinely indistinguishable, both in the log and
  in the actual editor GUI, from a directory that is simply, unremarkably
  empty. A level designer staring at the panel itself (not tailing stdout)
  had no way to tell those two states apart. Fixed by dropping
  `skip_permission_denied` entirely: `directory_iterator`'s plain
  `(path, error_code&)` constructor DOES set `iterEc` on EACCES, so a
  permission-denied directory now falls through to the exact same
  `LOG_WARN`-and-continue path (`asset_browser.cpp`'s own `buildNode()`)
  every other unreadable thing in this file already used -- reported, not
  silently absorbed, while still never throwing.
  `asset_browser_test.cpp` gained a matching case, honestly scoped to what
  this project's own sandboxed environment can actually verify: this test
  process runs as root (confirmed via `geteuid()`, not assumed), and root
  bypasses ordinary Linux DAC permission checks -- `chmod 000 dir && ls
  dir` still succeeds as root, checked directly while writing this fix --
  so a plain chmod-based test would have silently proven nothing. The test
  instead forks a short-lived child, drops ONLY that child's effective uid
  to "nobody" (uid 65534, seteuid -- reversible, process-local, the parent
  itself stays root throughout) before calling `buildAssetTree()` there, so
  the OS genuinely enforces the permission against that child process, and
  reports pass/fail back via its exit code; a non-root environment (or one
  where the privilege drop itself fails) takes a separate, explicitly
  labeled path rather than asserting a result it couldn't actually
  establish. Confirmed to actually exercise the new code path the same way
  as the symlink case above: running the test binary directly shows
  `[WARN] Asset Browser: could not list directory "models/locked"
  (Permission denied)...` before `all checks passed`, not just a green exit
  code.
- **Verify**: a clean rebuild (`rm -rf build && cmake -B build -S . &&
  cmake --build build -j`) compiles with **zero warnings** under `-Wall
  -Wextra` (`asset_browser.cpp`/`asset_browser_test.cpp` included -- no
  extra link flag needed for `<filesystem>` on this project's GCC 13
  toolchain). `ctest` reports **9/9** (`asset_browser_test` new -- category
  allowlist coverage proving `shaders/`/`scenes/` are excluded even when
  physically present alongside `models/`/`textures/` in the same scratch
  tree, recursive nesting, deterministic directories-before-files/
  alphabetical sort order, a missing single category, a wholly assetless
  root, a self-referencing symlink degrading gracefully instead of
  throwing, and (the second post-review addition above) a permission-denied
  subdirectory being detected/logged rather than silently absorbed, all
  against a scratch directory under `std::filesystem::temp_directory_path()`
  -- never this project's own real `assets/` tree, so results never depend
  on what that directory happens to contain on a given day). A
  `tools/run_headless.sh` run
  (`ENGINE_MAX_FRAMES=60 ENGINE_SHOW_DEBUG_UI=1`) logs the byte-identical
  clustered-lighting occupancy Phase 15a/15b/15c's own baseline recorded
  (2136 -> 2155/2304 clusters occupied, avg 3.565543 -> 3.581903
  lights/occupied cluster) and **zero** `[ERROR]` lines across two
  independent runs -- direct confirmation this phase's changes (Assets
  panel content only) touch nothing in the rendering/lighting pipeline, the
  expected result since `buildAssetTree()` never runs anywhere near
  render(). Inspected as a screenshot: the Assets panel shows two real,
  collapsed tree rows labeled `models` and `textures` (each with a real
  expand arrow) in place of the old placeholder sentence, docked in its
  original Phase 14a position beneath the Scene panel.

## Libraries used and why

| Library     | How it's obtained                          | Why |
|-------------|---------------------------------------------|-----|
| **GLFW**    | CMake `FetchContent` (git, tag `3.4`)        | De facto standard cross-platform windowing/input/GL-context library; actively maintained, small API surface, works fine headlessly against Xvfb. |
| **GLM**     | CMake `FetchContent` (git, tag `1.0.1`)      | Header-only math library with GLSL-like syntax (vectors, matrices, transforms) -- avoids hand-rolling matrix/vector math for later phases (cameras, transforms). |
| **stb_image** | Vendored single header in `external/stb/` (`stb_image.h`, from github.com/nothings/stb) | Public-domain, single-header, no build step -- simplest possible texture loading path. Vendored unused since Phase 0; Phase 4's `engine::Texture` (`src/texture.cpp`) is the first phase to `#include` it, and the only translation unit that `#define`s `STB_IMAGE_IMPLEMENTATION`. |
| **GL loader** | Hand-written, vendored in `external/glad/` | See below. |
| **Assimp** | CMake `FetchContent` (git, tag `v5.4.3`) | De facto standard asset-import library; loads Phase 5's OBJ/glTF scenes via one well-known API instead of hand-rolling per-format parsers. Importer scope narrowed to just OBJ + glTF (see "Phase 5" above) to keep build time/scope down. |
| **nlohmann/json** | CMake `FetchContent` (git, tag `v3.11.3`) | Single-header, MIT-licensed JSON library; Phase 8b's scene file format (see "Phase 8b" above) -- fetched the same way as GLFW/GLM/Assimp (an ordinary tagged git dependency), not hand-vendored like GLAD/stb_image below (see "Phase 8b"'s own writeup on why that precedent doesn't apply to a JSON library). |
| **Dear ImGui** | CMake `FetchContent` (git, tag `v1.92.9b`), built as a first-party `imgui` static library target (see "Phase 8c" above) | Phase 8c's debug overlay (entity inspector, render-pass toggles, frame stats) -- the de facto standard immediate-mode debug UI library for real-time engines/tools; ships no CMake build of its own by design, so this project compiles its core sources + GLFW/OpenGL3 backend files directly, the same "small first-party target" shape `glad` below already uses. |

### GL loader: why hand-written instead of a generated GLAD

A real GLAD loader is normally produced by a Python code generator that
downloads the Khronos `gl.xml` API registry. This environment's network
egress does not allow reaching the endpoints GLAD's generator (or gl3w's
generator) needs at generation time, and there isn't a single canonical
pre-generated GLAD-for-GL-3.3-core file to vendor verbatim without pulling
in a large, unrelated third-party repository just to lift its `glad.c`/`glad.h`
out of it.

Instead, `external/glad/` contains a small, hand-written loader that:

- Exposes the exact same public API a real GLAD would (`gladLoadGLLoader()`,
  `GLADloadproc`, plain `gl*` names available after `#include <glad/glad.h>`),
  so application code and future phases look identical to what a generated
  GLAD would produce, and swapping in a real generated GLAD later (if a
  phase needs broader GL coverage) is a drop-in replacement.
- Covers OpenGL 3.3 core state management, shaders/programs, VAOs/VBOs/EBOs,
  textures, and framebuffers/renderbuffers -- i.e. what's expected to be
  needed through the early rendering phases (triangle, shader uniforms,
  camera transforms, basic textures, basic render targets).
- Is loaded via `glfwGetProcAddress`, exactly like a real GLAD would be used
  in `main.cpp`.

If a later phase needs a GL function not in this list, either add another
entry to `external/glad/include/glad/glad.h` + `external/glad/src/glad.c`
following the existing pattern, or replace `external/glad/` wholesale with a
tool-generated GLAD/gl3w if/when the registry endpoints are reachable.
(Phase 7b needed only new *enum* `#define`s -- `GL_TEXTURE_CUBE_MAP*`,
`GL_TEXTURE_WRAP_R`, `GL_RGBA16F`, `GL_DEPTH_COMPONENT24` -- every function
its `Skybox`/`Framebuffer` classes call, e.g. `glGenRenderbuffers`/
`glRenderbufferStorage`/`glActiveTexture`, was already declared and loaded
by an earlier phase, since a cubemap/floating-point texture upload goes
through the exact same `glTexImage2D`/`glTexParameteri`/`glBindTexture`
entry points as an ordinary 2D texture, just with different enum arguments.)

