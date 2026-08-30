#!/usr/bin/env bash
# Runs the engine_app executable headlessly under Xvfb + Mesa software
# rendering (llvmpipe/softpipe via swrast_dri.so), verifying:
#   1. The app runs to completion without crashing or hanging.
#   2. A screenshot of the rendered window can be captured from outside the
#      process (proves an actual window + GL surface existed, not just that
#      the process didn't crash).
#
# Usage:
#   tools/run_headless.sh [path-to-engine_app] [screenshot-output.png]
#
# Requires: xvfb-run (or Xvfb + DISPLAY), imagemagick (import/convert) or
# xwd, and the built executable. See README.md for the full explanation.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

APP_BIN="${1:-${REPO_ROOT}/build/engine_app}"
# Default lives under the repo's (gitignored) build/ directory rather than a
# hardcoded /tmp path, so this script is portable across machines/CI and
# doesn't depend on any particular session's scratch directory existing.
SCREENSHOT_OUT="${2:-${REPO_ROOT}/build/phase0_screenshot.png}"

if [[ ! -x "${APP_BIN}" ]]; then
    echo "error: executable not found or not executable: ${APP_BIN}" >&2
    echo "Build it first: cmake -B build -S . && cmake --build build" >&2
    exit 1
fi

mkdir -p "$(dirname "${SCREENSHOT_OUT}")"

DISPLAY_NUM=99
XVFB_DISPLAY=":${DISPLAY_NUM}"

echo "== Starting Xvfb on ${XVFB_DISPLAY} =="
Xvfb "${XVFB_DISPLAY}" -screen 0 800x600x24 &
XVFB_PID=$!
trap 'kill "${XVFB_PID}" 2>/dev/null || true' EXIT

# Give Xvfb a moment to create its socket.
for _ in $(seq 1 20); do
    if [[ -e "/tmp/.X11-unix/X${DISPLAY_NUM}" ]]; then
        break
    fi
    sleep 0.2
done

export DISPLAY="${XVFB_DISPLAY}"
# Force Mesa's software (llvmpipe) GL rasterizer explicitly -- there is no
# real GPU in this environment, only the swrast_dri.so driver.
export LIBGL_ALWAYS_SOFTWARE=1

# Phase 14a: main.cpp's own default window size grew from 800x600 to
# 1600x900 (see its own comment) for normal interactive use, but this
# script pins engine_app back to the original 800x600 here via
# ENGINE_WINDOW_WIDTH/HEIGHT (main.cpp's override, see its own comment)
# rather than widening Xvfb's screen (below) to match. Reason: every render
# pass sized off the window's real framebuffer (hdrFramebuffer_, SSAO's
# g-buffer, bloom's ping-pong buffers) would run at 3x this project's
# original pixel count on the same llvmpipe software rasterizer this
# script's own MAX_WAIT_ITERS timeout below was measured against (see that
# comment for Phase 13g's own ~20s/60-frames Debug-build cost) --
# regressing every *existing* phase's headless verification timing budget,
# not just this one, for a resolution bump this phase's placeholder-only
# dockspace panels don't need to actually see. A real interactive run still
# gets the bigger, more usable default; only this headless verification
# path opts back down.
export ENGINE_WINDOW_WIDTH="${ENGINE_WINDOW_WIDTH:-800}"
export ENGINE_WINDOW_HEIGHT="${ENGINE_WINDOW_HEIGHT:-600}"

echo "== Launching ${APP_BIN} on DISPLAY=${DISPLAY} =="
# Run the app in the background so we can screenshot its window while it is
# still up (it only lives for a handful of frames), then wait for it to
# exit on its own -- it must not hang, so we still enforce a hard timeout.
"${APP_BIN}" &
APP_PID=$!

# Give the app a moment to actually create its window and issue its first
# glClear/glfwSwapBuffers before we screenshot -- capturing too early would
# just photograph Xvfb's empty (black) root window and look like a broken
# GL context even though rendering hasn't started yet.
sleep 0.4

# Poll for a screenshot that actually shows rendered content (up to ~8s)
# while the app is still alive. Phase 10 added a one-time, roughly
# ~1-second startup cost (engine::IBLProbe's offline irradiance/prefilter/
# BRDF-LUT convolution passes, run once before the main loop's first frame
# -- see application.cpp/ibl_probe.cpp) between window creation and the
# first real rendered frame; a plain "first xwd+convert that succeeds wins"
# loop (this script's original behavior) can't tell a genuinely rendered
# frame apart from Xvfb's still-blank root window captured during that
# window -- xwd/convert both "succeed" either way, they just capture
# whatever pixels are there, blank or not. A blank/near-empty capture
# encodes down to a tiny file (a few hundred bytes, a flat single-color
# PNG) versus a real rendered 800x600 frame's tens-to-hundreds of KB, so
# this loop now keeps polling past a merely-successful conversion until the
# resulting file also clears a minimum size, rather than stopping at the
# first technically-successful (but possibly still-blank) one.
#
# Phase 14a lowered MIN_SCREENSHOT_BYTES (20000 -> 2000) and added the
# consecutive-pass debounce below -- both earned by a real, reproducible
# false result this phase's own always-on ImGui dockspace exposed that
# neither existed before it:
#   1. A steady-state frame with the dockspace's four (mostly text-on-dark-
#      background) panels visible PNG-compresses to only ~11-12KB -- for
#      every prior phase, "real content" always meant a dense, colorful lit
#      3D render (tens-to-hundreds of KB), so 20000 bytes was a safe filter
#      against a ~241-byte blank Xvfb root window. Phase 14a's own
#      legitimate steady-state output falls *below* that old filter, so the
#      old threshold would silently exhaust every one of the 40 polling
#      attempts without ever "passing" and just fall through to whatever
#      the last (still perfectly valid) attempt wrote -- functionally fine,
#      but it burns this script's whole ~8s polling budget on every future
#      run instead of exiting the moment good content appears, for a
#      filter value that no longer matches what "real content" looks like
#      post-Phase-14a. 2000 bytes keeps comfortable margin above the
#      ~241-byte blank case while clearing every real frame observed either
#      side of this phase (dockspace-covered ~11-12KB and pre-14a
#      dense-3D-render hundreds of KB alike).
#   2. Directly measured against this phase's own build (see this
#      project's own Phase 14a README section for the full trace): the
#      very first frame that has any real content at all can, for exactly
#      one frame, show the freshly-docked Viewport panel not yet actually
#      occluding the 3D scene behind it -- a known, benign Dear ImGui
#      docking startup transient (a newly created dock node's child window
#      needs one frame to settle before it visually fills its slot) -- so a
#      "first passing screenshot wins" loop could land on that one
#      transient frame instead of the stable, ever-after-representative one
#      immediately following it. Requiring the SAME size check to pass on
#      two *consecutive* 0.2s-apart polls (not just once) skips past a
#      single anomalous frame like that one, at the cost of one extra 0.2s
#      in the common case where there's nothing to debounce.
#   3. Phase 17d bumped the required consecutive passes 2 -> 3. Root cause
#      (confirmed directly against the vendored Dear ImGui source, not
#      guessed): the SAME one-frame docking-settle transient #2 above
#      already documents -- Dear ImGui's own imgui.cpp (Begin(), around the
#      "Update visibility" section) intentionally sets
#      `window->HiddenFramesCannotSkipItems = 1` for a docked window whose
#      tab is being selected for the first time ("When we are about to
#      select this tab (which will only be visible on the _next_ frame)...
#      This is analogous to regular windows being hidden from one frame" --
#      that comment's own words), which is why each of the four dockspace
#      panels' own tab-bar pill (background fill + label text, drawn only
#      once DockTabIsVisible is confirmed) can be missing -- just the bare
#      collapse arrow visible -- for exactly one frame after a dock node's
#      tab bar is first created, predating this project's custom title bar
#      entirely (confirmed: an unmodified pre-Phase-17d build, rebuilt and
#      multi-sampled the same way, exhibits the identical single-frame gap).
#      Phase 17d's title bar simply adds enough always-rendered content
#      (icon/name/window-control buttons) that THIS transient frame's own
#      screenshot now *also* clears MIN_SCREENSHOT_BYTES -- something the
#      smaller pre-Phase-17d frame usually didn't do, so two consecutive
#      passes used to already mean "past the transient" by coincidence, not
#      by design. Three consecutive passes restores that same margin
#      directly (one settle frame can satisfy at most one pass; a second
#      "still-transient" frame passing immediately afterward has never been
#      observed in this project's own multi-frame sampling either side of
#      this phase) rather than depending on any particular build's exact
#      frame-1 byte count.
MIN_SCREENSHOT_BYTES=2000
REQUIRED_CONSECUTIVE_PASSES=3
consecutive_passes=0
for _ in $(seq 1 40); do
    if ! kill -0 "${APP_PID}" 2>/dev/null; then
        break
    fi
    passed=0
    if command -v xwd >/dev/null 2>&1; then
        if xwd -root -display "${DISPLAY}" -out "${SCREENSHOT_OUT}.xwd" 2>/dev/null; then
            if command -v convert >/dev/null 2>&1; then
                if convert "${SCREENSHOT_OUT}.xwd" "${SCREENSHOT_OUT}" 2>/dev/null; then
                    size=$(stat -c%s "${SCREENSHOT_OUT}" 2>/dev/null || stat -f%z "${SCREENSHOT_OUT}" 2>/dev/null || echo 0)
                    if (( size >= MIN_SCREENSHOT_BYTES )); then
                        passed=1
                    fi
                fi
            fi
        fi
    fi
    if (( passed )); then
        consecutive_passes=$((consecutive_passes + 1))
        if (( consecutive_passes >= REQUIRED_CONSECUTIVE_PASSES )); then
            break
        fi
    else
        consecutive_passes=0
    fi
    sleep 0.2
done

# Hard timeout so a hung app can never leave this script (or the headless
# verification calling it) hanging. This was 75 iters (15s) through Phase
# 13a; by Phase 13g's cumulative Debug-build cost -- CSM's 3 depth passes,
# clustered lighting's compute dispatch, SSAO's 3 screen-space passes,
# bloom's ping-ponged blur, and the SSR compositing pass, all stacked on the
# same 60-frame headless run, on this project's llvmpipe software
# rasterizer -- a legitimate, non-hung run of a Debug build routinely takes
# ~20s wall-clock (measured directly: 60 frames, ~18.4s of that inside the
# main loop alone, plus IBLProbe's one-time startup convolution). 300 iters
# (60s) restores real headroom above that instead of racing it.
MAX_WAIT_ITERS=300  # 300 * 0.2s = 60s
iters=0
while kill -0 "${APP_PID}" 2>/dev/null; do
    if (( iters >= MAX_WAIT_ITERS )); then
        echo "error: engine_app did not exit within 15s, killing it" >&2
        kill -9 "${APP_PID}" 2>/dev/null || true
        wait "${APP_PID}" 2>/dev/null || true
        exit 124
    fi
    sleep 0.2
    iters=$((iters + 1))
done

wait "${APP_PID}"
APP_EXIT=$?

rm -f "${SCREENSHOT_OUT}.xwd"

if [[ ${APP_EXIT} -ne 0 ]]; then
    echo "error: engine_app exited with code ${APP_EXIT}" >&2
    exit "${APP_EXIT}"
fi

if [[ -s "${SCREENSHOT_OUT}" ]]; then
    echo "== engine_app ran successfully; screenshot saved to ${SCREENSHOT_OUT} =="
else
    echo "warning: engine_app ran successfully but no screenshot was captured" >&2
fi

exit 0
