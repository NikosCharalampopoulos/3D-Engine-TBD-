#ifndef ENGINE_EDITOR_UI_HPP
#define ENGINE_EDITOR_UI_HPP

// Phase 14a: the first sub-phase of a larger "Phase 14: full editor UI" arc
// (see README.md's own Phase 14a section for the full writeup). This class
// owns a full-window Dear ImGui docking layout -- an always-visible editor
// "chrome" (Scene / Assets / Viewport / Inspector placeholder panels docked
// Unity/Blender-style) -- as opposed to `DebugUI` (debug_ui.hpp, Phase 8c),
// which is an F1-toggled *diagnostic* overlay meant to be hidden most of the
// time. The two classes serve genuinely different purposes and this phase
// keeps both: DebugUI's own panel content/F1-toggle behavior is completely
// unchanged (see debug_ui.hpp's own Phase 14a comment).
//
// Why EditorUI, not DebugUI, owns the shared ImGui context/backend:
// Dear ImGui supports multiple independent ImGuiContext instances in
// principle (ImGui::CreateContext() returns a handle, ImGui::
// SetCurrentContext() switches which one subsequent ImGui:: calls target),
// but imgui_impl_glfw's GLFW callbacks (key/mouse/cursor/scroll) are
// installed once per GLFWwindow*, not once per ImGuiContext -- there is no
// supported way for two independently-`ImGui_ImplGlfw_InitForOpenGL(window,
// install_callbacks=true)`-initialized contexts to both safely own callback
// dispatch for the *same* window at the same time (the second Init() call
// just clobbers the first context's own callback registration at the GLFW
// level). So "two separate ImGui subsystems layered on one window" is not a
// real option here: there can only be one live ImGui context driving this
// window's input, shared by every ImGui-based UI this engine draws in a
// given frame. This class is that one owner -- constructed unconditionally
// (no enabled/disabled gate, unlike DebugUI), since the dockspace it draws
// is meant to be on for the whole run, which makes it the natural place for
// the "always-on" shared plumbing to live. DebugUI keeps its own
// enabled_/setEnabled() state and its own panel-content function
// (Application::renderDebugUI(), application.cpp) entirely unchanged; it
// now just submits its ImGui:: widget calls *inside* the one ImGui frame
// EditorUI already started for this frame, instead of bracketing its own
// separate ImGui::NewFrame()/Render() pair the way it did through Phase 8c-
// 8e (see debug_ui.hpp's own Phase 14a comment for the mechanical rewiring
// this required and why it doesn't change what DebugUI's panel actually
// shows or when).
//
// What this class deliberately does NOT do (this phase's own scope --
// README.md's Phase 14a section, and the later Phase 14 sub-phases each own
// panel's real content): no real scene hierarchy (Phase 14d), no real
// asset browser, no render-to-texture 3D viewport (Phase 14c -- "Viewport"
// here is placeholder text only; the actual 3D scene keeps rendering
// straight to the default framebuffer, same as every prior phase, entirely
// independent of this dockspace -- see application.cpp's render()), no real
// inspector (Phase 14e), no Play/Pause/Restart or other toolbar/menu-bar
// chrome. Every one of the four panels is ImGui::Begin()/End() with a
// single ImGui::TextWrapped() placeholder line inside.
struct GLFWwindow;
typedef unsigned int ImGuiID;

namespace engine {

class EditorUI {
public:
    // `window` must already have a current GL context (mirrors DebugUI's own
    // constructor contract). Unlike DebugUI, there is no `enabled` parameter
    // -- this is always-on editor chrome, not an optional diagnostic
    // overlay, so the ImGui context/backends are created unconditionally,
    // every run (including headless verification -- see this phase's own
    // README section for why that's an intentional, expected change from
    // every prior phase's "off by default" ImGui-related flags).
    explicit EditorUI(GLFWwindow* window);
    ~EditorUI();

    EditorUI(const EditorUI&) = delete;
    EditorUI& operator=(const EditorUI&) = delete;
    EditorUI(EditorUI&&) = delete;
    EditorUI& operator=(EditorUI&&) = delete;

    // Starts this frame's single shared ImGui frame (ImGui_ImplOpenGL3_
    // NewFrame() + ImGui_ImplGlfw_NewFrame() + ImGui::NewFrame()). Call
    // exactly once per frame, before any ImGui:: widget call this frame --
    // both renderDockspaceShell() below and (when enabled) DebugUI's own
    // panel-content function -- see application.cpp's render() for the
    // exact per-frame ordering.
    void newFrame();

    // Submits the full-window dockspace host (ImGui::DockSpaceOverViewport())
    // plus the four placeholder panels (Scene, Assets, Viewport, Inspector),
    // docked into their approved-mockup layout via ImGui's DockBuilder API
    // the first time this ever runs (guarded by an internal
    // layoutBuilt_ flag, not re-run every frame) so a user's own later
    // drag-to-rearrange isn't stomped on the next frame. Call once per
    // frame, after newFrame() and before render().
    void renderDockspaceShell();

    // Finishes the frame newFrame() started (ImGui::Render()) and rasterizes
    // its combined draw data -- this class's own dockspace/panels plus
    // whatever DebugUI's panel-content function additionally submitted this
    // frame -- onto whichever framebuffer is currently bound (the default
    // framebuffer, after Application's tonemap/bloom postprocess pass, same
    // as DebugUI's own render() did through Phase 8c-8e). Call exactly once
    // per frame, last.
    void render();

private:
    void buildInitialLayout(ImGuiID dockspaceId);

    bool layoutBuilt_ = false;
};

}  // namespace engine

#endif  // ENGINE_EDITOR_UI_HPP
