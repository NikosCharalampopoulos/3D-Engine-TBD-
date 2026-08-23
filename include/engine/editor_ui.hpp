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
// asset browser, no real inspector (Phase 14e), no Play/Pause/Restart or
// other toolbar/menu-bar chrome. Scene/Assets/Inspector are still each
// ImGui::Begin()/End() with a single ImGui::TextWrapped() placeholder line
// inside, exactly as Phase 14a left them.
//
// Phase 14c: the Viewport panel is no longer placeholder text -- it now
// displays Application's own viewport-sized render target via
// ImGui::Image() (see renderDockspaceShell()'s new parameter below and
// application.cpp's render()/resizeViewportTargetsIfNeeded()). This class
// still owns no 3D rendering itself (it only ever draws a texture Application
// handed it, the same "just a Dear ImGui wrapper" role it always had) -- but
// it does now own the *size* the rest of the engine renders that 3D content
// at: the Viewport panel's own ImGui::GetContentRegionAvail(), recorded each
// time renderDockspaceShell() runs and exposed via viewportWidth()/
// viewportHeight() below for Application::render() to read at the top of
// its *next* call, before that frame's 3D pipeline runs -- see this header's
// own comment on those two getters for why "next frame", not "this frame",
// is correct.
//
// Phase 14d: the "Scene" panel's placeholder text is replaced by a real tree
// (see scene_hierarchy.hpp's buildSceneTree(), and this class's own Phase
// 14d comment on renderDockspaceShell() below) built from `registry`'s
// actual Parent-component nesting, with click-to-select; the Viewport panel
// gains an optional dashed-rectangle-plus-corner-brackets overlay around the
// selected entity's on-screen bounding box, drawn in the SAME window's own
// ImGui draw list right after ImGui::Image() so it composites on top of the
// rendered 3D content and is automatically clipped to the Viewport panel's
// own rectangle (Dear ImGui clips a window's draw commands to that window's
// visible bounds by default -- confirmed, not assumed, via this phase's own
// headless screenshot verification -- see README.md's Phase 14d section)
// rather than bleeding into the Scene/Assets/Inspector panels around it.
// EditorUI still owns none of this data itself -- `registry`/`selectedEntity`
// are Application's own state, passed in by reference each frame, matching
// this class's pre-existing "just a Dear ImGui wrapper over data Application
// owns" role (see e.g. viewportColorTexture above).

#include <optional>

#include "engine/ecs.hpp"

struct GLFWwindow;
typedef unsigned int ImGuiID;

namespace engine {

// Phase 14d: the currently-selected entity's on-screen bounding box, already
// projected into normalized device coordinates ([-1, 1] on both axes, +Y up
// -- this engine's ordinary clip-space convention, e.g. Frustum/BoundingSphere)
// by Application::render() (see that method's own Phase 14d comment for the
// "project center +/- radius along the camera's own right/up axes" technique
// used to build it) -- EditorUI does no 3D math of its own, it only maps this
// already-computed NDC rect onto the Viewport panel's own current on-screen
// pixel rectangle (see renderDockspaceShell()'s own Phase 14d comment).
struct SelectionOutline {
    float ndcMinX = 0.0f;
    float ndcMinY = 0.0f;
    float ndcMaxX = 0.0f;
    float ndcMaxY = 0.0f;
};

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
    // plus the four panels (Scene, Assets, Viewport, Inspector), docked into
    // their approved-mockup layout via ImGui's DockBuilder API the first
    // time this ever runs (guarded by an internal layoutBuilt_ flag, not
    // re-run every frame) so a user's own later drag-to-rearrange isn't
    // stomped on the next frame. Call once per frame, after newFrame() and
    // before render().
    //
    // Phase 14c: `viewportColorTexture` is the raw GL texture id of
    // Application's own viewport-sized render target (Framebuffer::
    // colorTextureId(), see application.cpp's render()) -- this frame's
    // already-finished 3D render, at whatever size the Viewport panel
    // reported *last* frame (see viewportWidth()/viewportHeight() below).
    // Drawn via ImGui::Image(), flipped vertically (uv0=(0,1), uv1=(1,0)) to
    // correct for OpenGL's bottom-left texture origin vs. Dear ImGui's
    // top-left image convention -- confirmed visually, not just assumed (see
    // this phase's README section). Pass 0 to skip the image entirely (no
    // 3D render exists yet to show, or its texture id isn't known -- neither
    // currently happens in Application's own call sequence, but this keeps
    // the method well-defined rather than sampling texture id 0). Still
    // records the panel's own current ImGui::GetContentRegionAvail() every
    // call, regardless of whether an image was drawn -- that's what
    // viewportWidth()/viewportHeight() return.
    //
    // Phase 14d: three new parameters.
    //   - `registry`: rebuilds the Scene panel's tree fresh every call (via
    //     scene_hierarchy.hpp's buildSceneTree() -- a handful of entities, so
    //     no reason to cache/diff this against a previous frame) instead of
    //     the old placeholder text, and reads/writes `selectedEntity` as the
    //     user clicks rows.
    //   - `selectedEntity`: Application's own selection state (see
    //     application.hpp's own Phase 14d comment for why Application, not
    //     EditorUI, owns it) -- passed by reference so a click inside this
    //     one call can update it directly, and so Application::render()
    //     (which builds `outline` below from this same value, earlier in the
    //     very same render() call, before renderDockspaceShell() runs) and
    //     a later Phase 14e Inspector panel can both read the exact same
    //     value back.
    //   - `outline`: nullptr when nothing is selected (no rectangle drawn --
    //     this phase's own "no selection = no outline" default state), else
    //     the selected entity's screen-space bounding box in NDC (see
    //     SelectionOutline's own comment above), which this method maps onto
    //     the Viewport panel's *current* on-screen pixel rectangle (captured
    //     via ImGui::GetCursorScreenPos() before ImGui::Image() advances the
    //     cursor, alongside the same ImGui::GetContentRegionAvail() the image
    //     itself is sized to) and draws as a dashed rectangle + four small
    //     corner brackets, via ImGui::GetWindowDrawList() so it lands in the
    //     Viewport window's own draw list -- see this method's own .cpp
    //     comment for why that, rather than the global foreground draw list,
    //     is what keeps this overlay from ever bleeding into the panels
    //     around it.
    void renderDockspaceShell(unsigned int viewportColorTexture, EntityRegistry& registry,
                               std::optional<EntityId>& selectedEntity, const SelectionOutline* outline);

    // The Viewport panel's own most recently recorded
    // ImGui::GetContentRegionAvail(), from the last renderDockspaceShell()
    // call -- 0 before that has ever run once. Application::render() reads
    // these at the very top of every frame, BEFORE that frame's 3D pipeline
    // runs, to size/resize every offscreen render target and to pick the
    // camera's own projection aspect ratio (see application.cpp's own Phase
    // 14c comment). That means a given frame's 3D content is always sized to
    // the Viewport panel's *previous* frame's dimensions, one frame stale --
    // deliberate, not a bug: the panel's size only actually changes when a
    // user drags a divider/redocks a panel (rare after initial layout), so
    // this is the same "read last frame's known layout, apply it to this
    // frame's render" latency every real engine editor's own render-to-
    // texture viewport has (there is no other order that doesn't require
    // either rendering the 3D scene *after* submitting the ImGui panel that
    // displays it within the same ImGui frame, which Dear ImGui's own
    // immediate-mode API doesn't support, or moving this whole class's
    // newFrame()/renderDockspaceShell() pair to the very top of render(),
    // which would just relocate the same one-frame gap, not remove it).
    int viewportWidth() const { return viewportWidth_; }
    int viewportHeight() const { return viewportHeight_; }

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
    // Phase 14c: see viewportWidth()/viewportHeight() above. 0 until
    // renderDockspaceShell() has run at least once -- Application's own
    // constructor seeds its mirror of these (viewportWidth_/viewportHeight_,
    // application.hpp) from the window's initial size instead, so a 0 read
    // here never reaches an actual framebuffer construction/resize call.
    int viewportWidth_ = 0;
    int viewportHeight_ = 0;
};

}  // namespace engine

#endif  // ENGINE_EDITOR_UI_HPP
