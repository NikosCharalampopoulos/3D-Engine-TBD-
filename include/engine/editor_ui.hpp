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
// Phase 17c update: the Viewport panel now DOES have a toolbar row
// (renderViewportToolbar(), editor_ui.cpp) -- but "Play/Pause/Restart" is
// still an accurate description of what it does NOT provide: its Play/
// Pause buttons (and its grid/undo buttons) are shown, matching a reference
// mockup, but BeginDisabled()'d -- this engine still has no real
// play/pause/restart simulation-state concept anywhere, only two of the
// toolbar's six buttons (lighting/texture-mode) are genuinely wired to
// real, working Application state. See README.md's own Phase 17c section
// for the full honest breakdown of which is which and why.
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
//
// Phase 14e: the "Inspector" panel's placeholder text is replaced by a real,
// live editor for whatever entity `selectedEntity` currently points at --
// renderDockspaceShell()'s own parameter list is UNCHANGED this phase
// (`registry`/`selectedEntity` were already threaded through for Phase 14d's
// tree/outline, and this phase's Inspector reads/writes those exact same two
// references, nothing new to plumb). Transform (position/Rot-Y/scale) is
// fully live; Material is read-only display (see material.hpp's own Phase
// 14e comment for why: Model instances are cached/shared across entities via
// ResourceManager, so a mutable Material reference here would let editing
// one entity silently repaint every other entity sharing the same cached
// Model); Physics exposes the real static/dynamic split via physics.hpp's
// new setEntityStatic() -- toggling "Static (Immovable)" genuinely adds/
// removes a RigidBody component, not a cosmetic flag. See editor_ui.cpp's
// own Phase 14e comment (at renderInspectorPanel()) for the full
// section-by-section design.
//
// Phase 14f: two real, working changes closing this phase's own two
// placeholder-labeled gaps.
//   - The Inspector's "Delete Object" button (Phase 14e left it
//     BeginDisabled()'d, with an explicit "Deletion is Phase 14f" caption)
//     is now live: it calls transform_hierarchy.hpp's
//     destroyEntityOrphaningChildren() on the selected entity and clears
//     `selectedEntity` -- see editor_ui.cpp's own renderInspectorPanel()
//     comment for exactly why clearing it there, not merely relying on this
//     phase's own defensive "Selected entity no longer has a Transform"
//     fallback (Phase 14e), is the correct behavior.
//   - The Scene panel gains a real Create menu -- a "+" button (also
//     reachable by right-clicking the panel's own background) offering
//     Cube/Sphere/Plane/Empty (all real, working) plus Point Light/
//     Directional Light/Camera (shown, but BeginDisabled()'d with an
//     explanatory tooltip -- see this file's own CreateEntityKind comment
//     and editor_ui.cpp's own Phase 14f comment for why those three are
//     deliberately NOT implemented this phase). renderDockspaceShell() now
//     RETURNS a CreateEntityKind (see that type's own comment below) rather
//     than gaining a fourth reference parameter -- EditorUI itself builds no
//     entities; Application::render() is what actually acts on a non-kNone
//     return value, since only Application owns the ResourceManager/Shader/
//     Camera a new entity needs.
//
// Phase 15a: "Point Light" in that same Create menu is now real too -- see
// light.hpp for the new PointLight ECS component this adds and
// Application::spawnEntityFromCreateMenu()'s own Phase 15a comment for what
// it builds. Directional Light and Camera stay BeginDisabled()'d -- see
// editor_ui.cpp's own Phase 15a comment on renderCreateEntityMenuItems() for
// why each is still its own separately-scoped follow-up. The Inspector also
// gains a live "Light" section (color + attenuation, editable exactly like
// Transform already is -- see renderInspectorPanel()'s own Phase 15a
// comment) for any selected entity that has a PointLight component.
//
// Phase 15b: "Directional Light" is real too now -- see light.hpp for the new
// DirectionalLight ECS component and Application::spawnEntityFromCreateMenu()'s
// own Phase 15b comment for what it builds. Camera alone stays
// BeginDisabled()'d -- see editor_ui.cpp's own Phase 15b comment on
// renderCreateEntityMenuItems() for why. renderDockspaceShell() below gains
// one new parameter, `activeDirectionalLight` -- Application's own
// activeDirectionalLight_ (application.hpp), read-only from EditorUI's own
// side (only Application::spawnEntityFromCreateMenu() ever changes it, on
// creation -- see that member's own comment for the "most recently created"
// rule), passed through to the Inspector's new "Light" section for a
// selected DirectionalLight entity so it can show whether THIS entity is
// the one actually driving the scene's single uLightDirection/uLightColor
// pair right now, or just an inactive entity that happens to also have the
// component -- see renderInspectorPanel()'s own Phase 15b comment for why
// that distinction matters and is worth surfacing, not just the color/
// direction fields themselves.
//
// Phase 15c: "Camera" is real too now, closing out this whole Phase 14f-
// inherited "Light/Camera" Create-menu gap -- see camera_component.hpp for
// the new CameraComponent this adds and Application::
// spawnEntityFromCreateMenu()'s own Phase 15c comment for what it builds.
// Unlike Directional Light, this gains NO new renderDockspaceShell()
// parameter -- there is no "active camera" concept to thread through (see
// camera_component.hpp's own header comment for exactly why not): a
// CameraComponent entity is a real, selectable, inspectable object with a
// live Inspector "Camera" section (fov/near/far, editor_ui.cpp's own Phase
// 15c comment), but it does not drive this engine's actual rendered view
// this phase, so there is nothing analogous to activeDirectionalLight for
// this panel to display.
//
// Phase 15d: the "Assets" panel's placeholder text is replaced by a real,
// read-only file/folder tree of assets/models/ and assets/textures/ (see
// asset_browser.hpp's own header comment for exactly which subdirectories
// are "browsable" and why) with expand/collapse and click-to-select, the
// same TreeNodeEx()-based interaction renderSceneTreeNode() already
// established for the Scene panel -- see editor_ui.cpp's own Phase 15d
// comment (at renderAssetTreeNode()) for the row-drawing half. Unlike the
// Scene panel's tree, this one is built exactly ONCE, in this class's own
// constructor, not every renderDockspaceShell() call -- see
// asset_browser.hpp's own "Caching" comment for why a filesystem tree that
// cannot change at runtime today doesn't need buildSceneTree()'s own
// "rebuilt fresh every frame" treatment. The selected row is this class's
// own private state (selectedAssetPath_ below), not threaded through
// renderDockspaceShell() the way selectedEntity is: nothing outside this
// class reads it this phase (no Inspector wiring, no viewport hookup, no
// drag-and-drop -- all deliberately out of this phase's own scope, see
// README.md's Phase 15d section), so it's a purely cosmetic "which row is
// highlighted" concern local to this one panel, with none of
// selectedEntity's cross-panel/cross-frame reasons (application.hpp's own
// Phase 14d comment) to live on Application instead.
//
// Phase 15e: this class submits this engine's first-ever menu bar (see
// renderDockspaceShell()'s own .cpp comment for the mechanics) -- a single
// "File" menu holding one item, "Save Scene" (shortcut hint "Ctrl+S", though
// the actual keyboard shortcut itself is handled entirely outside this
// class -- see application.cpp's own Phase 15e comment on run() for why).
// Deliberately just this one item, not a speculative full File/Edit/View/
// Window menu structure this project's own established "smallest correct
// increment" discipline would flag as premature (see e.g. physics.hpp's own
// RigidBody mass-field comment, or light.hpp's own "no scene serialization
// for a component nothing writes yet" Phase 15a/15b precedent, for the
// identical instinct applied elsewhere): a bare Ctrl+S with no on-screen
// affordance at all would be genuinely undiscoverable in an editor with no
// prior menu bar of any kind, so this phase adds the smallest visible
// surface that fixes that -- one menu, one item -- rather than either
// nothing (undiscoverable) or a full menu bar's worth of items this engine
// has no other actions to put there yet.
//
// Phase 15f: the Material Inspector's own "Browse..." button (Phase 14e --
// disabled ever since, with an explanatory "read-only, shared Model" comment
// -- see editor_ui.cpp's own Phase 14e comment) is real now: it opens a
// popup listing every file under assets/textures/, reusing this class's own
// assetTree_ (Phase 15d, below) rather than re-walking the filesystem a
// second time. Picking one reports the chosen path back via
// renderDockspaceShell()'s new `textureAssignRequested` parameter (below);
// Application::render() is what turns that into a real MaterialOverride
// component (material_override.hpp) on the selected entity -- see that
// header's own header comment for the full "per-entity override, not
// clone-on-edit" design this closes the shared-cache hazard with, and
// editor_ui.cpp's own Phase 15f comment (at renderInspectorPanel()) for the
// Material section's own updated layout.
//
// Phase 15g: the last item in the Phase 15 arc -- real drag-and-drop, the
// interaction pattern this whole arc's roadmap named from the start
// (Blender's own primary Asset Browser gesture) and every phase since 15d
// deliberately deferred (asset_browser.hpp's and material_override.hpp's own
// "Deliberately not done this phase" lists both name it explicitly). Two
// changes, built entirely on top of 15d's and 15f's own existing mechanisms,
// not a new one:
//   - Every row renderAssetTreeNode() draws (Phase 15d, below) is now also a
//     real Dear ImGui drag source (ImGui::BeginDragDropSource()/
//     SetDragDropPayload()), carrying its own assets/-relative path (the
//     SAME "assets/models/foo.obj"-shaped string ModelComponent::path/
//     MaterialOverride::diffuseTexturePath already use) as a flat, one-type
//     payload -- see editor_ui.cpp's own Phase 15g comment (at
//     renderAssetTreeNode()) for the exact payload shape and why there's no
//     separate "category" field alongside the path.
//   - The Viewport panel's ImGui::Image() (Phase 14c) is now also a real
//     drop target (ImGui::BeginDragDropTarget()/AcceptDragDropPayload()),
//     reported back via this method's new `assetDropRequested` out-parameter
//     below -- the identical "EditorUI reports intent, Application acts on
//     it" shape textureAssignRequested/createRequest/saveSceneRequested
//     already establish. EditorUI itself does no classification of what was
//     dropped (model vs. texture vs. something unrecognized) -- that's
//     engine::classifyAssetDropPath() (asset_drop.hpp), a pure function with
//     no ImGui dependency at all, called from Application::
//     handleViewportAssetDrop() (application.cpp) once this value comes back
//     non-nullopt.
//
// Phase 16: the Viewport panel gains one more piece of interaction --
// double-clicking anywhere inside it (not on a specific object: this engine
// has no click-to-select-an-object-IN-THE-3D-VIEWPORT feature at all, today
// or before this phase -- only the Scene Hierarchy tree's own click-to-select,
// see scene_hierarchy.hpp -- so "empty space" is, in practice, simply
// "anywhere inside the Viewport panel," since there is nothing else a click
// there could possibly hit) requests that Application's free-fly camera_
// start capturing keyboard/mouse input, fixing a real bug the project owner
// hit on the actual Windows build: camera_ used to read WASD/mouse
// unconditionally every frame, moving even when the user was clicking
// elsewhere in the window or just passing the mouse over the app with no
// intent to fly the camera at all. See camera_capture.hpp's own header
// comment for the full design and application.cpp's own Phase 16 comments
// (run()/render()/update()) for how the resulting request is consumed.
// Detected via `ImGui::IsWindowHovered() && ImGui::IsMouseDoubleClicked(...)`
// called INSIDE this "Viewport" panel's own Begin()/End() block (see this
// class's own renderDockspaceShell() .cpp comment, at the Viewport panel) --
// the only way to scope the check to specifically this ONE docked panel
// rather than the whole window, given ImGui's own dockspace layout, is to
// let ImGui itself answer "is the mouse over which panel," which only a
// call made from inside that panel's own Begin/End block can do.
//
// Phase 17d: the fourth and last item in the "Phase 17: visual design" arc
// (17a base theme, 17b icon font, 17c toolbar -- each named this class's own
// custom window chrome as explicitly out of scope; see e.g. 17a's own
// README section: "any custom window chrome/borderless rounded OUTER window
// border (Phase 17d -- real platform-level borderless-window work, much
// bigger scope...)"). This class gains a fourth always-drawn row -- a
// custom-drawn title bar (renderTitleBar(), private, editor_ui.cpp) stacked
// ABOVE the Phase 15e File menu bar via the identical
// ImGui::BeginViewportSideBar()-based reservation mechanism
// ImGui::BeginMainMenuBar() itself already uses internally (imgui_widgets.cpp)
// -- app icon/name on the left, minimize/maximize-restore/close buttons on
// the right, replacing the OS's own native title bar the Window class now
// optionally omits entirely (window.hpp's own Phase 17d `decorated`
// parameter). EditorUI itself still performs no window-level GLFW call of
// its own here (window.hpp's Window class owns every real
// glfwSetWindowPos()/glfwIconifyWindow()/etc. call) -- the identical "EditorUI
// reports intent via an out-parameter, Application acts on it" shape this
// whole class already follows for createRequest/saveSceneRequested/
// cameraCaptureRequested/etc. above, just applied to window-chrome intent
// instead of scene/asset intent. See TitleBarAction's own comment below for
// exactly what gets reported, and README.md's own Phase 17d section for the
// full design plus this phase's own real, honest verification ceiling (no
// real window manager runs in this project's own headless Xvfb dev/CI
// target, so dragging/minimizing/maximizing/closing via this new UI could
// only be verified by code-reading/logical-soundness, not by an actual
// interactive headless run -- see that section for the full accounting).

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "engine/asset_browser.hpp"
#include "engine/ecs.hpp"
#include "engine/gizmo.hpp"
#include "engine/shading_mode.hpp"
#include "engine/undo_stack.hpp"

struct GLFWwindow;
typedef unsigned int ImGuiID;
// Phase 18e: forward-declared (like ImGuiID above) purely so updateGizmo()'s
// own private declaration below can take a few of Dear ImGui's plain
// ImVec2 structs by const reference without this header pulling in the
// whole of <imgui.h> the way editor_ui.cpp itself does -- an incomplete
// type is sufficient for a reference parameter in a declaration with no
// definition in this file.
struct ImVec2;

namespace engine {

// Phase 14f: what (if anything) the Scene panel's Create menu was clicked
// for THIS frame -- returned by renderDockspaceShell() below rather than
// EditorUI acting on it directly, matching this class's own "just a Dear
// ImGui wrapper over data Application owns" role (see this file's own
// header comment further up): EditorUI has no ResourceManager/Shader/Camera
// to actually build a new entity from (Application owns all three), so it
// only ever reports the user's intent for Application::render() to act on
// right after this call returns -- the same "EditorUI edits Application's
// own state by reference/return value, never owns the underlying systems
// itself" shape selectedEntity/outline already establish for selection.
//
// kNone every frame nothing was clicked -- the overwhelmingly common case --
// so a caller can simply `if (request != CreateEntityKind::kNone)` rather
// than needing a separate std::optional wrapper around this enum.
enum class CreateEntityKind {
    kNone,
    kCube,
    kSphere,
    kPlane,
    kEmpty,
    // Phase 15a: a real point light entity (Transform + NameComponent +
    // light.hpp's PointLight component, no ModelComponent -- see
    // Application::spawnEntityFromCreateMenu()'s own Phase 15a comment).
    kPointLight,
    // Phase 15b: a real directional light entity (Transform + NameComponent +
    // light.hpp's DirectionalLight component, no ModelComponent -- same
    // "no light-gizmo mesh to draw" shape as kPointLight above -- see
    // Application::spawnEntityFromCreateMenu()'s own Phase 15b comment). Also
    // becomes this Application's new activeDirectionalLight_ the instant it's
    // created (see that member's own application.hpp comment for the "most
    // recently created" rule) -- so, unlike kPointLight, creating one has an
    // immediate side effect on what the ONE directional light this engine
    // actually renders/casts shadows with looks like, not just "one more
    // entity added to a budget."
    kDirectionalLight,
    // Phase 15c: a real camera entity (Transform + NameComponent +
    // camera_component.hpp's CameraComponent, no ModelComponent -- see
    // Application::spawnEntityFromCreateMenu()'s own Phase 15c comment).
    // Deliberately has NO side effect analogous to kDirectionalLight's own
    // activeDirectionalLight_ assignment above -- see camera_component.hpp's
    // own header comment for exactly why this entity does not become, or
    // need, an "active camera": this engine's actual rendered view still
    // comes entirely from Application's own free-fly camera_ object, which
    // this phase does not touch.
    kCamera,
};

// Phase 17d: this frame's outcome from the custom title bar (renderTitleBar(),
// editor_ui.cpp) -- unconditionally reset to its all-false/nullopt default at
// the top of every renderDockspaceShell() call, the identical "false/empty
// every frame except the one where the real thing actually happened" shape
// saveSceneRequested/textureAssignRequested/assetDropRequested/
// cameraCaptureRequested already establish (see this class's own Phase 15e/
// 15f/15g/16 header comments above). Application::render() is what actually
// calls Window's own setWindowPos()/iconifyWindow()/toggleMaximizeRestore()/
// requestClose() when the corresponding field comes back true/non-nullopt,
// immediately after renderDockspaceShell() returns -- EditorUI performs no
// window-level GLFW call of its own (see this file's own Phase 17d header
// comment above).
struct TitleBarAction {
    // Set true the one frame the minimize button was clicked.
    bool minimizeRequested = false;
    // Set true the one frame the maximize/restore button was clicked OR the
    // empty (non-button) title-bar area was double-clicked -- both gestures
    // mean the identical thing (window_chrome.hpp's own
    // decideMaximizeRestoreToggle()), so both funnel into this one flag
    // rather than two separately-named ones Application would otherwise have
    // to treat identically anyway.
    bool maximizeToggleRequested = false;
    // Set true the one frame the close button was clicked.
    bool closeRequested = false;
    // Set (to the new {x, y} screen position window_chrome.hpp's own
    // applyDragDelta() computed) on every frame a title-bar drag is actively
    // in progress AND this frame's raw mouse-movement delta (Dear ImGui's own
    // io.MouseDelta) is nonzero -- std::nullopt otherwise (no drag in
    // progress, or a drag in progress with the mouse perfectly still this
    // particular frame).
    std::optional<std::pair<int, int>> requestedWindowPos;
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
    // Phase 14d: two new parameters (originally three -- see the Phase 18d
    // paragraph below for why the third, `outline`, is gone).
    //   - `registry`: rebuilds the Scene panel's tree fresh every call (via
    //     scene_hierarchy.hpp's buildSceneTree() -- a handful of entities, so
    //     no reason to cache/diff this against a previous frame) instead of
    //     the old placeholder text, and reads/writes `selectedEntity` as the
    //     user clicks rows.
    //   - `selectedEntity`: Application's own selection state (see
    //     application.hpp's own Phase 14d comment for why Application, not
    //     EditorUI, owns it) -- passed by reference so a click inside this
    //     one call can update it directly, and so Application::render()
    //     (which draws the selected entity's mesh into a Phase 18d selection
    //     mask from this same value, earlier in the very same render() call,
    //     before renderDockspaceShell() runs) and the Inspector panel can
    //     both read the exact same value back.
    //
    // Phase 18d: the selection outline itself is no longer anything this
    // class draws. It replaced Phase 14d's flat 2D screen-space dashed-
    // rectangle-plus-corner-brackets gizmo (the `outline`/`SelectionOutline*`
    // parameter this signature used to carry, and the addDashedRect()/
    // addCornerBrackets() helpers + the draw-list block that called them,
    // editor_ui.cpp) with a real silhouette-based 3D outline baked directly
    // into `viewportColorTexture` itself by Application's own postprocess
    // pass (a selection mask render + screen-space edge-detection composite
    // -- see application.cpp's renderSelectionMask()/postprocess.frag's own
    // Phase 18d comments) -- so by the time this method's `viewportColorTexture`
    // argument reaches the ImGui::Image() call below, the outline is already
    // part of the image, with nothing left for EditorUI's own draw list to
    // add on top. Removed rather than left alongside the new mechanism as
    // dead code, per this phase's own "replacement, not addition" brief.
    //
    // Phase 14f: return value -- CreateEntityKind::kNone every frame except
    // the one where the Scene panel's Create menu (a "+" button, opened via
    // either a click on it or a right-click anywhere in the panel's own
    // background -- see editor_ui.cpp's own Phase 14f comment) had one of
    // its real, enabled items (Cube/Sphere/Plane/Empty) clicked this frame.
    // See CreateEntityKind's own comment above for why this is a return
    // value, not a third read/write reference parameter alongside
    // `selectedEntity` -- unlike selection, "what was just requested"
    // doesn't need to persist across frames or be readable from outside this
    // one call, only acted on once, immediately after it's returned.
    // Phase 15b: `activeDirectionalLight` -- see this class's own header
    // comment (Phase 15b paragraph) for what it's for and why it's a plain
    // std::optional<EntityId> BY VALUE (read-only here, unlike
    // `selectedEntity`'s read-write reference above) rather than a second
    // reference parameter: EditorUI never assigns Application's
    // activeDirectionalLight_ itself, only displays it, so there is nothing
    // for a reference to let this function write back.
    //
    // Phase 18g: `hasActiveCamera`, the identical read-only-BY-VALUE shape,
    // mirrors Application's own resolveActiveCamera(registry_).active.valid()
    // this frame (camera_component.hpp) -- true once a scene Camera entity
    // exists. renderCreateEntityMenuItems() (editor_ui.cpp) reads it to
    // BeginDisabled() the Create menu's own "Camera" item the same way it
    // already disables an item that has no real handler at all (this
    // file's own CreateEntityKind comment) -- except here the item DOES have
    // a real handler; it's disabled specifically to enforce this project's
    // confirmed "at most one Camera entity" rule, re-enabling itself the
    // instant that one entity is deleted (since `hasActiveCamera` is
    // recomputed fresh every call, from the registry's actual live content,
    // not a one-time snapshot).
    //
    // Phase 15e: `saveSceneRequested` -- unconditionally set to false at the
    // top of every call, then true only if this frame's new File > Save
    // Scene menu item (see this class's own Phase 15e header comment) was
    // clicked. A read/write reference, not folded into CreateEntityKind's
    // own return value the way "what was just requested" might first
    // suggest: CreateEntityKind is specifically "which kind of entity to
    // create" (a closed, mutually-exclusive choice -- exactly one item in
    // one popup menu can be clicked in a single frame), and Save Scene is a
    // wholly separate action a user could in principle trigger the SAME
    // frame as a Create click (unlikely with a mouse, but there's no reason
    // to bake in an assumption that can't happen) -- so it gets its own
    // independent out-parameter instead of an awkward "and also maybe
    // kSaveScene" case bolted onto an enum whose name and existing five
    // values are all specifically about entity creation. EditorUI still
    // never performs the save itself here (no ResourceManager/registry_
    // owned by this class to build one from, the same "just a Dear ImGui
    // wrapper" reasoning `createRequest`'s own comment above already gives)
    // -- Application::render() is what actually calls saveCurrentScene()
    // when this comes back true, immediately after this call returns.
    // Phase 15f: `textureAssignRequested` -- unconditionally reset to
    // std::nullopt at the top of every call, then set to the picked file's
    // assets/-relative path (e.g. "assets/textures/foo.png") only on the
    // frame a user clicks an entry in the Material Inspector's new
    // "Browse..." popup (see editor_ui.cpp's own renderTextureBrowsePopup()/
    // renderInspectorPanel() Phase 15f comments). A read/write reference,
    // mirroring `saveSceneRequested`'s own shape immediately above and for
    // the identical reason: EditorUI has no ResourceManager to actually load
    // a Texture through (only Application does), so this call only ever
    // reports WHICH path was picked -- Application::render() is what turns
    // a non-nullopt result into a real resources_.getTexture() call and a
    // MaterialOverride component (material_override.hpp) on
    // `selectedEntity`, immediately after this call returns.
    //
    // Phase 15g: `assetDropRequested` -- the identical "unconditionally
    // reset to std::nullopt at the top of every call, set only on the frame
    // something real happened" shape `textureAssignRequested` immediately
    // above already established, this time set on the frame a user actually
    // releases a drag over the Viewport panel's own ImGui::Image() (see this
    // class's own Phase 15g header comment and editor_ui.cpp's own
    // renderDockspaceShell() Phase 15g comment for the exact
    // BeginDragDropTarget()/AcceptDragDropPayload() mechanics). Carries the
    // SAME assets/-relative path string the Assets panel's own drag source
    // set as its payload -- EditorUI does not itself decide whether that
    // path names a model or a texture (see this class's own Phase 15g header
    // comment for why that decision is a separate, pure function, not
    // something ImGui-facing code should own); Application::
    // handleViewportAssetDrop() (application.cpp) is what classifies and
    // acts on a non-nullopt result, immediately after this call returns.
    //
    // Phase 16: two new parameters, closing out this method's own header
    // comment above. `cameraCaptured`: Application's own cameraCaptured_,
    // read-only BY VALUE (the identical shape `activeDirectionalLight`
    // already has above, for the identical reason -- EditorUI never assigns
    // it, only reads it) -- gates the Viewport's own double-click check so
    // it can only ever fire while NOT already captured, avoiding a
    // redundant/confusing re-trigger while the camera is already flying
    // (belt-and-suspenders alongside ImGuiConfigFlags_NoMouse, which
    // Application sets for exactly this state and which independently makes
    // IsWindowHovered() unable to return true at all while captured -- see
    // application.cpp's own Phase 16 render() comment; camera_capture.hpp's
    // own decideCameraCapture() ALSO tolerates a stray request while already
    // captured as a defensive no-op, so this is the third, not the only,
    // layer against it). `cameraCaptureRequested`: unconditionally reset to
    // false at the top of every call, the identical "false/empty every
    // frame except the one where the real thing actually happened" shape
    // saveSceneRequested/textureAssignRequested/assetDropRequested above all
    // already establish -- set true only on the frame the gated double-click
    // above actually fires. EditorUI performs no side effect of its own here
    // (no cursor-mode call, no Camera reference to reset) -- Application is
    // what owns window_/camera_, so, like every other out-parameter this
    // method already has, this only ever reports the user's intent for
    // Application to act on right after this call returns.
    // Phase 17c: two more trailing parameters, both plain `bool&` -- the
    // Viewport panel's own new toolbar row (editor_ui.cpp's own
    // renderViewportToolbar()) reads/writes them directly, exactly the same
    // "EditorUI mutates Application's own state through a reference, no
    // getter/setter round-trip" shape `selectedEntity`/`cameraCaptureRequested`
    // above already use. Originally (Phase 17c-18e) these two were
    // `ssaoDisabled`/`ssaoDebugMode` -- Application's own ssaoDisabled_/
    // ssaoDebugMode_ (application.hpp, Phase 13f), the SAME two members the
    // F1 debug overlay's own "Render Passes" checkboxes already bind by
    // address (Application::renderDebugUI(), application.cpp) -- bound to
    // this toolbar's "lighting"/"texture-mode" buttons purely because they
    // were the only two Application flags this toolbar had anything real to
    // toggle at the time; the two F1 checkboxes remained the actual owning
    // UI for that pair the whole time.
    //
    // Phase 18g repurposes those same two buttons for real Wireframe/Solid/
    // Rendered shading-mode control (see shading_mode.hpp's own header
    // comment for the full design), which is enough of a DIFFERENT feature
    // -- three states spread across two buttons, not two independent
    // booleans -- that a single `ShadingMode& editShadingMode` parameter
    // replaces the old `bool& ssaoDisabled, bool& ssaoDebugMode` pair here.
    // ssaoDisabled_/ssaoDebugMode_ themselves are COMPLETELY UNCHANGED --
    // still real Application members, still toggled by the F1 overlay's own
    // checkboxes exactly as before -- this toolbar simply stops being a
    // second way to reach them. `editShadingMode` is Application's own
    // editShadingMode_ (application.hpp's own Phase 18g comment) -- the
    // EDIT-mode choice, mutated ONLY by a real toolbar button click here,
    // never by Play/Pause (see that member's own comment for why "never
    // touched by entering/leaving Play mode" is what makes Play-forces-
    // Rendered/Edit-restores-its-own-choice fall out with no extra
    // bookkeeping). `editor_icons.hpp`'s own ToolbarButton/
    // toolbarButtonIconGlyph() comment records why only these two of the
    // toolbar's six buttons have a real flag to bind to at all; the other
    // four (grid/undo/play/pause) are shown but BeginDisabled()'d, needing
    // no Application state to read/write in the first place.
    // Phase 17d: four more trailing parameters, for the new custom title bar
    // (renderTitleBar(), private, editor_ui.cpp). `showCustomTitleBar`:
    // Window::isDecorated() negated -- false when the OS's own native
    // decorations are on (the ENGINE_WINDOW_DECORATED=1 escape hatch,
    // main.cpp), in which case renderTitleBar() draws nothing at all rather
    // than a redundant second title bar on top of the OS's real one (see
    // that method's own comment above). `windowMaximized`: a read-only
    // snapshot of Window::isMaximized(), the identical shape
    // `cameraCaptured`/`activeDirectionalLight` above already use for state
    // EditorUI only ever DISPLAYS, never assigns -- lets the title bar's
    // maximize/restore button draw the correct one of its two glyphs.
    // `windowPos`: a read-only snapshot of Window::getWindowPos() (the
    // window's own on-screen position AS OF THE START of this call), the one
    // piece of Window's own live state a title-bar drag this frame needs as
    // its "current position" input to window_chrome.hpp's own
    // applyDragDelta() -- EditorUI still owns no GLFWwindow* of its own to
    // query this from directly (see this file's own Phase 17d header
    // comment), so, like `windowMaximized`, Application passes in what it
    // already read from window_ this same frame. `titleBarAction`: the one
    // out-parameter carrying every real effect a click/drag/double-click on
    // the new title bar can have this frame -- see TitleBarAction's own
    // comment above for exactly what each field means and why they're
    // bundled into one struct rather than four more separate parameters
    // appended to an already-long signature.
    // Phase 18b: one more trailing `bool&`, `physicsRunning` -- Application's
    // own new `physicsRunning_` (application.hpp), the real Edit/Play mode
    // flag `stepPhysics()` is now gated behind (see that call site's own
    // application.cpp comment). Threaded through exactly the SAME "EditorUI
    // mutates Application's own state directly through a reference, no
    // getter/setter round-trip" shape `ssaoDisabled`/`ssaoDebugMode` above
    // already use, not a read-only snapshot plus a separate out-flag the way
    // `cameraCaptured`/`cameraCaptureRequested` are: like those two SSAO
    // toggles (and unlike camera capture -- see that pair's own Phase 16
    // comment on why IT needs the two-flag split, since entering/exiting
    // capture also has to call window_.setCursorCaptured() from
    // Application's own side), a click on the toolbar's Play/Pause button
    // has no OTHER side effect Application needs to gate on its own side of
    // any render()/run() boundary -- EditorUI performing the assignment
    // immediately, in place, is already the entire effect either button
    // click has (Application::update() re-reads it next frame the same way
    // it already re-reads ssaoDisabled_/ssaoDebugMode_ after this same
    // call).
    //
    // Phase 18e: three more trailing parameters -- `cameraPosition`/
    // `cameraView`/`cameraProjection`, Application's own camera_.position()/
    // getViewMatrix()/getProjectionMatrix(aspect) for THIS frame, read-only
    // (the identical by-value-snapshot shape `cameraCaptured`/
    // `activeDirectionalLight` above already use for state EditorUI only
    // ever reads, never assigns). The translate gizmo's own interaction --
    // hit-testing which axis handle (if any) the mouse is over, and running
    // the drag itself -- happens entirely INSIDE this call's own Viewport
    // panel Begin()/End() block (updateGizmo(), private, below), the only
    // place Dear ImGui's own IsWindowHovered()/mouse queries can be scoped
    // to that one docked panel (the identical reason the Phase 16
    // double-click-to-capture check already lives there, not in
    // Application). Unlike every other interactive feature this class
    // reports back via an out-parameter for Application to act on, a gizmo
    // drag is applied DIRECTLY here, mutating `registry`'s own Transform
    // component in place -- the exact same "EditorUI mutates registry_'s
    // Transform straight through the reference it's holding" pattern
    // renderInspectorPanel()'s own Position DragFloat3 already establishes,
    // just driven by a mouse drag instead of a typed number. See
    // gizmo.hpp's own header comment for the full design (including why a
    // WORLD-space drag delta is applied to the entity's LOCAL Transform
    // position, unconverted through any parent) and updateGizmo()'s own
    // comment below for exactly how the interaction itself is scoped/gated
    // against the toolbar and Phase 16 camera capture.
    // Phase 18h: seven more trailing parameters, closing out the Viewport
    // toolbar's own "undo" stub (BeginDisabled()'d since Phase 17c,
    // explicitly waiting for this) and the Inspector's "Delete Object"
    // button's own registry mutation, both of which now report intent
    // instead of acting directly -- the exact same "EditorUI reports, only
    // Application acts" shape createRequest/saveSceneRequested/
    // textureAssignRequested/assetDropRequested above already establish,
    // now extended to cover deletion and completed transform edits too
    // (undo_stack.hpp's own header comment has the full "why" for both).
    //
    // `deleteEntityRequested`: unconditionally reset to std::nullopt at the
    // top of every call, then set to `selectedEntity`'s own id the one
    // frame the Inspector's "Delete Object" button is clicked (see
    // editor_ui.cpp's own renderInspectorPanel() comment). EditorUI no
    // longer calls destroyEntityOrphaningChildren() itself here -- deleting
    // an entity now has to run through Application::deleteEntity()
    // (application.cpp) so a SceneEntityRecord can be captured and pushed
    // onto the undo stack BEFORE the entity is actually destroyed, which
    // only Application (registry_'s owner, with access to
    // captureEntityRecord()/undoStack_) can do.
    //
    // `transformEditCommitted`: unconditionally reset to std::nullopt at
    // the top of every call, then set to a real kTransformEdit Command the
    // one frame a COMPLETED transform edit is detected -- either an
    // Inspector Transform DragFloat3/DragFloat field's own
    // IsItemDeactivatedAfterEdit() (renderInspectorPanel()), or a gizmo
    // drag's own mouse-release transition (updateGizmo(), private, below).
    // Both report through this SAME out-parameter (only one can plausibly
    // fire in a single ImGui frame -- a user cannot simultaneously drag the
    // gizmo and type into an Inspector field) rather than two separate
    // parameters. Application::render() pushes this straight onto
    // undoStack_ when present -- EditorUI itself never touches an
    // UndoStack, matching its own "just a Dear ImGui wrapper over data
    // Application owns" role.
    //
    // `canUndo`/`canRedo`: read-only snapshots of Application's own
    // undoStack_.canUndo()/canRedo() this frame (the identical by-value
    // shape `cameraCaptured`/`activeDirectionalLight` above already use for
    // state EditorUI only ever displays) -- gate the toolbar's own
    // undo/redo buttons' enabled state, so they read exactly like a real
    // editor's (grayed out at either end of the history) rather than the
    // permanently-disabled stubs they were through Phase 18g.
    // `undoRequested`/`redoRequested`: unconditionally reset to false at
    // the top of every call, set true the one frame the toolbar's own
    // undo/redo button is clicked -- Application::render() calls
    // undo()/redo() when either comes back true, immediately after this
    // call returns, the same "report intent, act right after" shape every
    // other out-parameter here already follows.
    CreateEntityKind renderDockspaceShell(unsigned int viewportColorTexture, EntityRegistry& registry,
                                           std::optional<EntityId>& selectedEntity,
                                           std::optional<EntityId> activeDirectionalLight, bool hasActiveCamera,
                                           bool& saveSceneRequested,
                                           std::optional<std::string>& textureAssignRequested,
                                           std::optional<std::string>& assetDropRequested, bool cameraCaptured,
                                           bool& cameraCaptureRequested, ShadingMode& editShadingMode,
                                           bool& physicsRunning, bool showCustomTitleBar, bool windowMaximized,
                                           std::pair<int, int> windowPos, TitleBarAction& titleBarAction,
                                           const glm::vec3& cameraPosition, const glm::mat4& cameraView,
                                           const glm::mat4& cameraProjection,
                                           std::optional<EntityId>& deleteEntityRequested,
                                           std::optional<Command>& transformEditCommitted, bool canUndo, bool canRedo,
                                           bool& undoRequested, bool& redoRequested);

    // Phase 18e: the headless verification hook behind ENGINE_DEBUG_GIZMO_DRAG
    // (see that env var's own application.cpp comment for the full design).
    // When `screenPos` is non-nullopt, updateGizmo() (private, below) uses
    // `screenPos`/`mouseDown`/`mousePressedThisFrame` as this frame's mouse
    // state INSTEAD OF real Dear ImGui queries (GetIO().MousePos,
    // IsMouseDown(), a locally-computed press edge) -- everything
    // DOWNSTREAM of that one substitution (screenPointToWorldRay(),
    // hitTestGizmoAxes(), updateGizmoDrag(), and the resulting Transform
    // mutation) is the exact same production code a real mouse-driven drag
    // runs through, never a separate bypass path. `screenPos` is in the same
    // "relative to the Viewport panel's own top-left corner" space the real
    // path already computes (`io.MousePos - panelScreenPos`) -- Application
    // computes this directly via gizmo.hpp's own worldPointToScreenPoint()
    // against its own viewportWidth_/viewportHeight_, so it never needs to
    // know this panel's absolute on-screen position at all. Passing
    // std::nullopt (this class's own default state, never set otherwise)
    // restores the real ImGui-driven path -- an ordinary interactive run
    // never calls this at all.
    void setDebugMouseOverride(std::optional<glm::vec2> screenPos, bool mouseDown, bool mousePressedThisFrame);

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

    // Phase 17d: the custom title bar row -- see this file's own Phase 17d
    // header comment and TitleBarAction's own comment for the full design.
    // A private member function (like buildInitialLayout() above), not a
    // free function in editor_ui.cpp's own anonymous namespace the way
    // renderViewportToolbar()/toolbarIconButton() are: unlike that toolbar
    // row, this one needs access to this class's OWN persistent
    // frame-to-frame state (titleBarDragging_ below) to know whether a drag
    // is still in progress, the same reason buildInitialLayout() itself is a
    // member function rather than a free one (it needs layoutBuilt_).
    //
    // `showCustomTitleBar` (Window::isDecorated() negated, read by
    // Application::render() -- see that accessor's own window.hpp comment):
    // when false (the OS's native decorations are ON, the
    // ENGINE_WINDOW_DECORATED=1 escape hatch, main.cpp), this method reserves
    // NO screen space and draws nothing at all -- the OS already has its own
    // real title bar/min/max/close in that case, so drawing this project's
    // custom one on top would be a redundant, broken-looking second one, not
    // a graceful fallback. The File menu bar becomes the very top row again
    // in that case, exactly this project's pre-Phase-17d layout.
    void renderTitleBar(bool showCustomTitleBar, bool windowMaximized, std::pair<int, int> windowPos,
                         TitleBarAction& action);

    // Phase 18e: the translate gizmo's own mouse-interaction handling --
    // hit-testing which axis (if any) the mouse is over, running
    // gizmo.hpp's updateGizmoDrag() state machine, and, while dragging,
    // writing the resulting position straight into `registry`'s Transform
    // for `selectedEntity`. A private member function (like renderTitleBar()
    // above), not a free function, for the identical reason: it needs this
    // class's OWN persistent cross-frame state (gizmoDragState_ below) to
    // know whether a drag is still in progress.
    //
    // Must be called from INSIDE the "Viewport" panel's own Begin()/End()
    // block, AFTER the toolbar overlay has been submitted (so
    // `toolbarBgMin`/`toolbarBgMax` are this frame's real values) -- the
    // same placement/ordering constraint renderDockspaceShell()'s own Phase
    // 16 double-click-to-capture check already documents, and for the
    // identical reason (Dear ImGui's hover/mouse queries only mean "over
    // THIS one docked panel" when called from inside it).
    //
    // Returns true on any frame the mouse is currently interacting with a
    // gizmo axis at all -- hovering one (even without a button down) OR
    // actively dragging one -- so renderDockspaceShell()'s own camera-
    // capture double-click check can suppress itself that same frame: a
    // single click-drag on a gizmo arm must never be misread as the start
    // of a double-click camera capture, and camera capture must not be
    // enterable while a gizmo drag is in progress (this phase's own
    // documented precedence rule). `registry`/`selectedEntity` mirror
    // renderDockspaceShell()'s own same-named parameters exactly;
    // `cameraPosition`/`cameraView`/`cameraProjection` are that method's own
    // new Phase 18e parameters, threaded straight through; `panelScreenPos`/
    // `contentRegion` are the Viewport panel's own on-screen origin/size,
    // already captured earlier in the SAME renderDockspaceShell() call (see
    // that method's own Phase 14d/14c comments); `toolbarBgMin`/
    // `toolbarBgMax` are renderViewportToolbarOverlay()'s own just-measured
    // background rect, the identical rect the camera-capture guard already
    // excludes mouse interaction over.
    // Phase 18h: `transformEditCommitted` -- set to a real kTransformEdit
    // Command the one frame this call detects a drag's mouse-release
    // transition (gizmoDragState_ going from a real axis back to kNone),
    // using gizmoDragStartLocalPosition_ as `before` and the entity's own
    // live Transform (already updated by this SAME frame's drag logic, or
    // left exactly where the previous frame's last update step put it) as
    // `after` -- see renderDockspaceShell()'s own updated header comment
    // for what Application does with this. Left untouched (not reset to
    // std::nullopt) on every OTHER frame -- the caller (renderDockspaceShell())
    // is what resets it once, before either this function or
    // renderInspectorPanel() runs, since either one (never both in the same
    // frame) may be the one to set it.
    bool updateGizmo(EntityRegistry& registry, std::optional<EntityId> selectedEntity, const glm::vec3& cameraPosition,
                      const glm::mat4& cameraView, const glm::mat4& cameraProjection, const ImVec2& panelScreenPos,
                      const ImVec2& contentRegion, const ImVec2& toolbarBgMin, const ImVec2& toolbarBgMax,
                      std::optional<Command>& transformEditCommitted);

    bool layoutBuilt_ = false;
    // Phase 17d: true from the frame a title-bar drag starts (a left-click on
    // the title bar's own empty, non-button area -- see renderTitleBar()'s
    // own .cpp comment) until the frame the mouse button is released. Has to
    // be persistent, cross-frame state (not re-derived fresh every call the
    // way `overEmptyArea`/`clicked` are) because a real drag routinely
    // continues for many frames after its own starting one, during which the
    // cursor is not guaranteed to stay hovering the title bar's own
    // Begin()/End() window rect at all (a fast drag can easily out-run the
    // window being repositioned under it for a frame or two) -- so "is a drag
    // currently active" cannot be re-derived from this frame's own hover
    // state alone the way toolbarIconButton()'s click detection can.
    bool titleBarDragging_ = false;
    // Phase 14c: see viewportWidth()/viewportHeight() above. 0 until
    // renderDockspaceShell() has run at least once -- Application's own
    // constructor seeds its mirror of these (viewportWidth_/viewportHeight_,
    // application.hpp) from the window's initial size instead, so a 0 read
    // here never reaches an actual framebuffer construction/resize call.
    int viewportWidth_ = 0;
    int viewportHeight_ = 0;

    // Post-18a fix (Phase 18b): the floating toolbar's own rendered
    // button-group width, in pixels, as measured AFTER the group was last
    // actually submitted -- used at the START of the NEXT
    // renderViewportToolbarOverlay() call to compute where the group should
    // start so its center lands on the Viewport panel's current horizontal
    // center (see that function's own editor_ui.cpp comment for the
    // centering math). The group's true width isn't knowable until AFTER
    // Dear ImGui has actually laid out its six buttons -- exactly the same
    // "can't know the bounding box until after submitting it" problem
    // renderViewportToolbarOverlay()'s own background rect already solves
    // for ITS size via ImDrawListSplitter (Phase 18a's own comment) -- but
    // splitting channels only fixes ordering (paint the background behind
    // buttons already submitted this same frame), not POSITION (the buttons'
    // own start X has to be chosen before they're submitted at all). Using
    // last frame's real measurement as this frame's prediction is the
    // identical one-frame-lag, self-correcting shape viewportWidth_/
    // viewportHeight() above already establish and document (Phase 14c) for
    // the same underlying reason -- a redock/resize (or a Play/Pause click
    // flipping which glyph is active, which doesn't change width, but a
    // future toolbar change might) is reflected correctly within one frame,
    // not perfectly on the very first one. Starts at 0.0f (frame 1 centers
    // around a 0-wide group, i.e. renders left-of-center by half the real
    // group's width; self-corrects to exactly centered from frame 2
    // onward) -- the same harmless, documented frame-0 imperfection
    // viewportWidth_/viewportHeight_ already accept.
    float toolbarGroupWidthLastFrame_ = 0.0f;

    // Phase 18e: the translate gizmo's own persistent cross-frame drag state
    // -- gizmo.hpp's own GizmoDragState, the identical "not dragging /
    // dragging-axis-X/Y/Z" state machine described there, threaded back in
    // as `current` on every updateGizmo() call and overwritten with
    // whatever it returns. Has to persist across frames for the same reason
    // titleBarDragging_ above does: a real drag routinely continues for many
    // frames after its own starting one.
    GizmoDragState gizmoDragState_;
    // The selected entity's own LOCAL Transform::position() as of the frame
    // gizmoDragState_ transitioned from kNone to a real axis -- gizmo.hpp
    // itself knows nothing about Transform/local-vs-world space (see its own
    // header comment on why); THIS is where that translation actually
    // happens: updateGizmo() applies each frame's WORLD-space delta
    // (gizmoDragState_'s own anchor minus this frame's fresh drag-state
    // result) onto this cached LOCAL starting position, not onto the
    // entity's own (already-moving) live Transform value, for the same
    // "anchor captured once at grab time, never re-read mid-drag" reason
    // GizmoDragState::startEntityPosition itself is captured once.
    // Meaningless (left at its default) whenever gizmoDragState_.axis ==
    // GizmoAxis::kNone.
    glm::vec3 gizmoDragStartLocalPosition_{0.0f};
    // Phase 18e: ENGINE_DEBUG_GIZMO_DRAG's own headless-verification
    // override state -- see setDebugMouseOverride()'s own comment above.
    // std::nullopt (the default, and the state every ordinary interactive
    // run stays in forever) means updateGizmo() uses real Dear ImGui mouse
    // queries; a real value here is FROM Application's own update(), never
    // written from inside this class itself.
    std::optional<glm::vec2> debugMouseScreenPosOverride_;
    bool debugMouseDownOverride_ = false;
    bool debugMousePressedOverride_ = false;

    // Phase 15d: built exactly once, in the constructor -- see this class's
    // own header comment above and asset_browser.hpp's own "Caching"
    // comment for why a one-time build (not buildSceneTree()'s own
    // every-frame rebuild) is the correct match for a filesystem tree that
    // cannot change at runtime today.
    std::vector<AssetTreeNode> assetTree_;
    // The currently-highlighted Assets-panel row, identified by its own
    // AssetTreeNode::relativePath (e.g. "textures/skybox/left.png") -- a
    // plain string key rather than an EntityId-shaped identifier, since an
    // asset-tree row isn't an ECS entity at all. Purely local, cosmetic
    // "which row is selected" state -- see this class's own header comment
    // above for why this lives here instead of being threaded through
    // renderDockspaceShell() the way selectedEntity is.
    std::optional<std::string> selectedAssetPath_;

    // Phase 18h: the Inspector's own Transform DragFloat3/DragFloat fields'
    // (Position/Rot Y/Scale, renderInspectorPanel()) cross-frame "an edit
    // session is in progress" state -- the Transform snapshot captured the
    // instant ImGui::IsItemActivated() first fires for whichever field the
    // user just started interacting with, held here until that SAME field's
    // own ImGui::IsItemDeactivatedAfterEdit() fires (possibly many frames
    // later, for a real click-drag), at which point renderInspectorPanel()
    // builds a Command from this cached `before` plus the entity's own
    // then-current `after` and clears this back to std::nullopt. Has to be
    // persistent, cross-frame state for the identical reason
    // gizmoDragState_/titleBarDragging_ above are: a real drag interaction
    // routinely spans many frames between its own start and end. Only one
    // Inspector field can be under active ImGui interaction at a time (Dear
    // ImGui's own single-active-item model), so one shared member -- not one
    // per field -- is enough to cover all three.
    std::optional<TransformSnapshot> pendingInspectorTransformEditBefore_;
};

}  // namespace engine

#endif  // ENGINE_EDITOR_UI_HPP
