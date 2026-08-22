#include "engine/debug_ui.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "engine/log.hpp"

namespace engine {

namespace {

// The GLSL version string ImGui_ImplOpenGL3_Init() embeds at the top of its
// own internal shaders -- must not exceed the GL context's own core-profile
// ceiling (see window.hpp's own #ifdef __APPLE__ note on why macOS requests
// 4.1 instead of 4.3), or shader compilation inside ImGui_ImplOpenGL3_Init()
// would fail on that platform the same way requesting a >4.1 context itself
// would.
const char* glslVersionString() {
#ifdef __APPLE__
    return "#version 410 core";
#else
    return "#version 430 core";
#endif
}

}  // namespace

DebugUI::DebugUI(GLFWwindow* window, bool enabled) : enabled_(enabled) {
    if (!enabled_) {
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    // No imgui.ini persisted to disk -- this is a debug overlay for a small
    // fixed scene, not a tool whose window layout needs to survive between
    // runs, and writing a stray file next to wherever engine_app happens to
    // be launched from (including the headless verification harness's own
    // build/ directory) is a side effect this phase has no reason to add.
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    // install_callbacks = true: lets ImGui's GLFW backend register its own
    // key/mouse/cursor/scroll callbacks -- safe here specifically because
    // this engine has never registered any of its own (see this class's own
    // header comment on why InputState's poll-based reads are unaffected).
    ImGui_ImplGlfw_InitForOpenGL(window, /*install_callbacks=*/true);
    ImGui_ImplOpenGL3_Init(glslVersionString());

    LOG_INFO("Debug UI enabled (ENGINE_SHOW_DEBUG_UI)");
}

DebugUI::~DebugUI() {
    if (!enabled_) {
        return;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void DebugUI::newFrame() {
    if (!enabled_) {
        return;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void DebugUI::render() {
    if (!enabled_) {
        return;
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

}  // namespace engine
