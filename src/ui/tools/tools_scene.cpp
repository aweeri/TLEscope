#include "tools_scene.h"
#include "tools_registry.h"

#include "imgui.h"

void DrawSceneHooks(SceneContext *sctx, AppConfig *cfg)
{
    if (!ImGui::GetCurrentContext())
        return;

    for (int i = 0; i < PANEL_COUNT; i++)
    {
        if (g_panel_defs[i].draw_scene)
            g_panel_defs[i].draw_scene(sctx, cfg);
    }
}
