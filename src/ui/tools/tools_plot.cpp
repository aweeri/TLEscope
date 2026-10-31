/*
 * tools_plot.cpp - Shared, theme-aware drawing and sampling primitives.
 *
 * Extracted from tool_polar_plot.cpp so the Transit Tracking panel and its
 * overview modal share one implementation of the polar canvas, the XY charts
 * and the pass sampling.
 */

#include "tools_plot.h"
#include "ui/imgui_theme.h"
#include "core/theme.h"
#include "core/astro.h"
#include "io/rotator.h"

#include <cstdio>
#include <cmath>

#include <raylib.h>

/* ========================================================================== */
/*  Polar canvas                                                              */
/* ========================================================================== */

void PlotDrawPolarGrid(ImDrawList *dl, ImVec2 center, float radius)
{
    Color grid_theme = g_theme.ui.text_dim;
    Color label_theme = g_theme.ui.text_dim;

    int rings[3] = { 60, 30, 0 };
    ImU32 ring_col = IM_COL32(grid_theme.r, grid_theme.g, grid_theme.b, 80);
    ImU32 ring_col_bold = IM_COL32(grid_theme.r, grid_theme.g, grid_theme.b, 160);

    for (int r = 0; r < 3; r++)
    {
        float el = (float)rings[r];
        float r_norm = (90.0f - el) / 90.0f;
        float r_px = radius * r_norm;
        ImU32 col = (rings[r] == 0) ? ring_col_bold : ring_col;
        dl->AddCircle(center, r_px, col, 64, (rings[r] == 0) ? 2.0f : 1.0f);
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
        ImVec2 pos = ImVec2(center.x + dirs[i].x * (radius + UIPx(12.0f)),
                            center.y + dirs[i].y * (radius + UIPx(12.0f)));
        dl->AddText(pos, label_col, labels[i]);
    }

    /* elevation labels: nudge off the axis proportionally so they stay clear on a big plot */
    ImU32 lbl_col = IM_COL32(grid_theme.r, grid_theme.g, grid_theme.b, 140);
    float lbl_dx = fmaxf(UIPx(4.0f), radius * 0.035f);
    float lbl_dy = fmaxf(UIPx(2.0f), radius * 0.017f);
    /* +30° at radius*(2/3), +60° at radius/3 */
    dl->AddText(ImVec2(center.x + lbl_dx, center.y + radius * (2.0f / 3.0f) + lbl_dy), lbl_col, "30°");
    dl->AddText(ImVec2(center.x + lbl_dx, center.y + radius / 3.0f + lbl_dy), lbl_col, "60°");
}

ImVec2 PlotPolarProject(ImVec2 center, float radius, double az_deg, double el_deg)
{
    float a_rad = (float)(az_deg * DEG2RAD);
    float e_ratio = (90.0f - (float)el_deg) / 90.0f;
    if (e_ratio < 0.0f) e_ratio = 0.0f;
    if (e_ratio > 1.0f) e_ratio = 1.0f;
    float r = radius * e_ratio;
    return ImVec2(center.x + r * sinf(a_rad), center.y - r * cosf(a_rad));
}

void PlotDrawPolarTrack(ImDrawList *dl, ImVec2 center, float radius,
                        const SatPass *pass, double now_epoch, float arrow_scale)
{
    if (!pass || pass->num_pts <= 0)
        return;

    Color path_col   = g_theme.ui.accent;
    Color marker_col = g_theme.ui.warning;

    auto project = [&](double az, double el) -> ImVec2 {
        return PlotPolarProject(center, radius, az, el);
    };

    // break the polyline when the azimuth jumps >180° (crossing the 0°/360° north seam)
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
                prev = pt;
                continue;
            }
            dl->AddLine(prev, pt, IM_COL32(path_col.r, path_col.g, path_col.b, 200), UIPx(2.0f));
        }
        prev = pt;
        has_prev = true;
    }

    /* AOS / LOS markers */
    ImVec2 aos_pt = project(pass->path_pts[0].x, pass->path_pts[0].y);
    ImVec2 los_pt = project(pass->path_pts[pass->num_pts - 1].x,
                            pass->path_pts[pass->num_pts - 1].y);
    dl->AddCircleFilled(aos_pt, UIPx(4.0f), IM_COL32(marker_col.r, marker_col.g, marker_col.b, 220), 12);
    dl->AddCircleFilled(los_pt, UIPx(4.0f), IM_COL32(marker_col.r, marker_col.g, marker_col.b, 220), 12);

    /* max-elevation direction arrow */
    PlotDrawMaxElArrow(dl, center, radius, pass, UIPx(1.0f) * arrow_scale);

    /* current-time position along the pass */
    double span = pass->los_epoch - pass->aos_epoch;
    if (span > 0.0 && now_epoch >= pass->aos_epoch && now_epoch <= pass->los_epoch)
    {
        double frac = (now_epoch - pass->aos_epoch) / span;
        int idx = (int)(frac * (pass->num_pts - 1));
        if (idx < 0) idx = 0;
        if (idx >= pass->num_pts) idx = pass->num_pts - 1;
        ImVec2 cur = project(pass->path_pts[idx].x, pass->path_pts[idx].y);
        dl->AddCircleFilled(cur, UIPx(6.0f), IM_COL32(255, 255, 255, 255), 16);
        dl->AddCircle(cur, UIPx(6.0f), IM_COL32(path_col.r, path_col.g, path_col.b, 200), 16, UIPx(2.0f));
    }
}

void PlotDrawMaxElArrow(ImDrawList *dl, ImVec2 center, float radius,
                        const SatPass *pass, float px)
{
    if (!pass || pass->num_pts <= 0)
        return;

    Color maxel_col = g_theme.ui.success;

    auto project = [&](double az, double el) -> ImVec2 {
        return PlotPolarProject(center, radius, az, el);
    };

    int max_idx = 0;
    for (int k = 1; k < pass->num_pts; k++)
        if (pass->path_pts[k].y > pass->path_pts[max_idx].y)
            max_idx = k;

    ImVec2 max_pt = project(pass->path_pts[max_idx].x, pass->path_pts[max_idx].y);
    ImU32 col = IM_COL32(maxel_col.r, maxel_col.g, maxel_col.b, 240);

    /* travel direction from the neighbours; clamp at the ends */
    int i0 = max_idx - 1;
    int i1 = max_idx + 1;
    if (i0 < 0) i0 = max_idx;
    if (i1 >= pass->num_pts) i1 = max_idx;
    if (i0 == i1)
    {
        dl->AddCircleFilled(max_pt, 5.0f * px, col, 12);
        return;
    }

    // unwrap az across the 0/360 seam so the projected direction doesn't flip at north
    double az0 = pass->path_pts[i0].x;
    double az1 = pass->path_pts[i1].x;
    if (fabs(az1 - az0) > 180.0)
    {
        if (az1 < az0) az1 += 360.0;
        else az0 += 360.0;
    }
    ImVec2 p0 = project(az0, pass->path_pts[i0].y);
    ImVec2 p1 = project(az1, pass->path_pts[i1].y);

    float dx = p1.x - p0.x;
    float dy = p1.y - p0.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f)
    {
        dl->AddCircleFilled(max_pt, 5.0f * px, col, 12);
        return;
    }
    dx /= len;
    dy /= len;

    /* head only, centred on the max-el point */
    float head = 15.0f * px;
    float half = 10.0f * px;

    ImVec2 tip  = ImVec2(max_pt.x + dx * head * 0.5f,
                         max_pt.y + dy * head * 0.5f);
    ImVec2 perp = ImVec2(-dy, dx);
    ImVec2 base = ImVec2(tip.x - dx * head, tip.y - dy * head);
    dl->AddTriangleFilled(tip,
                          ImVec2(base.x + perp.x * half, base.y + perp.y * half),
                          ImVec2(base.x - perp.x * half, base.y - perp.y * half),
                          col);
}

void PlotDrawSkyDot(ImDrawList *dl, ImVec2 center, float radius,
                    double az_deg, double el_deg)
{
    Color accent = g_theme.ui.accent;

    float angle_rad = (float)(az_deg * DEG2RAD);
    float el_ratio = (90.0f - (float)el_deg) / 90.0f;
    if (el_ratio < 0.0f) el_ratio = 0.0f;
    if (el_ratio > 1.0f) el_ratio = 1.0f;
    float dot_r = radius * el_ratio;

    ImVec2 dot_pos = ImVec2(center.x + dot_r * sinf(angle_rad),
                            center.y - dot_r * cosf(angle_rad));

    /* below-horizon dot shrinks with depth */
    float dot_scale = 1.0f;
    if (el_deg < 0.0f)
    {
        float depth = (float)(-el_deg) / 90.0f;   /* 0 at horizon, 1 at el = -90 */
        if (depth > 1.0f) depth = 1.0f;
        dot_scale = 1.0f - 0.8f * depth;          /* 1.0 -> 0.2 */
    }
    float dot_radius = UIPx(6.0f) * dot_scale;

    /* filled above the horizon, hollow below */
    if (el_deg >= 0.0)
    {
        dl->AddCircleFilled(dot_pos, dot_radius,
                            IM_COL32(accent.r, accent.g, accent.b, 255), 16);
        dl->AddCircle(dot_pos, dot_radius,
                      IM_COL32(accent.r, accent.g, accent.b, 200), 16, UIPx(2.0f));
    }
    else
    {
        dl->AddCircle(dot_pos, dot_radius,
                      IM_COL32(accent.r, accent.g, accent.b, 255), 16, UIPx(2.5f));
    }
}

void PlotDrawRotatorMarker(ImDrawList *dl, ImVec2 center, float radius)
{
    if (!RotatorHasPosition())
        return;

    Color info = g_theme.ui.info;
    Color bg = g_theme.ui.bg;
    ImVec2 p = PlotPolarProject(center, radius, (double)RotatorGetAz(),
                                (double)RotatorGetEl());

    float tgt_az = 0.0f, tgt_el = 0.0f;
    if (RotatorGetCommandedTarget(&tgt_az, &tgt_el))
    {
        ImVec2 tp = PlotPolarProject(center, radius, (double)tgt_az, (double)tgt_el);
        dl->AddLine(p, tp, IM_COL32(info.r, info.g, info.b, 180), UIPx(1.5f));
    }

    float dot_r = UIPx(6.0f);
    dl->AddCircleFilled(p, dot_r, IM_COL32(info.r, info.g, info.b, 255), 20);
    dl->AddCircle(p, dot_r, IM_COL32(bg.r, bg.g, bg.b, 255), 20, UIPx(1.5f));
    dl->AddCircle(p, dot_r + UIPx(2.0f), IM_COL32(info.r, info.g, info.b, 160), 20,
                  UIPx(1.5f));
}

enum RotStateTone { TONE_DIM, TONE_INFO, TONE_SUCCESS };

struct RotStateRow
{
    int state;
    const char *label;
    RotStateTone tone;
};

static const RotStateRow kRotStateRows[] = {
    { PLOT_ROT_STATE_PARK, "Park",          TONE_INFO    },
    { RQ_PENDING,          "Queued",        TONE_DIM     },
    { RQ_ACTIVE,           "Tracking",      TONE_SUCCESS },
    { RQ_DONE,             "Done",          TONE_SUCCESS },
    { RQ_SKIPPED,          "Skipped",       TONE_INFO    },
    { RQ_AWAITING,         "Waiting",       TONE_DIM     },
    { RQ_AWAITING_AOS,     "Aiming at AOS", TONE_INFO    },
    { RQ_LOS_HOLD,         "LOS hold",      TONE_INFO    },
    { PLOT_ROT_STATE_NONE, "Waiting",       TONE_DIM     },
};

static const RotStateRow *RotStateFind(int state)
{
    for (const RotStateRow &row : kRotStateRows)
        if (row.state == state)
            return &row;
    return nullptr;
}

const char *PlotRotatorStateLabel(int state, bool connected, bool slewing)
{
    if (state == PLOT_ROT_STATE_COMM_LOST)
        return "Comm lost";
    if (!connected)
        return "Offline";
    if (state == RQ_ACTIVE && slewing)
        return "Slewing";
    const RotStateRow *row = RotStateFind(state);
    return row ? row->label : "Unknown";
}

ImU32 PlotRotatorStateColor(int state, bool connected)
{
    Color c = g_theme.ui.text_dim;
    if (state == PLOT_ROT_STATE_COMM_LOST)
        c = g_theme.ui.error;
    else if (connected)
    {
        const RotStateRow *row = RotStateFind(state);
        if (row && row->tone == TONE_SUCCESS) c = g_theme.ui.success;
        else if (row && row->tone == TONE_INFO) c = g_theme.ui.info;
    }
    return IM_COL32(c.r, c.g, c.b, 255);
}

Color PlotRotatorStateColorC(int state, bool connected)
{
    ImU32 u = PlotRotatorStateColor(state, connected);
    Color c;
    c.r = (unsigned char)((u >> IM_COL32_R_SHIFT) & 0xFF);
    c.g = (unsigned char)((u >> IM_COL32_G_SHIFT) & 0xFF);
    c.b = (unsigned char)((u >> IM_COL32_B_SHIFT) & 0xFF);
    c.a = 255;
    return c;
}

void PlotFormatAzEl(char *out, size_t n, double az, double el)
{
    if (!out || n == 0) return;
    snprintf(out, n, "Az %.1f° El %.1f°", az, el);
}

void PlotFormatAzElDelta(char *out, size_t n, double d_az, double d_el)
{
    if (!out || n == 0) return;
    // ASCII dAz/dEl only, the delta symbol is outside the bundled font range
    snprintf(out, n, "dAz %+.1f° dEl %+.1f°", d_az, d_el);
}

void PlotDrawRotatorStatusLine(const char *prefix, int state, bool connected,
                               bool slewing)
{
    const char *word = PlotRotatorStateLabel(state, connected, slewing);
    ImU32 col = PlotRotatorStateColor(state, connected);
    if (prefix && prefix[0])
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "%s: %s", prefix, word);
    else
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "%s", word);
}

/* ========================================================================== */
/*  XY charts                                                                 */
/* ========================================================================== */

// shared chart frame; returns the inner plot rect via out_p0/out_p1
static void PlotChartFrame(ImDrawList *dl, ImVec2 min, ImVec2 max,
                           float y_min, float y_max,
                           const char *title, const char *y_unit,
                           ImVec2 *out_p0, ImVec2 *out_p1)
{
    Color bg     = g_theme.ui.bg;
    Color border = g_theme.ui.border;
    Color grid   = g_theme.ui.text_dim;
    Color text   = g_theme.ui.text;

    float rounding = UIPx(4.0f);
    dl->AddRectFilled(min, max, IM_COL32(bg.r, bg.g, bg.b, 200), rounding);
    dl->AddRect(min, max, IM_COL32(border.r, border.g, border.b, 200), rounding);

    float pad_l = UIPx(52.0f), pad_r = UIPx(12.0f);
    float pad_t = UIPx(24.0f), pad_b = UIPx(20.0f);
    ImVec2 p0 = ImVec2(min.x + pad_l, min.y + pad_t);
    ImVec2 p1 = ImVec2(max.x - pad_r, max.y - pad_b);
    *out_p0 = p0;
    *out_p1 = p1;
    if (p1.x <= p0.x + 1.0f || p1.y <= p0.y + 1.0f)
        return;

    if (title && title[0])
        dl->AddText(ImVec2(min.x + UIPx(8.0f), min.y + UIPx(5.0f)),
                    IM_COL32(text.r, text.g, text.b, 230), title);

    ImU32 grid_col = IM_COL32(grid.r, grid.g, grid.b, 55);
    ImU32 lbl_col  = IM_COL32(grid.r, grid.g, grid.b, 190);

    const int hdiv = 4;
    for (int i = 0; i <= hdiv; i++)
    {
        float t = (float)i / (float)hdiv;
        float y = p1.y - t * (p1.y - p0.y);
        dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), grid_col, 1.0f);
        float val = y_min + t * (y_max - y_min);
        char lbl[32];
        snprintf(lbl, sizeof(lbl), "%.4g", val);
        ImVec2 ts = ImGui::CalcTextSize(lbl);
        dl->AddText(ImVec2(p0.x - ts.x - UIPx(5.0f), y - ts.y * 0.5f), lbl_col, lbl);
    }

    const int vdiv = 4;
    for (int i = 0; i <= vdiv; i++)
    {
        float t = (float)i / (float)vdiv;
        float x = p0.x + t * (p1.x - p0.x);
        dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), grid_col, 1.0f);
    }

    if (y_min < 0.0f && y_max > 0.0f)
    {
        float yz = p1.y - (0.0f - y_min) / (y_max - y_min) * (p1.y - p0.y);
        dl->AddLine(ImVec2(p0.x, yz), ImVec2(p1.x, yz),
                    IM_COL32(grid.r, grid.g, grid.b, 120), 1.0f);
    }

    if (y_unit && y_unit[0])
    {
        ImVec2 ts = ImGui::CalcTextSize(y_unit);
        dl->AddText(ImVec2(min.x + UIPx(8.0f), p0.y - ts.y - UIPx(2.0f)), lbl_col, y_unit);
    }
}

/* draw one series as a clipped polyline inside the plot rect */
static void PlotChartCurve(ImDrawList *dl, ImVec2 p0, ImVec2 p1,
                           const float *samples, int count,
                           float y_min, float y_max, ImU32 col, float thickness)
{
    if (!samples || count < 2)
        return;
    float range = y_max - y_min;
    if (range <= 0.0f) range = 1.0f;

    ImVec2 prev;
    for (int i = 0; i < count; i++)
    {
        float t = (float)i / (float)(count - 1);
        float x = p0.x + t * (p1.x - p0.x);
        float v = samples[i];
        if (v < y_min) v = y_min;
        if (v > y_max) v = y_max;
        float y = p1.y - (v - y_min) / range * (p1.y - p0.y);
        ImVec2 pt = ImVec2(x, y);
        if (i > 0)
            dl->AddLine(prev, pt, col, thickness);
        prev = pt;
    }
}

void PlotDrawHoverLabel(ImDrawList *dl, ImVec2 anchor, ImVec2 bounds_max, const char *text)
{
    if (!text || !text[0])
        return;
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 tp = ImVec2(anchor.x + UIPx(12.0f), anchor.y + UIPx(12.0f));
    if (tp.x + ts.x + UIPx(8.0f) > bounds_max.x)
        tp.x = anchor.x - ts.x - UIPx(12.0f);
    if (tp.y + ts.y + UIPx(8.0f) > bounds_max.y)
        tp.y = anchor.y - ts.y - UIPx(12.0f);
    Color bg = g_theme.ui.bg;
    Color text_col = g_theme.ui.text;
    Color dim = g_theme.ui.text_dim;
    dl->AddRectFilled(ImVec2(tp.x - UIPx(4.0f), tp.y - UIPx(3.0f)),
                      ImVec2(tp.x + ts.x + UIPx(4.0f), tp.y + ts.y + UIPx(3.0f)),
                      IM_COL32(bg.r, bg.g, bg.b, 235), UIPx(3.0f));
    dl->AddRect(ImVec2(tp.x - UIPx(4.0f), tp.y - UIPx(3.0f)),
                ImVec2(tp.x + ts.x + UIPx(4.0f), tp.y + ts.y + UIPx(3.0f)),
                IM_COL32(dim.r, dim.g, dim.b, 160), UIPx(3.0f));
    dl->AddText(tp, IM_COL32(text_col.r, text_col.g, text_col.b, 240), text);
}

/* current-position marker + hover readout drawn over a chart. s2 may be null. */
static void PlotChartOverlay(ImDrawList *dl, ImVec2 min, ImVec2 max,
                             ImVec2 p0, ImVec2 p1,
                             const float *s1, const float *s2, int count,
                             float y_min, float y_max,
                             float marker_frac,
                             PlotHoverFmt hover_fmt, void *hover_user)
{
    if (!s1 || count < 1)
        return;
    float range = y_max - y_min;
    if (range <= 0.0f) range = 1.0f;

    /* interpolate a series value at x fraction f between the bracketing samples */
    auto series_y_at = [&](const float *s, float f) -> float {
        float fpos = f * (float)(count - 1);
        int i0 = (int)floorf(fpos);
        if (i0 < 0) i0 = 0;
        if (i0 > count - 1) i0 = count - 1;
        int i1 = i0 + 1;
        if (i1 > count - 1) i1 = count - 1;
        float t = fpos - (float)i0;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        float v = s[i0] + (s[i1] - s[i0]) * t;
        if (v < y_min) v = y_min;
        if (v > y_max) v = y_max;
        return p1.y - (v - y_min) / range * (p1.y - p0.y);
    };

    Color accent = g_theme.ui.accent;
    Color warn   = g_theme.ui.warning;
    Color dim    = g_theme.ui.text_dim;

    if (marker_frac >= 0.0f)
    {
        float f = marker_frac;
        if (f > 1.0f) f = 1.0f;
        float x = p0.x + f * (p1.x - p0.x);
        dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y),
                    IM_COL32(dim.r, dim.g, dim.b, 120), 1.0f);
        dl->AddCircleFilled(ImVec2(x, series_y_at(s1, f)), UIPx(4.0f),
                            IM_COL32(accent.r, accent.g, accent.b, 255), 12);
        if (s2)
            dl->AddCircleFilled(ImVec2(x, series_y_at(s2, f)), UIPx(4.0f),
                                IM_COL32(warn.r, warn.g, warn.b, 255), 12);
    }

    if (hover_fmt && ImGui::IsMouseHoveringRect(min, max))
    {
        ImVec2 m = ImGui::GetMousePos();
        if (m.x >= p0.x && m.x <= p1.x && m.y >= p0.y && m.y <= p1.y)
        {
            float x_frac = (m.x - p0.x) / (p1.x - p0.x);
            if (x_frac < 0.0f) x_frac = 0.0f;
            if (x_frac > 1.0f) x_frac = 1.0f;
            /* interpolate between the two adjacent samples so the hover follows the cursor */
            float fpos = x_frac * (float)(count - 1);
            int i0 = (int)floorf(fpos);
            if (i0 < 0) i0 = 0;
            if (i0 > count - 1) i0 = count - 1;
            int i1 = i0 + 1;
            if (i1 > count - 1) i1 = count - 1;
            float t = fpos - (float)i0;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;

            float y_a = s1[i0] + (s1[i1] - s1[i0]) * t;
            float y_b = 0.0f;
            if (s2)
                y_b = s2[i0] + (s2[i1] - s2[i0]) * t;

            float sy_a = y_a;
            if (sy_a < y_min) sy_a = y_min;
            if (sy_a > y_max) sy_a = y_max;
            float py_a = p1.y - (sy_a - y_min) / range * (p1.y - p0.y);
            float py_b = 0.0f;
            if (s2)
            {
                float sy_b = y_b;
                if (sy_b < y_min) sy_b = y_min;
                if (sy_b > y_max) sy_b = y_max;
                py_b = p1.y - (sy_b - y_min) / range * (p1.y - p0.y);
            }

            float x = p0.x + x_frac * (p1.x - p0.x);
            dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y),
                        IM_COL32(dim.r, dim.g, dim.b, 150), 1.0f);
            dl->AddCircleFilled(ImVec2(x, py_a), UIPx(3.5f),
                                IM_COL32(accent.r, accent.g, accent.b, 255), 12);
            if (s2)
                dl->AddCircleFilled(ImVec2(x, py_b), UIPx(3.5f),
                                    IM_COL32(warn.r, warn.g, warn.b, 255), 12);

            char buf[192];
            buf[0] = '\0';
            if (hover_fmt(hover_user, x_frac, y_a, y_b,
                          buf, (int)sizeof(buf)) && buf[0])
                PlotDrawHoverLabel(dl, m, max, buf);
        }
    }
}

void PlotDrawXYChart(ImDrawList *dl, ImVec2 min, ImVec2 max,
                     const float *samples, int count,
                     float y_min, float y_max,
                     const char *title, const char *y_unit,
                     float marker_frac,
                     PlotHoverFmt hover_fmt, void *hover_user)
{
    ImVec2 p0, p1;
    PlotChartFrame(dl, min, max, y_min, y_max, title, y_unit, &p0, &p1);
    if (p1.x <= p0.x || p1.y <= p0.y)
        return;

    Color curve = g_theme.ui.accent;
    PlotChartCurve(dl, p0, p1, samples, count, y_min, y_max,
                   IM_COL32(curve.r, curve.g, curve.b, 235), UIPx(1.6f));

    PlotChartOverlay(dl, min, max, p0, p1, samples, nullptr, count,
                     y_min, y_max, marker_frac, hover_fmt, hover_user);
}

void PlotDrawXYChart(ImDrawList *dl, ImVec2 min, ImVec2 max,
                     const float *s1, const float *s2, int count,
                     float y_min, float y_max,
                     const char *title, const char *y_unit,
                     const char *label1, const char *label2,
                     float marker_frac,
                     PlotHoverFmt hover_fmt, void *hover_user)
{
    ImVec2 p0, p1;
    PlotChartFrame(dl, min, max, y_min, y_max, title, y_unit, &p0, &p1);
    if (p1.x <= p0.x || p1.y <= p0.y)
        return;

    Color c1 = g_theme.ui.accent;
    Color c2 = g_theme.ui.warning;
    PlotChartCurve(dl, p0, p1, s1, count, y_min, y_max,
                   IM_COL32(c1.r, c1.g, c1.b, 235), UIPx(1.6f));
    PlotChartCurve(dl, p0, p1, s2, count, y_min, y_max,
                   IM_COL32(c2.r, c2.g, c2.b, 235), UIPx(1.6f));

    Color text = g_theme.ui.text;
    float lh = ImGui::GetTextLineHeight();
    float lx = p1.x - UIPx(96.0f);
    float ly = min.y + UIPx(5.0f);
    if (lx < p0.x) lx = p0.x;

    if (label1 && label1[0])
    {
        dl->AddRectFilled(ImVec2(lx, ly + lh * 0.5f - UIPx(3.0f)),
                          ImVec2(lx + UIPx(10.0f), ly + lh * 0.5f + UIPx(3.0f)),
                          IM_COL32(c1.r, c1.g, c1.b, 235), UIPx(1.0f));
        dl->AddText(ImVec2(lx + UIPx(14.0f), ly),
                    IM_COL32(text.r, text.g, text.b, 220), label1);
    }
    if (label2 && label2[0])
    {
        float ly2 = ly + lh + UIPx(2.0f);
        dl->AddRectFilled(ImVec2(lx, ly2 + lh * 0.5f - UIPx(3.0f)),
                          ImVec2(lx + UIPx(10.0f), ly2 + lh * 0.5f + UIPx(3.0f)),
                          IM_COL32(c2.r, c2.g, c2.b, 235), UIPx(1.0f));
        dl->AddText(ImVec2(lx + UIPx(14.0f), ly2),
                    IM_COL32(text.r, text.g, text.b, 220), label2);
    }

    PlotChartOverlay(dl, min, max, p0, p1, s1, s2, count,
                     y_min, y_max, marker_frac, hover_fmt, hover_user);
}

/* ========================================================================== */
/*  Sampling                                                                  */
/* ========================================================================== */

void PlotSampleDoppler(Satellite *sat, double aos, double los, double base_freq,
                       Location obs, float *out, int count)
{
    if (!out || count <= 0)
        return;

    double span = (los > aos) ? (los - aos) : 0.0;
    for (int i = 0; i < count; i++)
    {
        double t = aos + span * (span > 0.0 ? (double)i / (double)(count - 1) : 0.0);
        double f = (span > 0.0 && sat) ? calculate_doppler_freq(sat, t, obs, base_freq)
                                       : base_freq;
        out[i] = (float)(f - base_freq); /* offset in Hz, S-curve view */
    }
}

void PlotSampleElevAz(const SatPass *pass, float *out_el, float *out_az, int count)
{
    if (!out_el || !out_az || count <= 0)
        return;

    if (!pass || pass->num_pts <= 0)
    {
        for (int k = 0; k < count; k++)
        {
            out_el[k] = 0.0f;
            out_az[k] = 0.0f;
        }
        return;
    }

    /* resample the pass path to `count` evenly spaced points across the same window */
    for (int k = 0; k < count; k++)
    {
        double frac = (count > 1) ? (double)k / (double)(count - 1) : 0.0;
        int idx = (int)(frac * (double)(pass->num_pts - 1) + 0.5);
        if (idx < 0) idx = 0;
        if (idx >= pass->num_pts) idx = pass->num_pts - 1;
        out_az[k] = pass->path_pts[idx].x;
        out_el[k] = pass->path_pts[idx].y;
    }
}
