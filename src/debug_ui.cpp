#include "engine/debug_ui.hpp"

#include "engine/log.hpp"

namespace engine {

DebugUI::DebugUI(bool enabled) : enabled_(enabled) {
    // Phase 14a: no ImGui/GL work here any more -- see this class's own
    // header comment. The log line is kept (moved from the old
    // initializeImGuiContext() path) so ENGINE_SHOW_DEBUG_UI's effect is
    // still visible in the log exactly as before when set at startup.
    if (enabled_) {
        LOG_INFO("Debug UI enabled (ENGINE_SHOW_DEBUG_UI)");
    }
}

void DebugUI::setEnabled(bool enabled) {
    if (enabled == enabled_) {
        return;
    }
    LOG_INFO(enabled ? "Debug UI shown (F1)" : "Debug UI hidden (F1)");
    enabled_ = enabled;
}

}  // namespace engine
