/*
 * tools_scene.cpp - Scene render-hook dispatcher.
 *
 * Iterates the g_panel_defs registry and calls every registered draw_scene
 * callback. Called by main.cpp each frame inside BeginMode3D / BeginMode2D so
 * tool overlays draw on top of the scene.
 */

#include "tools_scene.h"
#include "tools_registry.h"

void DrawSceneHooks(SceneContext *sctx, AppConfig *cfg)
{
    for (int i = 0; i < g_panel_count; i++)
    {
        if (g_panel_defs[i].draw_scene)
            g_panel_defs[i].draw_scene(sctx, cfg);
    }
}