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

#include <cstdio>
#include <cmath>

#include <raylib.h>
#include <raymath.h>

#include "imgui.h"

static void DrawPolarPlotGrid(ImDrawList *dl, ImVec2 center, float radius)
{
    /* concentric rings for 0°, 30°, 60°, 90° elevation */
    int rings[4] = { 90, 60, 30, 0 };
    ImU32 ring_col = IM_COL32(120, 120, 140, 80);
    ImU32 ring_col_bold = IM_COL32(120, 120, 140, 160);

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
    ImU32 label_col = IM_COL32(180, 180, 200, 200);
    for (int i = 0; i < 4; i++)
    {
        ImVec2 pos = ImVec2(center.x + dirs[i].x * (radius + 12.0f),
                            center.y + dirs[i].y * (radius + 12.0f));
        dl->AddText(pos, label_col, labels[i]);
    }

    /* elevation labels on the 0° ring */
    dl->AddText(ImVec2(center.x + 4.0f, center.y + radius + 4.0f), IM_COL32(120, 120, 140, 120), "0°");
    dl->AddText(ImVec2(center.x + 4.0f, center.y + radius * 0.34f + 2.0f), IM_COL32(120, 120, 140, 100), "30°");
    dl->AddText(ImVec2(center.x + 4.0f, center.y + radius * 0.67f + 2.0f), IM_COL32(120, 120, 140, 80), "60°");
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
    /* background circle */
    dl->AddCircleFilled(center, radius + 4.0f, IM_COL32(10, 10, 16, 200), 64);
    DrawPolarPlotGrid(dl, center, radius);

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
        dl->AddCircleFilled(dot_pos, 6.0f, IM_COL32(100, 200, 255, 255), 16);
        dl->AddCircle(dot_pos, 6.0f, IM_COL32(200, 230, 255, 200), 16, 2.0f);

        /* draw path trace: compute future positions over ~90 min */
        ImVec2 prev_pt = dot_pos;
        int trace_steps = 60;
        double time_step_s = 90.0; /* 90 seconds per step = 90 min total */
        for (int j = 1; j <= trace_steps; j++)
        {
            double future_unix = get_unix_from_epoch(*ctx->current_epoch) + j * time_step_s;
            Vector3 future_pos = calculate_position(sat, future_unix);
            double faz = 0.0, fel = 0.0;
            get_az_el(future_pos, ctx->gmst_deg,
                      GetHomeLocation()->lat, GetHomeLocation()->lon, GetHomeLocation()->alt,
                      &faz, &fel);

            if (fel < 0.0) continue; /* skip below horizon */

            float fa_rad = (float)(faz * DEG2RAD);
            float fe_ratio = (90.0f - (float)fel) / 90.0f;
            float f_r = radius * fe_ratio;
            ImVec2 f_pos = ImVec2(
                center.x + f_r * sinf(fa_rad),
                center.y - f_r * cosf(fa_rad)
            );

            dl->AddLine(prev_pt, f_pos, IM_COL32(100, 200, 255, 100), 1.5f);
            prev_pt = f_pos;
        }

        /* satellite name label */
        char label[128];
        snprintf(label, sizeof(label), "%s  AZ: %.1f°  EL: %.1f°", sat->name, az, el);
        ImVec2 label_sz = ImGui::CalcTextSize(label);
        ImVec2 label_pos = ImVec2(canvas_pos.x + 6.0f, canvas_pos.y + 4.0f);
        dl->AddRectFilled(label_pos, ImVec2(label_pos.x + label_sz.x + 8.0f, label_pos.y + label_sz.y + 6.0f),
                          IM_COL32(10, 10, 16, 180), 4.0f);
        dl->AddText(ImVec2(label_pos.x + 4.0f, label_pos.y + 3.0f), IM_COL32(100, 200, 255, 255), label);
    }
    else
    {
        const char *msg = "No satellite selected";
        ImVec2 msg_sz = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(center.x - msg_sz.x * 0.5f, center.y - msg_sz.y * 0.5f),
                    IM_COL32(120, 120, 140, 160), msg);
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
