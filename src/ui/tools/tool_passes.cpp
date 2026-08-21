/*
 * tool_passes.cpp - Satellite Passes panel
 *
 * ROADMAP section 2:
 *   2.1 Two pass modes (single-sat / all-active) + real-time updates.
 *   2.2 Richer pass entries with unique ImGui IDs, AOS/LOS, max elevation.
 *   2.3 Pass progress bar (green when ongoing).
 *   2.5 Pass-to-Doppler handoff.
 *   2.6 Pass list height cap with internal scroll.
 */

#include "tools.h"
#include "tools_settings.h"
#include "tools_common.h"
#include "tools_registry.h"
#include "ui/ui_layout.h"
#include "ui/ui.h"
#include "core/astro.h"
#include "core/config.h"
#include "core/theme.h"

#include <cstdio>
#include <cmath>

#include "imgui.h"

void DrawPanelPasses(UIContext *ctx, AppConfig *cfg)
{
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(0.0f);

    /* pass settings (ROADMAP 12.3) - persisted via tool_settings */
    static bool s_loaded = false;
    static int s_mode = 0; /* 0 = selected satellite, 1 = all active */
    static double s_last_calc_epoch = -1.0;
    if (!s_loaded)
    {
        pass_min_elev = ToolSettingGetFloat(cfg, "passes.min_elev", 0.0f);
        pass_time_span_hours = ToolSettingGetFloat(cfg, "passes.time_span_hours", 24.0f);
        s_mode = ToolSettingGetInt(cfg, "passes.mode", 0);
        s_loaded = true;
    }

    /* ---- 2.1: compact mode selector (single line) ---- */
    const char *mode_items[] = { "Selected Satellite", "All Active" };
    ImGui::SetNextItemWidth(avail_w);
    if (ImGui::Combo("##pass_mode", &s_mode, mode_items, 2))
        ToolSettingSetInt(cfg, "passes.mode", s_mode);

    ImGui::SetNextItemWidth(avail_w);
    if (ImGui::SliderFloat("Min Elevation", &pass_min_elev, 0.0f, 90.0f, "%.0f°"))
        ToolSettingSetFloat(cfg, "passes.min_elev", pass_min_elev);

    ImGui::SetNextItemWidth(avail_w);
    if (ImGui::SliderFloat("Time Span", &pass_time_span_hours, 1.0f, 72.0f, "%.0f h"))
        ToolSettingSetFloat(cfg, "passes.time_span_hours", pass_time_span_hours);

    /* ---- 2.1: real-time recompute ----
     * Recompute when the sim epoch advances past a small threshold, or when the
     * mode / span / min-elev changed. This keeps the listing current as the
     * simulation runs instead of only on a button press. */
    double now = *ctx->current_epoch;
    bool settings_changed = false;
    static float s_last_min_elev = -1.0f;
    static float s_last_span = -1.0f;
    static int s_last_mode = -1;
    if (s_last_min_elev != pass_min_elev || s_last_span != pass_time_span_hours ||
        s_last_mode != s_mode)
    {
        s_last_min_elev = pass_min_elev;
        s_last_span = pass_time_span_hours;
        s_last_mode = s_mode;
        settings_changed = true;
    }
    if (settings_changed || s_last_calc_epoch < 0.0 ||
        fabs(now - s_last_calc_epoch) > 60.0 / 86400.0)
    {
        if (s_mode == 0 && *ctx->selected_sat)
            CalculatePasses(*ctx->selected_sat, now);
        else
            CalculatePasses(NULL, now);
        s_last_calc_epoch = now;
    }

    ImGui::Separator();

    /* ---- 2.6: cap the list height to ~1/3 of the sidebar, internal scroll ---- */
    float sidebar_h = ImGui::GetIO().DisplaySize.y - ImGui::GetFrameHeight();
    float max_list_h = sidebar_h * 0.33f;
    float row_h = 20.0f + ImGui::GetStyle().ItemSpacing.y;
    float content_h = num_passes * row_h + ImGui::GetStyle().ItemSpacing.y;
    float avail_h = ImGui::GetContentRegionAvail().y;
    float list_h = fminf(fminf(content_h, max_list_h), avail_h);
    ImGui::BeginChild("##PassList", ImVec2(0.0f, list_h));

    for (int i = 0; i < num_passes; i++)
    {
        SatPass *p = &passes[i];
        const char *name = p->sat ? p->sat->name : "Unknown";

        /* ---- 2.2: unique ImGui ID from pass index + AOS epoch ---- */
        char id[64];
        snprintf(id, sizeof(id), "##pass_%d_%.0f", i, p->aos_epoch);
        ImGui::PushID(id);

        /* ---- 2.2: compact row content (name, max el, AOS time) ---- */
        char aos_str[64];
        epoch_to_time_str(p->aos_epoch, aos_str);

        /* ETA counter: time until AOS for future passes, time remaining for
         * ongoing passes. */
        double span = p->los_epoch - p->aos_epoch;
        bool ongoing = (span > 0.0 && now >= p->aos_epoch && now <= p->los_epoch);
        double eta_s = ongoing ? (p->los_epoch - now) * 86400.0
                               : (p->aos_epoch - now) * 86400.0;
        if (eta_s < 0.0) eta_s = 0.0;
        int eta_h = (int)(eta_s / 3600.0);
        int eta_m = (int)((eta_s - eta_h * 3600.0) / 60.0);
        int eta_sec = (int)(eta_s - eta_h * 3600.0 - eta_m * 60.0);
        char eta_str[32];
        if (eta_h > 0)
            snprintf(eta_str, sizeof(eta_str), "%dh %02dm", eta_h, eta_m);
        else if (eta_m > 0)
            snprintf(eta_str, sizeof(eta_str), "%dm %02ds", eta_m, eta_sec);
        else
            snprintf(eta_str, sizeof(eta_str), "%ds", eta_sec);

        char label[192];
        snprintf(label, sizeof(label), "%s  El %.1f°  %s  %s%s",
                 name, p->max_el, aos_str,
                 ongoing ? "T-" : "in ", eta_str);

        bool selected = (i == g_ui.selected_pass_idx);
        if (ImGui::Selectable(label, selected))
        {
            g_ui.selected_pass_idx = i;
            /* ---- 2.4: selecting a pass shows it in the Polar Plot ---- */
            LayoutOpenPanel(PANEL_POLAR_PLOT);
        }

        /* ---- 2.5: right-click a pass to hand it off to Doppler ---- */
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            g_ui.selected_pass_idx = i;
            g_ui.locked_pass_sat = p->sat;
            g_ui.locked_pass_aos = p->aos_epoch;
            g_ui.locked_pass_los = p->los_epoch;
            LayoutOpenPanel(PANEL_DOPPLER);
        }

        /* ---- 2.3: thin progress bar, only while the pass is
         * ongoing. Drawn directly so it can be just a few px tall. */
        if (ongoing)
        {
            double progress = (now - p->aos_epoch) / span;
            progress = fmax(0.0, fmin(1.0, progress));

            ImVec2 bar_sz = ImVec2(avail_w, 3.0f);
            ImVec2 bar_pos = ImGui::GetCursorScreenPos();
            ImGui::Dummy(bar_sz);

            ImDrawList *dl = ImGui::GetWindowDrawList();
            Color fill = g_theme.ui.notif_success;
            dl->AddRectFilled(bar_pos, ImVec2(bar_pos.x + bar_sz.x, bar_pos.y + bar_sz.y),
                              IM_COL32(fill.r, fill.g, fill.b, 200), 1.0f);
            float fill_w = bar_sz.x * (float)progress;
            if (fill_w > 0.0f)
                dl->AddRectFilled(bar_pos, ImVec2(bar_pos.x + fill_w, bar_pos.y + bar_sz.y),
                                  IM_COL32(fill.r, fill.g, fill.b, 255), 1.0f);
        }

        /* more separation between rows */
        ImGui::Spacing();
        ImGui::PopID();
    }

    ImGui::EndChild();

    ImGui::PopTextWrapPos();
}