# 3D Engine (TBD)

A small C++/OpenGL 3D engine: a real windowing/main-loop foundation, a
free-fly camera, GL 3.3 core shaders/meshes/textures, multi-light (directional
+ point + spot) Blinn-Phong lighting with tangent-space normal mapping and
directional shadow mapping, Assimp-based multi-object scene loading with a
real node hierarchy, MSAA anti-aliasing, a thin entity/resource-cache
layer, a procedural-sky cubemap background, an HDR + Reinhard-tonemapped
post-process pipeline, (Phase 9) a real metallic/roughness Cook-Torrance
PBR material/shader alongside the original Blinn-Phong path, proven out on a
sphere reference grid, and (Phase 10) real-time image-based lighting -- a
diffuse irradiance cubemap, a GGX-prefiltered mipmapped specular cubemap, and
a split-sum BRDF LUT, all convolved once at startup from the existing skybox
-- replacing that PBR path's flat placeholder ambient term with a real,
direction- and roughness-aware one -- built up from bare-metal OpenGL, and
verified at every step by a headless Xvfb+Mesa run/screenshot harness (no GPU
or display required). See "Architecture overview" right below for what the
finished whole looks like today, or "Development history" further down for
how it got built, phase by phase (this repo was built incrementally across
10 phases, each independently bug-reviewed), including the specific bugs
each phase's review found and fixed.

## Architecture overview

`engine_app` is a single executable (no separate engine library target
yet -- see the note at the top of `CMakeLists.txt`) built around a handful
of small, mostly-RAII classes in `include/engine/` + `src/`:

- **`Window`** (`window.hpp`/`.cpp`) -- owns the GLFW window and a GL 3.3
  core context, requested with 4x MSAA (`GLFW_SAMPLES`, plus a Linux-only
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
  `shadowShader_`, a `skyboxShader_`, and a `postProcessShader_`), a
  `ShadowMap`, an off-screen HDR `Framebuffer`, a `Skybox`, a hand-built
  normal-mapped ground plane (`groundMesh_`/`groundMaterial_`), a fullscreen
  `postProcessQuad_`, a `std::vector<Entity>`, and a `Camera`; runs the main
  loop (poll input -> update camera -> render -> swap) until the window
  closes, ESC is pressed, or `ENGINE_MAX_FRAMES` is reached (headless
  verification only -- see below). `render()` now does three GL passes per
  frame: a depth-only shadow pass (`renderShadowPass()`), the normal lit
  color pass (scene + skybox, into the HDR framebuffer), and a fullscreen
  tonemap/gamma resolve pass to the window (see "Phase 7b" below).
- **`Camera`** + **`Transform`** (`camera.hpp`/`.cpp`, `transform.hpp`) -- a
  yaw/pitch free-fly camera driven by `InputState` (or a small scripted
  waypoint path under `ENGINE_CAMERA_DEMO`, for headless verification where
  there's no real input device), and a position/quaternion-rotation/scale
  bundle used for every object's model matrix.
- **`Shader`** / **`Mesh`** / **`Texture`** / **`Material`** -- the
  rendering primitives: a linked GL program; an interleaved
  position/normal/texCoord/tangent VAO+VBO+EBO; a 2D GL texture loaded via
  stb_image; and a `Shader` + diffuse `Texture` + optional normal-map
  `Texture` + tint/shininess bundle bound once per draw call.
  `assets/shaders/basic.vert`/`basic.frag` implement Blinn-Phong lighting
  for one directional light + a fixed-size array of point/spot lights (see
  "Phase 7a" below), tangent-space normal mapping, and directional shadow
  mapping, all with a properly computed (transpose-inverse) normal matrix.
- **`PBRMaterial`** (`pbr_material.hpp`, header-only) -- a second,
  independent material type (see "Phase 9" below) alongside `Material`
  above: an albedo tint, metallic, roughness, and ambient-occlusion scalar,
  bound to `assets/shaders/pbr.vert`/`pbr.frag`'s metallic/roughness
  Cook-Torrance BRDF instead of `basic.frag`'s Blinn-Phong. `Material`/
  `basic.vert`/`basic.frag` are unchanged and still drive the rest of the
  scene (the table/box/pyramid/ground) -- Phase 9 adds a second lighting
  model rather than replacing the first.
- **`ShadowMap`** (`shadow_map.hpp`/`.cpp`) -- an RAII depth-only FBO +
  texture used to render the scene from the directional light's point of
  view once per frame; the main pass samples it to shadow that one light's
  own contribution per fragment.
- **`Framebuffer`** (`framebuffer.hpp`/`.cpp`) -- an RAII "render target":
  an FBO with a floating-point (`GL_RGBA16F`) color attachment plus a depth
  renderbuffer. `Application` renders the whole lit scene (+ skybox) into
  one of these (`hdrFramebuffer_`) instead of straight to the window, so a
  light's real intensity can exceed 1.0 without hard-clipping (see "Phase
  7b" below) -- a small reusable pattern a future bloom pass could extend,
  though bloom itself isn't built here.
- **`Skybox`** (`skybox.hpp`/`.cpp`) -- a 6-face procedural-sky
  `GL_TEXTURE_CUBE_MAP` (`assets/textures/skybox/`) rendered as the scene's
  background via its own program (`assets/shaders/skybox.vert`/`.frag`),
  drawn last each frame with a `GL_LEQUAL` depth trick so it only shows
  through pixels nothing else drew.
- **`IBLProbe`** (`ibl_probe.hpp`/`.cpp`, see "Phase 10" below) -- real-time
  image-based lighting built from `Skybox`'s own cubemap: a diffuse
  irradiance cubemap, a mipmapped GGX-prefiltered specular cubemap, and a 2D
  BRDF integration LUT, all convolved once at startup (a handful of ordinary
  draw calls into small offscreen FBOs, not a persistent render target) via
  three dedicated one-time shader passes. `pbr.frag` samples all three every
  frame to drive its ambient term -- replacing Phase 9's flat placeholder
  ambient with real, direction- and roughness-aware image-based lighting.
- **`Model`** (`model.hpp`/`.cpp`) -- loads a whole scene via Assimp
  (`assets/models/scene.obj`: a table, a box on the table, and a separate
  pyramid) into a tree of `ModelNode`s, each with its own local transform,
  mesh indices, and children; `draw()` walks the tree depth-first,
  composing world transforms and binding each node's `Material`.
- **`Entity`** (`entity.hpp`) -- a `Transform` plus an optional
  `shared_ptr<Model>`; `Application::entities_` is the list `render()`
  iterates (currently one element), rather than a hardcoded single model
  member.
- **`ResourceManager`** (`resource_manager.hpp`/`.cpp`) -- a per-key
  `shared_ptr` cache for `Shader`/`Texture`/`Model`. Every asset load in the
  engine -- the scene's shader, the scene's model, every material's diffuse
  texture including the shared checker-texture fallback -- goes through it,
  so nothing is loaded from disk or re-uploaded to the GPU more than once.

One frame, in short: `Application::run()` polls GLFW events and an
`InputState`, feeds it to `camera_`, then `render()` (1) renders the whole
scene depth-only into `shadowMap_` from the directional light's point of
view, (2) restores the window's real viewport and binds `hdrFramebuffer_`,
uploads view/projection/light-space/lighting uniforms once, and iterates
`entities_` calling `model->draw(shader, entity.transform.getModelMatrix())`
(which recurses the model's node tree drawing each mesh with its own
material) plus the hand-built ground plane, each fragment sampling
`shadowMap_` and any bound normal map as it shades, (3) draws `skybox_` last
into that same HDR framebuffer as the background, and (4) resolves
`hdrFramebuffer_`'s color buffer to the window with one fullscreen
tonemap/gamma-correct pass (`postProcessShader_` + `postProcessQuad_`).

## Directory layout

```
CMakeLists.txt        Root build: fetches deps (incl. Assimp), builds engine_app
src/                   Engine .cpp sources (main.cpp, window.cpp, application.cpp,
                       shader.cpp, mesh.cpp, camera.cpp, texture.cpp, model.cpp,
                       input.cpp, resource_manager.cpp, shadow_map.cpp,
                       framebuffer.cpp, skybox.cpp, ibl_probe.cpp)
include/engine/        Public engine .h/.hpp headers (window, application, log,
                       gl_debug, version, shader, mesh, camera, transform,
                       texture, material, pbr_material, model, entity, input,
                       resource_manager, shadow_map, framebuffer, skybox,
                       ibl_probe)
external/              Vendored small/single-header libs (stb_image, glad)
assets/                Shaders (incl. shadow.vert/.frag, skybox.vert/.frag,
                       postprocess.vert/.frag, pbr.vert/.frag,
                       cubemap_capture.vert, irradiance_convolution.frag,
                       prefilter.frag, brdf_lut.frag), textures
                       (checker.png, normal_bump.png, skybox/ -- 6 cubemap
                       faces), models (scene.obj + scene.mtl)
tools/                 Build/run/screenshot scripts
tests/                 Placeholder for later phases (empty CMakeLists)
```

A single executable target (`engine_app`) is built for now. A future phase
will likely split this into an `engine` library (most of src/) plus a thin
app target, so tests/ and any additional front-ends can link the engine
without recompiling it -- see the comment at the top of `CMakeLists.txt`.

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

Equivalent manual steps, if you want to reproduce it by hand:

```sh
Xvfb :99 -screen 0 800x600x24 &
export DISPLAY=:99
export LIBGL_ALWAYS_SOFTWARE=1   # force Mesa's llvmpipe software GL driver
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
  action-mapping/rebinding system -- just the concrete fields this phase's
  `Camera` reads.
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
  -- a 4x4 grid (metallic 0->1 across columns, roughness 0.05->1.0 across
  rows), one shared albedo (a saturated red-orange) so the metal/dielectric
  Fresnel distinction is directly comparable across the grid. Built directly
  in the camera's own image plane (its right/up basis vectors, derived from
  `kDefaultCameraPosition`/`kSceneCenter`) rather than laid out along world
  X/Z: an axis-aligned grid recedes away from the camera along a mostly
  depth-facing direction at this engine's fixed camera angle, foreshortening
  row spacing so hard that adjacent rows visibly overlapped on screen even
  with generous world-space spacing -- confirmed by re-projecting sphere
  centers through the same view/projection matrices `Application` builds. A
  camera-facing grid instead faces the camera edge-on like a real reference
  chart, every sphere equidistant from the camera, both axes evenly spaced
  in screen space. Placed in front of the existing table/box/pyramid scene
  (closer to the camera) so both the new PBR content and the existing
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

## Libraries used and why

| Library     | How it's obtained                          | Why |
|-------------|---------------------------------------------|-----|
| **GLFW**    | CMake `FetchContent` (git, tag `3.4`)        | De facto standard cross-platform windowing/input/GL-context library; actively maintained, small API surface, works fine headlessly against Xvfb. |
| **GLM**     | CMake `FetchContent` (git, tag `1.0.1`)      | Header-only math library with GLSL-like syntax (vectors, matrices, transforms) -- avoids hand-rolling matrix/vector math for later phases (cameras, transforms). |
| **stb_image** | Vendored single header in `external/stb/` (`stb_image.h`, from github.com/nothings/stb) | Public-domain, single-header, no build step -- simplest possible texture loading path. Vendored unused since Phase 0; Phase 4's `engine::Texture` (`src/texture.cpp`) is the first phase to `#include` it, and the only translation unit that `#define`s `STB_IMAGE_IMPLEMENTATION`. |
| **GL loader** | Hand-written, vendored in `external/glad/` | See below. |
| **Assimp** | CMake `FetchContent` (git, tag `v5.4.3`) | De facto standard asset-import library; loads Phase 5's OBJ/glTF scenes via one well-known API instead of hand-rolling per-format parsers. Importer scope narrowed to just OBJ + glTF (see "Phase 5" above) to keep build time/scope down. |

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

