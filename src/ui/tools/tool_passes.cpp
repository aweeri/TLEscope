/*
 * tool_passes.cpp - Satellite Passes panel
 */

#include "tools.h"
#include "tools_settings.h"
#include "ui/ui.h"
#include "core/astro.h"
#include "core/config.h"

#include <cstdio>

#include "imgui.h"

void DrawPanelPasses(UIContext *ctx, AppConfig *cfg)
{
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(0.0f);

    /* pass settings (ROADMAP 12.3) - persisted via tool_settings */
    static bool s_loaded = false;
    if (!s_loaded)
    {
        pass_min_elev = ToolSettingGetFloat(cfg, "passes.min_elev", 0.0f);
        pass_time_span_hours = ToolSettingGetFloat(cfg, "passes.time_span_hours", 24.0f);
        s_loaded = true;
    }

    ImGui::SetNextItemWidth(avail_w);
    if (ImGui::SliderFloat("Min Elevation", &pass_min_elev, 0.0f, 90.0f, "%.0f°"))
        ToolSettingSetFloat(cfg, "passes.min_elev", pass_min_elev);

    ImGui::SetNextItemWidth(avail_w);
    if (ImGui::SliderFloat("Time Span", &pass_time_span_hours, 1.0f, 72.0f, "%.0f h"))
        ToolSettingSetFloat(cfg, "passes.time_span_hours", pass_time_span_hours);

    if (ImGui::Button("Calculate Passes", ImVec2(avail_w, 0)))
    {
        if (*ctx->selected_sat)
            CalculatePasses(*ctx->selected_sat, *ctx->current_epoch);
        else
            CalculatePasses(NULL, *ctx->current_epoch);
    }

    ImGui::Separator();
    ImGui::BeginChild("PassList");

    for (int i = 0; i < num_passes; i++)
    {
        char label[160];
        char aos_str[64];
        epoch_to_time_str(passes[i].aos_epoch, aos_str);
        snprintf(label, sizeof(label), "%s - El: %.1f @ %s",
                 passes[i].sat ? passes[i].sat->name : "Unknown",
                 passes[i].max_el, aos_str);

        if (ImGui::Selectable(label, i == g_ui.selected_pass_idx))
        {
            g_ui.selected_pass_idx = i;
        }
    }

    ImGui::EndChild();

    ImGui::PopTextWrapPos();
}
