# 3D Engine (TBD)

A C++/OpenGL 3D engine, building toward a game engine, starting from
OpenGL bare-metal basics. This repository is developed in phases; this
document covers **Phase 0 (project scaffolding and a proven build/run
pipeline), Phase 1 (a real window + main-loop foundation), Phase 2
(shaders + a rendered, per-face-colored cube), Phase 3 (a real
free-fly Camera + Transform, replacing the hardcoded view/projection),
Phase 4 (a Texture class, a Material bundling it with a Shader, and
Blinn-Phong directional lighting -- the flat per-face-colored cube is now a
single textured, lit surface), Phase 5 (Assimp-based model/scene
loading -- the single hardcoded cube is replaced with a real `engine::Model`
that loads a multi-object scene from a file and renders its whole node
hierarchy), and Phase 6 (engine foundations: MSAA anti-aliasing, a
lightweight `Entity` (Transform + Model) replacing loose per-object members,
a `ResourceManager` asset cache, and an `InputState` layer decoupling
`Camera` from `Window` -- the final planned phase, closing out the fixed-demo
era in favor of reusable engine plumbing).**

Phase 0 does *not* contain engine logic -- it exists to prove that the
toolchain (CMake, dependency fetching, a GL 3.3 core context, and a headless
run/screenshot harness) all work end to end, so later phases can focus on
actual engine code.

Phase 1 replaces Phase 0's 5-hardcoded-frames placeholder with a real
`Window` (RAII GLFW window + GL context) and `Application` (poll/update/
render/swap main loop) pair -- see "Phase 1: window + main loop" below.

## Directory layout

```
CMakeLists.txt        Root build: fetches deps (incl. Assimp), builds engine_app
src/                   Engine .cpp sources (main.cpp, window.cpp, application.cpp,
                       shader.cpp, mesh.cpp, camera.cpp, texture.cpp, model.cpp,
                       input.cpp, resource_manager.cpp)
include/engine/        Public engine .h/.hpp headers (window, application, log,
                       gl_debug, version, shader, mesh, camera, transform,
                       texture, material, model, entity, input, resource_manager)
external/              Vendored small/single-header libs (stb_image, glad)
assets/                Shaders, textures, models (assets/textures/checker.png,
                       assets/models/scene.obj + scene.mtl)
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

The resulting executable is `build/engine_app`.

## Phase 1: window + main loop

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

## Running headlessly (no GPU / no display required)

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

## Phase 2: shaders + a rendered cube

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

## Phase 3: Camera + Transform

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

## Phase 4: Texture + Material + Phong lighting

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

## Phase 5: Assimp model loading + a node hierarchy

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

## Phase 6: engine foundations (MSAA, Entity, ResourceManager, InputState)

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

## What `engine_app` actually does

Phase 0's `src/main.cpp` created a GLFW window with a GL 3.3 core context,
loaded GL function pointers, cleared the framebuffer to cornflower blue for
a handful of hardcoded frames, and exited -- proving build+link+run
(headless included) worked end to end.

Phase 1's `src/main.cpp` now just constructs an `engine::Application`
(800x600, "3D Engine") and calls `run()`; the window, GL context, and the
poll/update/render/swap loop live in `engine::Window` /
`engine::Application` (see "Phase 1: window + main loop" above). Phase 2
adds the first real rendering content -- shaders and a colored cube -- on
top of that loop; see "Phase 2: shaders + a rendered cube" above. Phase 3
replaces the hardcoded view/projection/model matrices with a real
`engine::Camera` (free-fly, yaw/pitch, WASD + mouse-look) and
`engine::Transform` (the cube's model matrix); see "Phase 3: Camera +
Transform" above. Phase 4 replaces the flat per-face-colored cube with a
textured, Blinn-Phong-lit one -- `engine::Texture` loads
`assets/textures/checker.png`, `engine::Material` bundles it with the
shader and simple tint/shininess properties, and `basic.vert`/`basic.frag`
implement ambient+diffuse+specular directional lighting with a proper
normal matrix; see "Phase 4: Texture + Material + Phong lighting" above.
Phase 5 replaces that single hardcoded cube with `engine::Model`, which
loads a whole multi-object scene (`assets/models/scene.obj`) via Assimp and
recursively draws its node hierarchy, each node's mesh(es) placed by its own
composed world transform; see "Phase 5: Assimp model loading + a node
hierarchy" above. Phase 6 renders the identical scene with real MSAA
anti-aliasing and restructures ownership around it: the scene is now an
`engine::Entity` in `Application::entities_` rather than a bare `model_`
member, `Shader`/`Texture`/`Model` all load through a shared
`engine::ResourceManager` cache instead of ad-hoc construction, and
`engine::Camera` receives per-frame input through an `engine::InputState`
snapshot instead of reading `Window` directly; see "Phase 6: engine
foundations" above.
