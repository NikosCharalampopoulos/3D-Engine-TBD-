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

#include "engine/log.hpp"

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

void EditorUI::renderDockspaceShell() {
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
    ImGui::TextWrapped("Scene Hierarchy -- coming in Phase 14d.");
    ImGui::End();

    ImGui::Begin("Assets");
    ImGui::TextWrapped("Asset Browser -- coming in a later Phase 14 sub-phase.");
    ImGui::End();

    ImGui::Begin("Viewport");
    ImGui::TextWrapped(
        "3D viewport (render-to-texture) -- coming in Phase 14c.\n\n"
        "For this phase, the actual 3D scene still renders directly to the "
        "window underneath/alongside this docked panel rather than into a "
        "texture sampled here -- see this class's own header comment and "
        "README.md's Phase 14a section.");
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
