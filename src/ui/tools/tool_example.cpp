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
