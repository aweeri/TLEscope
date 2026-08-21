/*
 * tool_doppler.cpp - Doppler Analysis panel
 */

#include "tools.h"
#include "tools_common.h"
#include "core/astro.h"
#include "core/config.h"
#include "core/theme.h"

#include <cstdio>
#include <cmath>

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

    /* ---- 2.5: consume a pass handed off from the Passes panel ----
     * When a pass row's "Doppler" action was clicked, g_ui.locked_pass_sat /
     * aos / los are set. Preload the analysis target and window from them. */
    Satellite *target = g_ui.locked_pass_sat;
    double win_aos = g_ui.locked_pass_aos;
    double win_los = g_ui.locked_pass_los;

    if (target)
    {
        char aos_str[64], los_str[64];
        epoch_to_datetime_str(win_aos, aos_str);
        epoch_to_datetime_str(win_los, los_str);
        ImGui::TextColored(ThemeColor(g_theme.ui.ui_accent), "Target: %s", target->name);
        ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary), "AOS: %s", aos_str);
        ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary), "LOS: %s", los_str);
        ImGui::Separator();
    }
    else
    {
        ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary),
                           "No pass selected. Use \"Analyze in Doppler\" on a pass row.");
        ImGui::Separator();
    }

    ImGui::SetNextItemWidth(avail_w);
    ImGui::InputFloat("Frequency (Hz)", &freq, 1000.0f, 1000000.0f, "%.0f");
    ImGui::SetNextItemWidth(avail_w);
    ImGui::InputFloat("CSV Resolution (s)", &csv_res, 0.1f, 10.0f);
    ImGui::SetNextItemWidth(avail_w);
    ImGui::InputText("Export Path", csv_path, sizeof(csv_path));

    if (ImGui::Button("Export CSV", ImVec2(avail_w, 0)))
    {
        /* TODO: Implement CSV export (ROADMAP 4.1) */
    }

    ImGui::PopTextWrapPos();
}
