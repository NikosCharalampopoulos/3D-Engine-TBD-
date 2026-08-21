# 3D Engine (TBD)

A C++/OpenGL 3D engine, building toward a game engine, starting from
OpenGL bare-metal basics. This repository is developed in phases; this
document covers **Phase 0 (project scaffolding and a proven build/run
pipeline) and Phase 1 (a real window + main-loop foundation).**

Phase 0 does *not* contain engine logic -- it exists to prove that the
toolchain (CMake, dependency fetching, a GL 3.3 core context, and a headless
run/screenshot harness) all work end to end, so later phases can focus on
actual engine code.

Phase 1 replaces Phase 0's 5-hardcoded-frames placeholder with a real
`Window` (RAII GLFW window + GL context) and `Application` (poll/update/
render/swap main loop) pair -- see "Phase 1: window + main loop" below.

## Directory layout

```
CMakeLists.txt        Root build: fetches deps, builds engine_app
src/                   Engine .cpp sources (main.cpp, window.cpp, application.cpp)
include/engine/        Public engine .h/.hpp headers (window, application, log, gl_debug, version)
external/              Vendored small/single-header libs (stb_image, glad)
assets/                Shaders, textures, models (shaders/ stubbed for now)
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

## Libraries used and why

| Library     | How it's obtained                          | Why |
|-------------|---------------------------------------------|-----|
| **GLFW**    | CMake `FetchContent` (git, tag `3.4`)        | De facto standard cross-platform windowing/input/GL-context library; actively maintained, small API surface, works fine headlessly against Xvfb. |
| **GLM**     | CMake `FetchContent` (git, tag `1.0.1`)      | Header-only math library with GLSL-like syntax (vectors, matrices, transforms) -- avoids hand-rolling matrix/vector math for later phases (cameras, transforms). |
| **stb_image** | Vendored single header in `external/stb/` (`stb_image.h`, from github.com/nothings/stb) | Public-domain, single-header, no build step -- simplest possible texture loading path for a later phase. Not yet `#include`d/used anywhere in Phase 0 (no textures yet), just vendored and ready. |
| **GL loader** | Hand-written, vendored in `external/glad/` | See below. |

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
`engine::Application` (see "Phase 1: window + main loop" above). Real
rendering content (shaders, geometry, a camera) starts in Phase 2.
