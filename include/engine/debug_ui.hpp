#ifndef ENGINE_DEBUG_UI_HPP
#define ENGINE_DEBUG_UI_HPP

// Phase 8c: a thin RAII wrapper around Dear ImGui's context + its GLFW/
// OpenGL3 backends -- the same "small class owns some GL-adjacent global
// state, constructed/destroyed exactly once" shape Window/Skybox/ShadowMap
// already establish, just for ImGui's own global context instead of a GL
// object this engine allocates directly.
//
// `enabled` (constructor parameter, from ENGINE_SHOW_DEBUG_UI -- see
// application.cpp) gates whether this constructor does anything at all:
// when false (the default), no ImGui context is created, no GLFW callbacks
// are installed, and newFrame()/render() are no-ops for this object's whole
// lifetime -- so a headless run with the overlay off never calls a single
// ImGui function, which is what guarantees this phase can't perturb a pixel
// of the existing rendered-scene screenshots every prior phase's own
// headless verification depends on (see this phase's own README section for
// the pixel-diff that confirms this). This mirrors the "always a plain
// member, a factory/condition decides what it's built from" pattern
// buildSkybox()/ENGINE_USE_PROCEDURAL_SKYBOX already uses elsewhere in this
// engine, rather than making debugUI_ an optional/pointer member in
// Application.
//
// What this class deliberately does NOT do: decide what widgets to draw.
// That's Application::renderDebugUI()'s job (application.cpp) -- it has
// access to the private state (registry_, ssaoDisabled_, etc.) the actual
// debug panel needs to show/edit, which this class has no business knowing
// about. DebugUI only owns the frame-lifecycle plumbing (start a frame,
// rasterize whatever ImGui:: calls happened since) that's the same
// regardless of what any particular phase's panel contains.
//
// What this class also deliberately does NOT do: gate this engine's own
// camera mouse-look behind ImGui's io.WantCaptureMouse. Dear ImGui's GLFW
// backend installs its own GLFW callbacks (glfwSetKeyCallback,
// glfwSetCursorPosCallback, etc.) when enabled -- this engine had zero
// callbacks registered before this phase (see input.hpp's own header
// comment: InputState is a per-frame *poll* of Window::isKeyPressed()/
// getCursorPos(), never callback-driven), so there's nothing for ImGui's
// callbacks to clobber. But polling and callbacks are independent GLFW
// mechanisms: glfwGetCursorPos() always returns the real, current absolute
// cursor position regardless of whether ImGui's callbacks also fired for
// the same event, and regardless of io.WantCaptureMouse -- that flag is
// only a hint for an application to *choose* to ignore input elsewhere, not
// something that changes what a poll-based query returns. So dragging an
// ImGui widget while the debug UI is visible does not corrupt
// Window::getCursorPos()'s value for Camera's own mouse-look; it does mean
// the camera keeps reading mouse deltas while the user's cursor is actually
// over a widget, which could feel like "the camera moves while I'm trying
// to drag a slider." Fixing that (checking
// ImGui::GetIO().WantCaptureMouse/WantCaptureKeyboard before Camera reads
// InputState) is a real, well-known integration step for a shipped tool,
// but out of scope here: this phase's overlay defaults to hidden precisely
// so it doesn't need to coexist with live camera control in the one
// environment (headless verification) this repo's own tooling actually
// exercises every commit.
//
// Phase 8d adds setEnabled() (see its own comment below) so
// InputAction::ToggleDebugUI (default F1 -- see input_action_map.hpp and
// application.cpp) can flip the overlay on/off at runtime, on top of
// ENGINE_SHOW_DEBUG_UI's existing constructor-time `enabled` -- the env
// var now just sets the INITIAL state and F1 toggles from there, the same
// "env-var-initialized, then runtime-mutable" combination
// ssaoDisabled_/ssrDisabled_ already use (Phase 13d/13g's env vars +
// Phase 8c's own checkboxes over them).
//
// Phase 14a rewiring (behavior unchanged -- see README.md's own Phase 14a
// section): this class used to own the ImGui context + GLFW/OpenGL3 backend
// lifecycle itself, lazily created only once first enabled, so a headless
// run with the overlay off (the default) called zero ImGui functions ever.
// Phase 14a adds a second, always-on ImGui-based UI layer (see
// engine::EditorUI, editor_ui.hpp) -- and Dear ImGui only supports one live
// context safely driving a given GLFWwindow*'s input at a time (see
// EditorUI's own header comment for exactly why two independent contexts
// on one window doesn't work). Since EditorUI's dockspace is unconditionally
// on every run, it -- not this optional, F1-toggled class -- is now the one
// that creates/owns the shared ImGui context and backends, and calls the
// one shared ImGui::NewFrame()/Render() pair each frame. DebugUI no longer
// creates or tears down anything ImGui-related itself: it is now just the
// enabled/disabled state ENGINE_SHOW_DEBUG_UI + F1 have always controlled,
// which Application::renderDebugUI() (application.cpp, its own content
// completely untouched by this phase) still checks before submitting its
// panel's ImGui:: widget calls into whichever frame EditorUI already
// started. The *observable* behavior is identical to before: unset
// ENGINE_SHOW_DEBUG_UI means the panel itself still never appears and F1
// still toggles it exactly as before -- what changed is only "which class's
// constructor calls ImGui::CreateContext()", not what the panel shows or
// when. One real, intentional consequence: a headless run's default
// screenshot is no longer pixel-identical to a pre-Phase-14a build the way
// Phase 8c's own verification proved (see that phase's own README section) --
// EditorUI's dockspace is drawn unconditionally now, independent of
// ENGINE_SHOW_DEBUG_UI, which is exactly the point of this phase (an
// always-visible editor chrome) rather than a regression in this specific
// class's own guarantee.
namespace engine {

class DebugUI {
public:
    // `enabled` is the overlay's initial visibility (from
    // ENGINE_SHOW_DEBUG_UI, see application.cpp's showDebugUIFromEnv()).
    // Phase 14a: no longer takes a GLFWwindow* or touches ImGui/GL at all --
    // see this file's own Phase 14a comment above for why that ownership
    // moved to EditorUI. This constructor is now just a plain state
    // initializer (and an existence log line).
    explicit DebugUI(bool enabled);

    DebugUI(const DebugUI&) = delete;
    DebugUI& operator=(const DebugUI&) = delete;
    DebugUI(DebugUI&&) = delete;
    DebugUI& operator=(DebugUI&&) = delete;

    bool enabled() const { return enabled_; }

    // Phase 8d: flips whether the overlay's panel is submitted from here on.
    // A no-op if `enabled` already matches the current state. Phase 14a:
    // no longer lazily creates/tears down anything -- EditorUI's ImGui
    // context is already always alive, so this is now purely a state flip
    // (plus the same log line as before).
    void setEnabled(bool enabled);

private:
    bool enabled_;
};

}  // namespace engine

#endif  // ENGINE_DEBUG_UI_HPP
