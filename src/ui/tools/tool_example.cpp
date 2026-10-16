/*
 * tool_example.cpp - Example Tool
 *
 * ---------------------------------------------------------------------------
 * REFERENCE TEMPLATE: copy this file to start your own tool.
 * ---------------------------------------------------------------------------
 *
 * This is a minimal, working example. It is shipped disabled by default (see
 * its row in tools_registry.cpp: default_enabled = false), so it is hidden
 * from the sidebar on first run. Enable it from the Tools modal to try it.
 *
 * Integration is 3 small steps (full guide in src/ui/tools/README.md):
 *   1. Write `void DrawPanelMyTool(UIContext *ctx, AppConfig *cfg)` in a new
 *      tool_my_tool.cpp - the body is plain ImGui.
 *   2. Declare it in src/ui/tools/tools.h.
 *   3. Add a PanelId to src/core/types.h and one row to g_panel_defs in
 *      src/ui/tools/tools_registry.cpp.
 *
 * Tool settings persist through the generic store in tools_settings.h (keys
 * are namespaced, e.g. "example.show_greeting"), so no AppConfig field or
 * config.cpp edit is needed. The pattern below - read once, write back only
 * when the widget changes - is all there is to it.
 */

#include "tools.h"
#include "tools_settings.h"

#include "imgui.h"

/* Namespaced persistence key: "<toolname>.<setting>". Keep the prefix unique. */
#define EXAMPLE_KEY_SHOW_GREETING "example.show_greeting"

void DrawPanelExample(UIContext *ctx, AppConfig *cfg)
{
    /* This example draws no scene overlay, so the shared context is unused. */
    (void)ctx;

    ImGui::TextWrapped("Example Tool - a minimal reference for new tools. "
                       "See src/ui/tools/README.md.");

    ImGui::Separator();

    // One persisted bool. A checkbox. Read it (default true), then write it back only when the user toggles it.
    bool show_greeting = ToolSettingGetBool(cfg, EXAMPLE_KEY_SHOW_GREETING, true);
    if (ImGui::Checkbox("Show greeting", &show_greeting))
        ToolSettingSetBool(cfg, EXAMPLE_KEY_SHOW_GREETING, show_greeting);

    /* Render something obvious that reflects the setting above. */
    if (show_greeting)
        ImGui::Text("Hello from the Example Tool!");
    else
        ImGui::TextDisabled("(greeting hidden)");
}
