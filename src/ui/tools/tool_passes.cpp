/*
 * tool_passes.cpp - Satellite Passes panel
 */

#include "tools.h"
#include "ui/ui.h"
#include "core/astro.h"
#include "core/config.h"

#include <cstdio>

#include "imgui.h"

void DrawPanelPasses(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(0.0f);

    ImGui::SetNextItemWidth(avail_w);
    static char min_el_buf[8] = "0";
    ImGui::InputText("Min Elevation", min_el_buf, sizeof(min_el_buf));

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
