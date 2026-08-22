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
struct GLFWwindow;

namespace engine {

class DebugUI {
public:
    // `window` must already have a current GL context (see Window's own
    // constructor) -- ImGui_ImplOpenGL3_Init() below creates GL objects
    // (a shader program, the font atlas texture) immediately. Does nothing
    // at all if `enabled` is false; see this file's own header comment.
    DebugUI(GLFWwindow* window, bool enabled);
    ~DebugUI();

    DebugUI(const DebugUI&) = delete;
    DebugUI& operator=(const DebugUI&) = delete;
    DebugUI(DebugUI&&) = delete;
    DebugUI& operator=(DebugUI&&) = delete;

    bool enabled() const { return enabled_; }

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
    bool enabled_;
};

}  // namespace engine

#endif  // ENGINE_DEBUG_UI_HPP
