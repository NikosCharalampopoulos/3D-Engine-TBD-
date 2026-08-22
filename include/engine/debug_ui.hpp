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
struct GLFWwindow;

namespace engine {

class DebugUI {
public:
    // `window` must already have a current GL context (see Window's own
    // constructor). If `enabled` is true, ImGui_ImplOpenGL3_Init() creates
    // GL objects (a shader program, the font atlas texture) immediately;
    // see this file's own header comment for why `enabled=false` (the
    // default, unless ENGINE_SHOW_DEBUG_UI is set) instead does nothing at
    // all here -- that GL-object creation is deferred until setEnabled(true)
    // is first called, if ever.
    DebugUI(GLFWwindow* window, bool enabled);
    ~DebugUI();

    DebugUI(const DebugUI&) = delete;
    DebugUI& operator=(const DebugUI&) = delete;
    DebugUI(DebugUI&&) = delete;
    DebugUI& operator=(DebugUI&&) = delete;

    bool enabled() const { return enabled_; }

    // Phase 8d: flips whether the overlay is shown/updated from here on.
    // A no-op if `enabled` already matches the current state. Turning it
    // on for the FIRST time on an object originally constructed with
    // enabled=false lazily performs the same ImGui context/backend
    // creation the constructor would have done had it started enabled --
    // deferred rather than done unconditionally at construction, so a run
    // that starts disabled and is never toggled on (every headless
    // verification run today, since Xvfb has no real F1 keypress to
    // trigger it) still calls zero ImGui functions across its whole
    // lifetime, preserving this class's original guarantee for that
    // unchanged default path. Once created, the ImGui context is kept
    // alive (not torn down) across later off->on->off transitions within
    // the same DebugUI's lifetime -- only newFrame()/render() below start
    // no-op'ing again -- since destroying and recreating ImGui's GL
    // objects on every toggle would be needless churn for a debug overlay.
    void setEnabled(bool enabled);

    // Starts a new ImGui frame (ImGui_ImplOpenGL3_NewFrame() +
    // ImGui_ImplGlfw_NewFrame() + ImGui::NewFrame()) -- call once per frame,
    // before issuing any ImGui:: widget calls. No-op if !enabled().
    void newFrame();

    // Finishes the frame newFrame() started (ImGui::Render()) and
    // rasterizes its draw data straight onto whichever framebuffer is
    // currently bound (see Application::renderDebugUI()'s own comment on
    // why that's always the default framebuffer, after this engine's own
    // tonemap pass) via ImGui_ImplOpenGL3_RenderDrawData(). No-op if
    // !enabled().
    void render();

private:
    // Factored out of the constructor so setEnabled(true) can lazily run
    // the exact same ImGui context/backend setup the constructor runs
    // immediately when starting enabled.
    void initializeImGuiContext();

    GLFWwindow* window_;
    bool enabled_;
    // Separate from enabled_: tracks whether the ImGui context/backends
    // have EVER been created for this object, so setEnabled()/the
    // destructor know whether there's anything to tear down/re-toggle
    // versus lazily create, independent of the CURRENT enabled_ value
    // (e.g. constructed enabled, then toggled off: initialized_ stays
    // true, enabled_ goes false, and no re-init is needed if toggled back
    // on).
    bool initialized_ = false;
};

}  // namespace engine

#endif  // ENGINE_DEBUG_UI_HPP
