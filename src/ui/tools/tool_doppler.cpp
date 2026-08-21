/*
 * tool_doppler.cpp - Doppler Analysis panel
 */

#include "tools.h"
#include "core/config.h"

#include <cstdio>

#include "imgui.h"

void DrawPanelDoppler(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(0.0f);

    static float freq = 145800000.0f; /* default: 2m band */
    static float csv_res = 1.0f;
    static char csv_path[128] = "doppler_export.csv";

    ImGui::SetNextItemWidth(avail_w);
    ImGui::InputFloat("Frequency (Hz)", &freq, 1000.0f, 1000000.0f, "%.0f");
    ImGui::SetNextItemWidth(avail_w);
    ImGui::InputFloat("CSV Resolution (s)", &csv_res, 0.1f, 10.0f);
    ImGui::SetNextItemWidth(avail_w);
    ImGui::InputText("Export Path", csv_path, sizeof(csv_path));

    if (ImGui::Button("Export CSV", ImVec2(avail_w, 0)))
    {
        /* TODO: Implement CSV export */
    }

    ImGui::PopTextWrapPos();
}
