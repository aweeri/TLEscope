/*
 * tool_polar_plot.cpp - Polar Plot panel
 */

#include "tools.h"
#include "tools_plot.h"
#include "tools_registry.h"
#include "ui/ui_layout.h"
#include "ui/ui.h"
#include "core/astro.h"
#include "core/location.h"
#include "core/config.h"
#include "core/theme.h"
#include "io/rotator.h"

#include <cstdio>
#include <cmath>

#include <raylib.h>
#include <raymath.h>

#include "imgui.h"

/**
 * @brief mark the selected pass's AOS/LOS points in the scene.
 *
 * Registered as the draw_scene hook for PANEL_POLAR_PLOT. When a pass is
 * selected (g_ui.selected_pass_idx), this places a tick at the AOS and LOS
 * markers — on the 3D globe (DrawLine3D) and on the 2D map (DrawLineEx).
 */
void DrawScenePolarPlot(SceneContext *sctx, AppConfig *cfg)
{
    (void)cfg;
    if (g_ui.selected_pass_idx < 0 || g_ui.selected_pass_idx >= num_passes)
        return;

    SatPass *pass = &passes[g_ui.selected_pass_idx];
    if (!pass->sat || !pass->sat->is_active)
        return;

    /* AOS/LOS edge epochs (in normalized epoch days) */
    double aos_epoch = pass->aos_epoch;
    double los_epoch = pass->los_epoch;

    Color notch_col = g_theme.ui.warning;

    /* helper to compute the satellite's draw-space position at an epoch */
    auto pos_at = [&](double epoch) -> Vector3 {
        double unix = get_unix_from_epoch(epoch);
        return Vector3Scale(calculate_position(pass->sat, unix), 1.0f / DRAW_SCALE);
    };

    Vector3 aos_pos = pos_at(aos_epoch);
    Vector3 los_pos = pos_at(los_epoch);

    if (sctx->is_2d_view)
    {
        /* ---- 2D map ---- */
        float map_w = sctx->map_w, map_h = sctx->map_h;
        float tick_len = 14.0f / sctx->camera2d->zoom;
        double dt = 30.0 / 86400.0; /* 30 s sample for the tangent */

        /* helper: map coords at an epoch */
        auto map_at = [&](double epoch, float *x, float *y) {
            Vector3 p = pos_at(epoch);
            get_map_coordinates(Vector3Scale(p, DRAW_SCALE), sctx->gmst_deg,
                                sctx->earth_rotation_offset, map_w, map_h, x, y);
        };

        /* helper: draw a tick perpendicular to the orbit path in map space */
        auto draw_tick = [&](double epoch) {
            float x, y, x1, y1, x2, y2;
            map_at(epoch, &x, &y);
            map_at(epoch - dt, &x1, &y1);
            map_at(epoch + dt, &x2, &y2);
            float tx = x2 - x1, ty = y2 - y1;
            float len = sqrtf(tx * tx + ty * ty);
            if (len < 0.001f)
                return;
            float px = -ty / len, py = tx / len; /* perpendicular unit */
            for (int off = -1; off <= 1; off++)
            {
                float x_off = off * map_w;
                DrawLineEx((Vector2){x + x_off - px * tick_len, y - py * tick_len},
                           (Vector2){x + x_off + px * tick_len, y + py * tick_len},
                           3.0f / sctx->camera2d->zoom, notch_col);
            }
        };

        draw_tick(aos_epoch);
        draw_tick(los_epoch);
    }
    else
    {
        /* ---- 3D globe ---- */
        float tick_len = 0.05f; /* ~150 km at DRAW_SCALE, clearly visible */
        double dt = 30.0 / 86400.0; /* ~30 s sample for the tangent */

        /* helper: orbit tangent at an epoch (analytic velocity) */
        auto tangent_at = [&](double epoch) -> Vector3 {
            double unix = get_unix_from_epoch(epoch);
            return Vector3Normalize(calculate_velocity(pass->sat, unix));
        };

        /* helper: draw a tick perpendicular to the orbit and facing the camera */
        auto draw_tick = [&](Vector3 pos, Vector3 tangent) {
            Vector3 to_cam = Vector3Normalize(Vector3Subtract(sctx->camera3d->position, pos));
            Vector3 perp = Vector3Normalize(Vector3CrossProduct(tangent, to_cam));
            if (Vector3LengthSqr(perp) < 0.001f)
                perp = Vector3Normalize(Vector3CrossProduct(tangent, (Vector3){0, 1, 0}));
            DrawLine3D(Vector3Add(pos, Vector3Scale(perp, tick_len)),
                       Vector3Add(pos, Vector3Scale(perp, -tick_len)),
                       notch_col);
        };

        draw_tick(aos_pos, tangent_at(aos_epoch));
        draw_tick(los_pos, tangent_at(los_epoch));
    }
}

void DrawPanelPolarPlot(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(0.0f);

    Satellite *sat = *ctx->selected_sat;
    Location *home = GetHomeLocation();

    /* observation geometry shared by the header, canvas dot and readout */
    bool have_obs = (sat && sat->is_active && home);
    double az = 0.0, el = 0.0, range = 0.0;
    if (have_obs)
    {
        get_az_el(sat->current_pos, ctx->gmst_deg,
                  home->lat, home->lon, home->alt,
                  &az, &el);
        range = get_sat_range(sat, *ctx->current_epoch, *home);
    }

    /* ---- satellite name header (above the canvas) ---- */
    Color accent_theme = g_theme.ui.accent;
    if (have_obs)
    {
        ImGui::TextColored(ImVec4(accent_theme.r / 255.0f, accent_theme.g / 255.0f,
                                  accent_theme.b / 255.0f, 1.0f),
                           "%s", sat->name);
    }
    else
    {
        ImGui::TextDisabled("No satellite");
    }

    /* ---- Polar plot canvas ---- */
    float plot_size = fminf(avail_w, 360.0f);
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_sz = ImVec2(plot_size, plot_size);
    ImGui::InvisibleButton("##polar_canvas", canvas_sz);
    ImVec2 center = ImVec2(canvas_pos.x + plot_size * 0.5f, canvas_pos.y + plot_size * 0.5f);
    float radius = plot_size * 0.5f - 20.0f;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    /* background circle - theme-aware (12.2) */
    Color bg_theme = g_theme.ui.bg;
    dl->AddCircleFilled(center, radius + 4.0f,
                        IM_COL32(bg_theme.r, bg_theme.g, bg_theme.b, 200), 64);
    PlotDrawPolarGrid(dl, center, radius);

    /* ---- 2.4: render the selected pass sky path (SatPass.path_pts) ----
     * Draw the pass arc as a distinct highlighted polyline, with AOS/LOS
     * markers, the max-elevation point, and the current-time position. */
    if (g_ui.selected_pass_idx >= 0 && g_ui.selected_pass_idx < num_passes)
    {
        SatPass *pass = &passes[g_ui.selected_pass_idx];
        Color path_col = g_theme.ui.accent;
        Color marker_col = g_theme.ui.warning;

        auto project = [&](double az, double el) -> ImVec2 {
            return PlotPolarProject(center, radius, az, el);
        };

        /* draw the pass arc.
         * Break the polyline whenever the azimuth jumps by more than 180°
         * (a pass crossing the 0°/360° north boundary) so we never draw a
         * straight chord across the plot. */
        ImVec2 prev = {0, 0};
        bool has_prev = false;
        for (int k = 0; k < pass->num_pts; k++)
        {
            ImVec2 pt = project(pass->path_pts[k].x, pass->path_pts[k].y);
            if (has_prev)
            {
                double az_delta = fabs(pass->path_pts[k].x - pass->path_pts[k - 1].x);
                if (az_delta > 180.0)
                {
                    /* azimuth wrapped across north — start a new segment */
                    prev = pt;
                    continue;
                }
                dl->AddLine(prev, pt, IM_COL32(path_col.r, path_col.g, path_col.b, 200), 2.0f);
            }
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

        /* max-elevation direction arrow */
        PlotDrawMaxElArrow(dl, center, radius, pass, 1.0f);

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
    if (have_obs)
    {
        ImVec2 dot_pos = PlotPolarProject(center, radius, az, el);

        // below-horizon dot shrinks with depth
        float dot_scale = 1.0f;
        if (el < 0.0f)
        {
            float depth = (float)(-el) / 90.0f;   /* 0 at horizon, 1 at el = -90 */
            if (depth > 1.0f) depth = 1.0f;
            dot_scale = 1.0f - 0.8f * depth;   /* 1.0 -> 0.2 (reaches 0.2x at el = -90) */
        }
        float dot_radius = 6.0f * dot_scale;

        /* draw current position dot: filled above the horizon, hollow below */
        if (el >= 0.0)
        {
            dl->AddCircleFilled(dot_pos, dot_radius,
                                IM_COL32(accent_theme.r, accent_theme.g, accent_theme.b, 255), 16);
            dl->AddCircle(dot_pos, dot_radius,
                          IM_COL32(accent_theme.r, accent_theme.g, accent_theme.b, 200), 16, 2.0f);
        }
        else
        {
            dl->AddCircle(dot_pos, dot_radius,
                          IM_COL32(accent_theme.r, accent_theme.g, accent_theme.b, 255), 16, 2.5f);
        }
    }
    else
    {
        const char *msg = (sat && sat->is_active && !home)
                            ? "No home location configured"
                            : "No satellite selected";
        ImVec2 msg_sz = ImGui::CalcTextSize(msg);
        Color msg_theme = g_theme.ui.text_dim;
        dl->AddText(ImVec2(center.x - msg_sz.x * 0.5f, center.y - msg_sz.y * 0.5f),
                    IM_COL32(msg_theme.r, msg_theme.g, msg_theme.b, 160), msg);
    }

    // drawn last so the marker sits on top of grid, track and sky dot
    PlotDrawRotatorMarker(dl, center, radius);

    ImGui::Dummy(ImVec2(0, 6.0f));

    /* ---- AZ/EL readout (aligned two-column block) ---- */
    if (have_obs)
    {
        char azel_buf[64];
        PlotFormatAzEl(azel_buf, sizeof(azel_buf), az, el);
        if (ImGui::BeginTable("##polar_azel", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Pointing");
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(azel_buf);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Range");
            ImGui::TableSetColumnIndex(1); ImGui::Text("%.1f km", range);

            ImGui::EndTable();
        }
    }

    {
        bool rconn = RotatorIsConnected();
        bool rcomm = RotatorCommLost();
        bool rrunning = RotatorQueueIsRunning();
        int ractive = RotatorQueueActiveIndex();
        int rcount = RotatorQueueCount();
        const RotatorQueueItem *ritems = RotatorQueueItems();

        int rstate = PLOT_ROT_STATE_NONE;
        bool rslewing = false;
        if (rcomm)
            rstate = PLOT_ROT_STATE_COMM_LOST;
        else if (rrunning && ractive >= 0 && ritems)
        {
            rstate = (int)ritems[ractive].state;
            rslewing = !ritems[ractive].arrived;
        }
        else if (!rrunning && rcount > 0)
            rstate = RQ_PENDING;
        PlotDrawRotatorStatusLine("Rotator", rstate, rconn, rslewing);
    }

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
    if (ImGui::Button("Jump to AOS", ImVec2(avail_w, 0)))
    {
        /* warp smoothly to the next upcoming AOS */
        double now = *ctx->current_epoch;
        double target = -1.0;
        for (int i = 0; i < num_passes; i++)
        {
            if (passes[i].aos_epoch >= now &&
                (target < 0.0 || passes[i].aos_epoch < target))
            {
                target = passes[i].aos_epoch;
            }
        }
        /* fall back to the selected pass's AOS if none is upcoming */
        if (target < 0.0 &&
            g_ui.selected_pass_idx >= 0 && g_ui.selected_pass_idx < num_passes)
        {
            target = passes[g_ui.selected_pass_idx].aos_epoch;
        }

        if (target >= 0.0)
        {
            *ctx->auto_warp_target = target;
            *ctx->auto_warp_initial_diff = (target - *ctx->current_epoch) * 86400.0;
            *ctx->is_auto_warping = true;
        }
    }

    ImGui::PopTextWrapPos();
}
