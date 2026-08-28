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

#include <optional>
#include <string>
#include <vector>

#include "engine/asset_browser.hpp"
#include "engine/ecs.hpp"

struct GLFWwindow;
typedef unsigned int ImGuiID;

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
    CreateEntityKind renderDockspaceShell(unsigned int viewportColorTexture, EntityRegistry& registry,
                                           std::optional<EntityId>& selectedEntity, const SelectionOutline* outline,
                                           std::optional<EntityId> activeDirectionalLight, bool& saveSceneRequested,
                                           std::optional<std::string>& textureAssignRequested,
                                           std::optional<std::string>& assetDropRequested, bool cameraCaptured,
                                           bool& cameraCaptureRequested);

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
};

}  // namespace engine

#endif  // ENGINE_EDITOR_UI_HPP
