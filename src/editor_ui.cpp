#include "engine/editor_ui.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
// Phase 14a: imgui_internal.h is needed for the DockBuilder* functions
// (DockBuilderAddNode/SplitNode/DockWindow/Finish) and
// ImGuiDockNodeFlags_DockSpace -- these are explicitly still "not yet a
// stable public API" per imgui_internal.h's own comment on them, but
// programmatically setting up an initial dock layout (rather than requiring
// a user to manually drag every panel into place on first launch) has no
// public-API equivalent; this is the documented, expected way every
// Dear ImGui docking application does this (see imgui_demo.cpp's own
// ShowExampleAppDockSpace()/DockBuilder usage, which this class's layout
// setup below mirrors).
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

#include "engine/log.hpp"
#include "engine/scene_hierarchy.hpp"

namespace engine {

namespace {

// Same GLSL-version-string reasoning as DebugUI's own (removed, Phase 14a --
// see debug_ui.hpp) copy of this helper: must not exceed the GL context's
// own core-profile ceiling (window.hpp's __APPLE__ note).
const char* glslVersionString() {
#ifdef __APPLE__
    return "#version 410 core";
#else
    return "#version 430 core";
#endif
}

// Phase 14d: recursively renders one Scene-Hierarchy row (and, if expanded,
// its own children) as an ImGui::TreeNodeEx() -- real parent/child nesting
// via ImGui's own tree indentation, matching this engine's own choice of
// real Parent-component grouping over a flat "folder" label (see
// scene_hierarchy.hpp's own header comment). `##<index>` is folded into the
// node's own ImGui id via PushID(entity index) rather than appended to the
// visible label text, so two differently-parented entities that happen to
// share a NameComponent string can't collide as far as ImGui's own
// id-stack-based widget identity is concerned, without the visible label
// itself growing a stray "##123" suffix.
//
// No icon glyphs (folder/mesh/light/camera, per the approved mockup): Dear
// ImGui's default font (no custom font atlas is built anywhere in this
// engine) only carries the ASCII/Latin-1 glyph range, nowhere near the
// Unicode private-use/symbol code points an icon font would need -- adding
// one is real, separate scope (a new vendored font asset + atlas
// configuration) this phase's brief doesn't ask for. Indentation, the
// tree-node's own expand/collapse arrow, and ImGuiTreeNodeFlags_Selected's
// highlight are what carry "this is a group vs. a leaf" and "this row is
// selected" instead -- functionally equivalent to the mockup's own icons/
// highlight for this phase's purpose (real tree + click-to-select), just
// without the pixel-identical iconography.
void renderSceneTreeNode(const SceneTreeNode& node, std::optional<EntityId>& selectedEntity) {
    ImGui::PushID(static_cast<int>(node.id.index()));

    const bool isSelected = selectedEntity.has_value() && *selectedEntity == node.id;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                                ImGuiTreeNodeFlags_DefaultOpen;
    if (node.children.empty()) {
        // ImGuiTreeNodeFlags_Leaf: no expand arrow drawn for a childless
        // entity -- NoTreePushOnOpen means TreeNodeEx() doesn't push an
        // indentation level for it either, so this row doesn't need a
        // matching TreePop() below.
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (isSelected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool opened = ImGui::TreeNodeEx(node.name.c_str(), flags);
    // IsItemClicked() covers a click anywhere on this row's own label/
    // background (not the expand arrow specifically, which TreeNodeEx()
    // already handles internally for open/close) -- exactly "click this row
    // to select it", independent of whether the click also happened to
    // toggle this node open/closed.
    if (ImGui::IsItemClicked()) {
        selectedEntity = node.id;
    }
    if (opened && !node.children.empty()) {
        for (const SceneTreeNode& child : node.children) {
            renderSceneTreeNode(child, selectedEntity);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

// Phase 14d: the approved mockup's dashed-rectangle-plus-corner-brackets
// selection look (a simple 2D screen-space gizmo, deliberately NOT a fancy
// inverted-hull silhouette shader -- see this phase's own brief). Both
// helpers draw directly into `drawList` in already-resolved screen-pixel
// coordinates (topLeft/bottomRight), leaving all NDC-to-panel-pixel mapping
// to their one call site below.
void addDashedRect(ImDrawList* drawList, ImVec2 topLeft, ImVec2 bottomRight, ImU32 color) {
    constexpr float kDashLength = 6.0f;
    constexpr float kGapLength = 4.0f;
    constexpr float kThickness = 1.5f;

    auto dashedLine = [&](ImVec2 from, ImVec2 to) {
        const ImVec2 delta(to.x - from.x, to.y - from.y);
        const float length = std::sqrt((delta.x * delta.x) + (delta.y * delta.y));
        if (length < 1.0f) {
            return;
        }
        const ImVec2 direction(delta.x / length, delta.y / length);
        float traveled = 0.0f;
        bool drawing = true;
        while (traveled < length) {
            const float segment = std::min(drawing ? kDashLength : kGapLength, length - traveled);
            if (drawing) {
                const ImVec2 segmentStart(from.x + (direction.x * traveled), from.y + (direction.y * traveled));
                const ImVec2 segmentEnd(from.x + (direction.x * (traveled + segment)),
                                         from.y + (direction.y * (traveled + segment)));
                drawList->AddLine(segmentStart, segmentEnd, color, kThickness);
            }
            traveled += segment;
            drawing = !drawing;
        }
    };

    dashedLine(topLeft, ImVec2(bottomRight.x, topLeft.y));
    dashedLine(ImVec2(bottomRight.x, topLeft.y), bottomRight);
    dashedLine(bottomRight, ImVec2(topLeft.x, bottomRight.y));
    dashedLine(ImVec2(topLeft.x, bottomRight.y), topLeft);
}

void addCornerBrackets(ImDrawList* drawList, ImVec2 topLeft, ImVec2 bottomRight, ImU32 color) {
    // Each bracket's own two short, solid arms -- not dashed, so they read as
    // a distinct "handle" accent against the dashed outline itself, matching
    // the approved mockup's own corner-bracket look.
    constexpr float kArmLength = 10.0f;
    constexpr float kThickness = 2.0f;

    const ImVec2 corners[4] = {
        topLeft,
        ImVec2(bottomRight.x, topLeft.y),
        bottomRight,
        ImVec2(topLeft.x, bottomRight.y),
    };
    // Sign of each arm's own direction along x/y, pointing INWARD from that
    // corner (e.g. the top-left corner's arms extend right and down) so the
    // brackets sit just inside the dashed rectangle rather than outside it.
    const float armX[4] = {1.0f, -1.0f, -1.0f, 1.0f};
    const float armY[4] = {1.0f, 1.0f, -1.0f, -1.0f};

    for (int i = 0; i < 4; ++i) {
        const ImVec2& corner = corners[i];
        drawList->AddLine(corner, ImVec2(corner.x + (armX[i] * kArmLength), corner.y), color, kThickness);
        drawList->AddLine(corner, ImVec2(corner.x, corner.y + (armY[i] * kArmLength)), color, kThickness);
    }
}

}  // namespace

EditorUI::EditorUI(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    // No imgui.ini persisted to disk -- same reasoning as DebugUI's own
    // constructor comment (Phase 8c): this phase's dockspace layout is
    // rebuilt programmatically every run anyway (see buildInitialLayout()
    // below), so there is nothing useful an on-disk layout would preserve
    // yet, and it avoids a stray file appearing next to wherever engine_app
    // happens to be launched from (including this project's own headless
    // verification harness's build/ directory). A later Phase 14 sub-phase
    // that actually wants cross-run layout persistence (once panels hold
    // real, resizable content worth remembering the arrangement of) can
    // revisit this.
    io.IniFilename = nullptr;

    // Phase 14a: this is the one flag this whole phase exists to flip on --
    // see CMakeLists.txt's own Phase 14a comment for why the vendored ImGui
    // tag had to change (v1.92.9b -> v1.92.9b-docking) for this flag/the
    // DockBuilder* API/DockSpaceOverViewport() to even exist.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    // install_callbacks = true: see this class's own header comment on why
    // there can only be one ImGui context's worth of GLFW callbacks active
    // for this window, and why this -- not DebugUI -- is the one that
    // installs them. Safe for the same underlying reason DebugUI's own
    // Phase 8c comment already established: this engine has never
    // registered any GLFW callbacks of its own (input.hpp's InputState
    // polls Window::isKeyPressed()/getCursorPos() every frame instead), so
    // there is nothing pre-existing for ImGui's callbacks to clobber.
    ImGui_ImplGlfw_InitForOpenGL(window, /*install_callbacks=*/true);
    ImGui_ImplOpenGL3_Init(glslVersionString());

    LOG_INFO("Editor UI initialized (always-on dockspace shell, Phase 14a)");
}

EditorUI::~EditorUI() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void EditorUI::newFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void EditorUI::buildInitialLayout(ImGuiID dockspaceId) {
    // Standard DockBuilder recipe (mirrors imgui_demo.cpp's own
    // ShowExampleAppDockSpace()): tear down and recreate the node
    // DockSpaceOverViewport() just created for `dockspaceId` so it can be
    // split into the approved-mockup layout below, rather than left as one
    // single undivided node the four Begin() calls in
    // renderDockspaceShell() would otherwise all pile into as overlapping
    // tabs.
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

    // Left column (~22% width): split off first, then split again
    // vertically into Scene (upper) / Assets (lower). `centerId` keeps
    // being reassigned to "whatever's left after the most recent split" --
    // DockBuilderSplitNode's own contract (see imgui_internal.h).
    ImGuiID centerId = dockspaceId;
    ImGuiID leftId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, 0.22f, nullptr, &centerId);
    // Right column (~28% of what remains after the left split): Inspector.
    ImGuiID rightId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, 0.28f, nullptr, &centerId);
    // Left column split again, top/bottom: Scene above Assets.
    ImGuiID leftBottomId = ImGui::DockBuilderSplitNode(leftId, ImGuiDir_Down, 0.45f, nullptr, &leftId);

    ImGui::DockBuilderDockWindow("Scene", leftId);
    ImGui::DockBuilderDockWindow("Assets", leftBottomId);
    ImGui::DockBuilderDockWindow("Inspector", rightId);
    // Whatever's left in the center (the majority of the window) is the
    // Viewport -- see this class's own header comment on why it's placeholder
    // text only in this phase, not a render-to-texture 3D view yet.
    ImGui::DockBuilderDockWindow("Viewport", centerId);

    ImGui::DockBuilderFinish(dockspaceId);
}

void EditorUI::renderDockspaceShell(unsigned int viewportColorTexture, EntityRegistry& registry,
                                     std::optional<EntityId>& selectedEntity, const SelectionOutline* outline) {
    // DockSpaceOverViewport() is the built-in "just cover the whole main
    // viewport" helper (creates its own invisible host window internally) --
    // simpler than manually building a host window + ImGui::DockSpace()
    // call, and sufficient for this phase's single-viewport, single-monitor
    // scope. ImGuiDockNodeFlags_PassthruCentralNode lets the still-directly-
    // rendered 3D scene (see this class's own header comment) show through
    // any part of the dockspace nothing is currently docked over, matching
    // this phase's documented "Viewport panel floats over/alongside the
    // existing 3D render, not yet a texture of it" intermediate state.
    const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

    // Built exactly once per process run (io.IniFilename == nullptr above
    // means there's no on-disk layout to restore instead) -- guarded by
    // layoutBuilt_, not re-run every frame, so a user's own later drag-to-
    // resize/rearrange of these four panels isn't stomped on the next
    // frame.
    if (!layoutBuilt_) {
        buildInitialLayout(dockspaceId);
        layoutBuilt_ = true;
    }

    ImGui::Begin("Scene");
    {
        // Phase 14d: rebuilt fresh every frame -- this engine's own scene has
        // a handful of entities (three today, see assets/scenes/default.json),
        // so there is no reason to cache/diff buildSceneTree()'s own small
        // allocation against a previous frame's tree the way a much larger
        // scene's editor might need to. Real Parent-component nesting (not a
        // flat "folder" label) -- see scene_hierarchy.hpp's own header
        // comment for why, and renderSceneTreeNode() above for how each
        // root/child row is actually drawn/selected.
        const std::vector<SceneTreeNode> tree = buildSceneTree(registry);
        for (const SceneTreeNode& root : tree) {
            renderSceneTreeNode(root, selectedEntity);
        }
    }
    ImGui::End();

    ImGui::Begin("Assets");
    ImGui::TextWrapped("Asset Browser -- coming in a later Phase 14 sub-phase.");
    ImGui::End();

    ImGui::Begin("Viewport");
    {
        // Phase 14c: the panel's own available content-region size -- both
        // what ImGui::Image() below is sized to fill and what
        // viewportWidth()/viewportHeight() report for Application to read
        // next frame (see this class's own header comment on why next
        // frame, not this one). Recorded every call, even when the image
        // below is skipped.
        const ImVec2 contentRegion = ImGui::GetContentRegionAvail();
        viewportWidth_ = static_cast<int>(contentRegion.x);
        viewportHeight_ = static_cast<int>(contentRegion.y);
        // Phase 14d: the Viewport panel's own on-screen top-left corner, in
        // absolute screen pixels -- captured here, BEFORE ImGui::Image()
        // below advances the cursor down by the image's own height, since
        // that's the point at which ImGui::GetCursorScreenPos() reports this
        // panel's content-region origin rather than somewhere past it. This
        // (plus contentRegion above) is the panel's whole on-screen
        // rectangle -- the outline projection below needs both, since the
        // Viewport panel does NOT fill the whole window (Scene/Assets/
        // Inspector occupy the rest, see this class's own Phase 14a layout).
        const ImVec2 panelScreenPos = ImGui::GetCursorScreenPos();

        if (viewportColorTexture != 0 && contentRegion.x > 0.0f && contentRegion.y > 0.0f) {
            // uv0=(0,1)/uv1=(1,0): flips vertically. OpenGL's texture
            // coordinate origin is the bottom-left texel, matching how
            // every one of this engine's own fullscreen shader passes
            // already samples a Framebuffer's color texture (e.g.
            // postprocess.frag) -- that convention reproduces the rendered
            // image right-side-up when drawn as a fullscreen NDC quad. Dear
            // ImGui's ImGui::Image(), however, treats uv (0,0) as the
            // image's top-left corner (the same convention its own font
            // atlas and every other UI texture use) -- so handing it this
            // texture with the default uv0=(0,0)/uv1=(1,1) would display
            // Application's 3D render upside down. Confirmed visually via
            // this phase's own headless screenshot, not just assumed --
            // see README.md's Phase 14c section.
            ImGui::Image(static_cast<ImTextureID>(viewportColorTexture), contentRegion, ImVec2(0.0f, 1.0f),
                         ImVec2(1.0f, 0.0f));
        }
        // else: nothing rendered into viewportColorTexture yet, or the
        // panel's content region is currently degenerate (0 in some
        // dimension -- the very first frame before ImGui's docking layout
        // has settled, or a user dragging a divider all the way shut).
        // Leaving the panel body blank this frame is the documented,
        // simple choice here (see application.hpp's own Phase 14c comment
        // on the matching degenerate-size guard for Application's own
        // render targets) -- there is nothing meaningful to show yet
        // either way.

        // Phase 14d: the selection outline, drawn on top of the image above
        // via THIS SAME "Viewport" window's own draw list
        // (ImGui::GetWindowDrawList()) -- not the global foreground draw
        // list. Both compose on top of ImGui::Image() (a window's own draw
        // commands are submitted, and therefore rasterized, in the order
        // they're issued within that window, and the foreground draw list is
        // drawn on top of every window besides), but only the WINDOW draw
        // list is automatically clipped to this window's own visible
        // rectangle by Dear ImGui -- the foreground list is not clipped to
        // any one window at all, so a selection near the Viewport panel's own
        // edge could otherwise paint a stray fragment of dashed line over
        // whatever panel happens to be docked next to it. Confirmed by this
        // phase's own headless screenshot (see README.md's Phase 14d
        // section), not just assumed.
        if (outline != nullptr && contentRegion.x > 0.0f && contentRegion.y > 0.0f) {
            // NDC ([-1,1], +Y up) -> this panel's own screen pixels (+Y
            // down): the standard "u/v in [0,1], then scale by the panel's
            // own size and offset by its own screen-space origin" mapping --
            // note the Y flip (1.0f - v), same direction (though a distinct
            // reason) as ImGui::Image()'s own uv0/uv1 flip just above: NDC's
            // own +Y-up convention is the opposite of ImGui's own +Y-down
            // screen-pixel convention.
            const auto ndcToPanelScreen = [&](float ndcX, float ndcY) {
                const float u = (ndcX * 0.5f) + 0.5f;
                const float v = 1.0f - ((ndcY * 0.5f) + 0.5f);
                return ImVec2(panelScreenPos.x + (u * contentRegion.x), panelScreenPos.y + (v * contentRegion.y));
            };
            // outline->ndcMaxY is the NDC-space TOP edge (+Y up), which maps
            // to the smaller screen-Y (closer to the panel's own top) --
            // i.e. topLeft pairs ndcMinX with ndcMaxY, not ndcMinY.
            const ImVec2 topLeft = ndcToPanelScreen(outline->ndcMinX, outline->ndcMaxY);
            const ImVec2 bottomRight = ndcToPanelScreen(outline->ndcMaxX, outline->ndcMinY);

            // Teal accent, matching the approved mockup's own selection
            // color direction (a modern dark/teal-accented style) -- not a
            // pixel-perfect match to any one specific hex value (this
            // phase's brief explicitly doesn't require that), just a bright,
            // clearly-not-part-of-the-3D-scene color against this engine's
            // own rendered content.
            const ImU32 accentColor = IM_COL32(56, 217, 197, 255);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            addDashedRect(drawList, topLeft, bottomRight, accentColor);
            addCornerBrackets(drawList, topLeft, bottomRight, accentColor);
        }
    }
    ImGui::End();

    ImGui::Begin("Inspector");
    ImGui::TextWrapped("Inspector -- coming in Phase 14e.");
    ImGui::End();
}

void EditorUI::render() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

}  // namespace engine
