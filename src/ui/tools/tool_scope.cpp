/*
 * tool_scope.cpp - Scope panel
 */

#include "tools.h"
#include "tools_settings.h"
#include "ui/ui.h"
#include "core/config.h"

#include "imgui.h"

void DrawPanelScope(UIContext *ctx, AppConfig *cfg)
{
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(0.0f);

    /* scope defaults (ROADMAP 12.3) - persisted via tool_settings */
    static bool s_loaded = false;
    if (!s_loaded)
    {
        *ctx->scope_beam = ToolSettingGetFloat(cfg, "scope.beam_width", *ctx->scope_beam);
        g_ui.scope_show_leo    = ToolSettingGetBool(cfg, "scope.show_leo", g_ui.scope_show_leo);
        g_ui.scope_show_heo    = ToolSettingGetBool(cfg, "scope.show_heo", g_ui.scope_show_heo);
        g_ui.scope_show_geo    = ToolSettingGetBool(cfg, "scope.show_geo", g_ui.scope_show_geo);
        g_ui.scope_show_trails = ToolSettingGetBool(cfg, "scope.show_trails", g_ui.scope_show_trails);
        s_loaded = true;
    }

    ImGui::SetNextItemWidth(avail_w);
    ImGui::SliderFloat("Azimuth", ctx->scope_az, 0.0f, 360.0f, "%.1f");
    ImGui::SetNextItemWidth(avail_w);
    ImGui::SliderFloat("Elevation", ctx->scope_el, -90.0f, 90.0f, "%.1f");
    ImGui::SetNextItemWidth(avail_w);
    if (ImGui::SliderFloat("Beam Width", ctx->scope_beam, 1.0f, 120.0f, "%.1f"))
        ToolSettingSetFloat(cfg, "scope.beam_width", *ctx->scope_beam);

    ImGui::Separator();
    /* Use a wrapping layout for checkboxes so they adapt to sidebar width */
    float cb_w = ImGui::CalcTextSize("Show HEO/MEO").x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::GetFrameHeight();
    float avail_for_cb = avail_w;
    int items_per_row = (int)(avail_for_cb / (cb_w + ImGui::GetStyle().ItemSpacing.x));
    if (items_per_row < 1) items_per_row = 1;

    if (ImGui::Checkbox("Show LEO", &g_ui.scope_show_leo))
        ToolSettingSetBool(cfg, "scope.show_leo", g_ui.scope_show_leo);
    if (items_per_row >= 2) { ImGui::SameLine(); }
    if (ImGui::Checkbox("Show HEO/MEO", &g_ui.scope_show_heo))
        ToolSettingSetBool(cfg, "scope.show_heo", g_ui.scope_show_heo);
    if (items_per_row >= 3) { ImGui::SameLine(); }
    if (ImGui::Checkbox("Show GEO", &g_ui.scope_show_geo))
        ToolSettingSetBool(cfg, "scope.show_geo", g_ui.scope_show_geo);
    if (items_per_row >= 4) { ImGui::SameLine(); }
    if (ImGui::Checkbox("Show Trails", &g_ui.scope_show_trails))
        ToolSettingSetBool(cfg, "scope.show_trails", g_ui.scope_show_trails);

    ImGui::PopTextWrapPos();
}
