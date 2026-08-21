/*
 * tool_scope.cpp - Scope panel
 */

#include "tools.h"
#include "ui/ui.h"
#include "core/config.h"

#include "imgui.h"

void DrawPanelScope(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(0.0f);

    ImGui::SetNextItemWidth(avail_w);
    ImGui::SliderFloat("Azimuth", ctx->scope_az, 0.0f, 360.0f, "%.1f");
    ImGui::SetNextItemWidth(avail_w);
    ImGui::SliderFloat("Elevation", ctx->scope_el, -90.0f, 90.0f, "%.1f");
    ImGui::SetNextItemWidth(avail_w);
    ImGui::SliderFloat("Beam Width", ctx->scope_beam, 1.0f, 120.0f, "%.1f");

    ImGui::Separator();
    /* Use a wrapping layout for checkboxes so they adapt to sidebar width */
    float cb_w = ImGui::CalcTextSize("Show HEO/MEO").x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::GetFrameHeight();
    float avail_for_cb = avail_w;
    int items_per_row = (int)(avail_for_cb / (cb_w + ImGui::GetStyle().ItemSpacing.x));
    if (items_per_row < 1) items_per_row = 1;

    ImGui::Checkbox("Show LEO", &g_ui.scope_show_leo);
    if (items_per_row >= 2) { ImGui::SameLine(); }
    ImGui::Checkbox("Show HEO/MEO", &g_ui.scope_show_heo);
    if (items_per_row >= 3) { ImGui::SameLine(); }
    ImGui::Checkbox("Show GEO", &g_ui.scope_show_geo);
    if (items_per_row >= 4) { ImGui::SameLine(); }
    ImGui::Checkbox("Show Trails", &g_ui.scope_show_trails);

    ImGui::PopTextWrapPos();
}
