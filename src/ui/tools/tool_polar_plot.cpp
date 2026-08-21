/*
 * tool_polar_plot.cpp - Polar Plot panel
 */

#include "tools.h"
#include "tools_registry.h"
#include "ui/ui_layout.h"
#include "ui/ui.h"
#include "core/astro.h"
#include "core/location.h"
#include "core/config.h"
#include "core/theme.h"

#include <cstdio>
#include <cmath>

#include <raylib.h>
#include <raymath.h>

#include "imgui.h"

static void DrawPolarPlotGrid(ImDrawList *dl, ImVec2 center, float radius)
{
    /* concentric rings for 0°, 30°, 60°, 90° elevation — theme-aware (12.2) */
    Color grid_theme = g_theme.ui.text_secondary;
    Color label_theme = g_theme.ui.text_secondary;
    int rings[4] = { 90, 60, 30, 0 };
    ImU32 ring_col = IM_COL32(grid_theme.r, grid_theme.g, grid_theme.b, 80);
    ImU32 ring_col_bold = IM_COL32(grid_theme.r, grid_theme.g, grid_theme.b, 160);

    for (int r = 0; r < 4; r++)
    {
        float r_ratio = (90.0f - rings[r]) / 90.0f;
        float r_px = radius * r_ratio;
        ImU32 col = (r == 0) ? ring_col_bold : ring_col;
        dl->AddCircle(center, r_px, col, 64, 1.0f);
    }

    /* crosshairs (N-S, E-W) */
    dl->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y), ring_col, 1.0f);
    dl->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius), ring_col, 1.0f);

    /* cardinal labels */
    const char *labels[] = { "N", "E", "S", "W" };
    ImVec2 dirs[] = {
        ImVec2(0, -1),  /* N = up */
        ImVec2(1, 0),   /* E = right */
        ImVec2(0, 1),   /* S = down */
        ImVec2(-1, 0)   /* W = left */
    };
    ImU32 label_col = IM_COL32(label_theme.r, label_theme.g, label_theme.b, 200);
    for (int i = 0; i < 4; i++)
    {
        ImVec2 pos = ImVec2(center.x + dirs[i].x * (radius + 12.0f),
                            center.y + dirs[i].y * (radius + 12.0f));
        dl->AddText(pos, label_col, labels[i]);
    }

    /* elevation labels on the 0° ring */
    dl->AddText(ImVec2(center.x + 4.0f, center.y + radius + 4.0f), IM_COL32(grid_theme.r, grid_theme.g, grid_theme.b, 120), "0°");
    dl->AddText(ImVec2(center.x + 4.0f, center.y + radius * 0.34f + 2.0f), IM_COL32(grid_theme.r, grid_theme.g, grid_theme.b, 100), "30°");
    dl->AddText(ImVec2(center.x + 4.0f, center.y + radius * 0.67f + 2.0f), IM_COL32(grid_theme.r, grid_theme.g, grid_theme.b, 80), "60°");
}

void DrawPanelPolarPlot(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(0.0f);

    ImGui::Checkbox("Lunar Mode", &g_ui.polar_lunar_mode);

    /* ---- Polar plot canvas ---- */
    float plot_size = fminf(avail_w, 360.0f);
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_sz = ImVec2(plot_size, plot_size);
    ImGui::InvisibleButton("##polar_canvas", canvas_sz);
    ImVec2 center = ImVec2(canvas_pos.x + plot_size * 0.5f, canvas_pos.y + plot_size * 0.5f);
    float radius = plot_size * 0.5f - 20.0f;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    /* background circle - theme-aware (12.2) */
    Color bg_theme = g_theme.ui.window_bg;
    Color accent_theme = g_theme.ui.ui_accent;
    dl->AddCircleFilled(center, radius + 4.0f,
                        IM_COL32(bg_theme.r, bg_theme.g, bg_theme.b, 200), 64);
    DrawPolarPlotGrid(dl, center, radius);

    /* ---- 2.4: render the selected pass sky path (SatPass.path_pts) ----
     * Draw the pass arc as a distinct highlighted polyline, with AOS/LOS
     * markers, the max-elevation point, and the current-time position. */
    if (g_ui.selected_pass_idx >= 0 && g_ui.selected_pass_idx < num_passes)
    {
        SatPass *pass = &passes[g_ui.selected_pass_idx];
        Color path_col = g_theme.ui.ui_accent;
        Color marker_col = g_theme.ui.notif_warning;
        Color maxel_col = g_theme.ui.notif_success;

        /* project a (az, el) pair to canvas coords (same as the live dot) */
        auto project = [&](double az, double el) -> ImVec2 {
            float a_rad = (float)(az * DEG2RAD);
            float e_ratio = (90.0f - (float)el) / 90.0f;
            float r = radius * e_ratio;
            return ImVec2(center.x + r * sinf(a_rad),
                          center.y - r * cosf(a_rad));
        };

        /* draw the pass arc */
        ImVec2 prev = {0, 0};
        bool has_prev = false;
        for (int k = 0; k < pass->num_pts; k++)
        {
            ImVec2 pt = project(pass->path_pts[k].x, pass->path_pts[k].y);
            if (has_prev)
                dl->AddLine(prev, pt, IM_COL32(path_col.r, path_col.g, path_col.b, 200), 2.0f);
            prev = pt;
            has_prev = true;
        }

        /* AOS / LOS markers */
        if (pass->num_pts > 0)
        {
            ImVec2 aos_pt = project(pass->path_pts[0].x, pass->path_pts[0].y);
            ImVec2 los_pt = project(pass->path_pts[pass->num_pts - 1].x,
                                    pass->path_pts[pass->num_pts - 1].y);
            dl->AddCircleFilled(aos_pt, 4.0f, IM_COL32(marker_col.r, marker_col.g, marker_col.b, 220), 12);
            dl->AddCircleFilled(los_pt, 4.0f, IM_COL32(marker_col.r, marker_col.g, marker_col.b, 220), 12);
        }

        /* max-elevation point */
        if (pass->num_pts > 0)
        {
            int max_idx = 0;
            for (int k = 1; k < pass->num_pts; k++)
                if (pass->path_pts[k].y > pass->path_pts[max_idx].y)
                    max_idx = k;
            ImVec2 max_pt = project(pass->path_pts[max_idx].x, pass->path_pts[max_idx].y);
            dl->AddCircleFilled(max_pt, 5.0f, IM_COL32(maxel_col.r, maxel_col.g, maxel_col.b, 240), 12);
        }

        /* current-time position along the pass */
        double now = *ctx->current_epoch;
        double span = pass->los_epoch - pass->aos_epoch;
        if (span > 0.0 && now >= pass->aos_epoch && now <= pass->los_epoch)
        {
            double frac = (now - pass->aos_epoch) / span;
            int idx = (int)(frac * (pass->num_pts - 1));
            if (idx < 0) idx = 0;
            if (idx >= pass->num_pts) idx = pass->num_pts - 1;
            ImVec2 cur = project(pass->path_pts[idx].x, pass->path_pts[idx].y);
            dl->AddCircleFilled(cur, 6.0f, IM_COL32(255, 255, 255, 255), 16);
            dl->AddCircle(cur, 6.0f, IM_COL32(accent_theme.r, accent_theme.g, accent_theme.b, 200), 16, 2.0f);
        }
    }

    /* ---- Plot satellite position ---- */
    Satellite *sat = *ctx->selected_sat;
    if (sat && sat->is_active)
    {
        double az = 0.0, el = 0.0;
        get_az_el(sat->current_pos, ctx->gmst_deg,
                  GetHomeLocation()->lat, GetHomeLocation()->lon, GetHomeLocation()->alt,
                  &az, &el);

        /* convert azimuth (degrees from North, clockwise) to canvas angle
         * canvas: 0° = up (North), clockwise = positive screen angle */
        float angle_rad = (float)(az * DEG2RAD);
        float el_ratio = (90.0f - (float)el) / 90.0f;
        float dot_r = radius * el_ratio;

        ImVec2 dot_pos = ImVec2(
            center.x + dot_r * sinf(angle_rad),
            center.y - dot_r * cosf(angle_rad)
        );

        /* draw current position dot */
        dl->AddCircleFilled(dot_pos, 6.0f,
                            IM_COL32(accent_theme.r, accent_theme.g, accent_theme.b, 255), 16);
        dl->AddCircle(dot_pos, 6.0f,
                      IM_COL32(accent_theme.r, accent_theme.g, accent_theme.b, 200), 16, 2.0f);

        /* satellite name label */
        char label[128];
        snprintf(label, sizeof(label), "%s  AZ: %.1f°  EL: %.1f°", sat->name, az, el);
        ImVec2 label_sz = ImGui::CalcTextSize(label);
        ImVec2 label_pos = ImVec2(canvas_pos.x + 6.0f, canvas_pos.y + 4.0f);
        dl->AddRectFilled(label_pos, ImVec2(label_pos.x + label_sz.x + 8.0f, label_pos.y + label_sz.y + 6.0f),
                          IM_COL32(bg_theme.r, bg_theme.g, bg_theme.b, 180), 4.0f);
        dl->AddText(ImVec2(label_pos.x + 4.0f, label_pos.y + 3.0f),
                    IM_COL32(accent_theme.r, accent_theme.g, accent_theme.b, 255), label);
    }
    else
    {
        const char *msg = "No satellite selected";
        ImVec2 msg_sz = ImGui::CalcTextSize(msg);
        Color msg_theme = g_theme.ui.text_secondary;
        dl->AddText(ImVec2(center.x - msg_sz.x * 0.5f, center.y - msg_sz.y * 0.5f),
                    IM_COL32(msg_theme.r, msg_theme.g, msg_theme.b, 160), msg);
    }

    ImGui::Dummy(ImVec2(0, 6.0f));

    /* ---- Pass info ---- */
    if (g_ui.selected_pass_idx >= 0 && g_ui.selected_pass_idx < num_passes)
    {
        SatPass *pass = &passes[g_ui.selected_pass_idx];
        char aos_str[64], los_str[64];
        epoch_to_datetime_str(pass->aos_epoch, aos_str);
        epoch_to_datetime_str(pass->los_epoch, los_str);
        ImGui::Text("Max Elevation: %.1f", pass->max_el);
        ImGui::Text("AOS: %s", aos_str);
        ImGui::Text("LOS: %s", los_str);
    }

    ImGui::Separator();
    float half_w = (avail_w - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Jump to AOS", ImVec2(half_w, 0)))
    {
        if (g_ui.selected_pass_idx >= 0 && g_ui.selected_pass_idx < num_passes)
        {
            *ctx->current_epoch = passes[g_ui.selected_pass_idx].aos_epoch;
        }

        ImGui::PopTextWrapPos();
    }
    ImGui::SameLine();
    if (ImGui::Button("Doppler Analysis", ImVec2(half_w, 0)))
    {
        LayoutOpenPanel(PANEL_DOPPLER);
    }

    ImGui::PopTextWrapPos();
}
