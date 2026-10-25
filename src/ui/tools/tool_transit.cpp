/*
 * tool_transit.cpp - Transit Tracking panel + overview modal.
 *
 * Unifies the former "Satellite Passes" and "Polar Plot" panels into one
 * compact panel, and adds a large "Transit Overview" modal with three plot
 * tabs (Polar, Doppler Shift, Elevation & Azimuth).
 *
 * Selection is a single source of truth: g_ui.selected_pass_idx, backed by a
 * stable key (norad + AOS) so it stays locked to one pass while timewarping.
 */

#include "tools.h"
#include "tools_plot.h"
#include "tools_settings.h"
#include "tools_common.h"
#include "tools_registry.h"
#include "ui/ui_layout.h"
#include "ui/ui.h"
#include "ui/imgui_theme.h"
#include "render/map_view.h"
#include "core/astro.h"
#include "core/config.h"
#include "core/theme.h"
#include "core/location.h"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <raylib.h>
#include <raymath.h>

#include "imgui.h"
#include "IconsFontAwesome6.h"

/* samples across the AOS..LOS window for the modal charts */
#define TRANSIT_DOPPLER_SAMPLES 128

/* ---- frequency presets (values reused from tool_doppler.cpp) -------------- */

typedef struct { const char *label; double freq_hz; } TransitBandPreset;

static const TransitBandPreset s_bands[] = {
    { "2m",   145800000.0 },
    { "70cm", 435000000.0 },
    { "23cm", 1268000000.0 },
    { "13cm", 2400000000.0 },
    { "5cm",  5650000000.0 },
};

/* ---- shared panel/modal state -------------------------------------------- */

static bool   s_loaded = false;
static int    s_mode = 0;                 /* 0 = selected, 1 = all active, 2 = favorites */
static double s_last_calc_epoch = -1.0;
static bool   s_list_expanded = true;
static bool   s_list_scroll_reset = true; /* scroll the child to the top on next draw */
static bool   s_list_interacting = false; /* true while the user presses/drags the list */
static int    s_last_num_passes = -1;     /* list size last draw (detect refreshes) */
static bool   s_show_deep_space = false;  /* include deep-space/GEO/HEO sats in the list */
static int    s_requested_tab = -1;       /* modal tab to force-select on next open (-1 = none) */

/* stable selection identity: survives list recomputes while timewarping */
static bool     s_sel_key_valid = false;
static uint32_t s_sel_norad = 0;
static double   s_sel_aos = 0.0;
static int      s_sel_idx = -1;           /* index we last wrote to g_ui.selected_pass_idx */
static SatPass  s_sel_pass;               /* last resolved copy, shown if the pass leaves the list */

/* per-row hover-reveal slide offsets, keyed by norad + AOS so they survive recomputes */
static std::unordered_map<uint64_t, float> s_row_slide;

static uint64_t TransitRowKey(const SatPass *p)
{
    uint64_t n = p->sat ? (uint64_t)p->sat->norad_id_num : 0u;
    uint64_t a = (uint64_t)(p->aos_epoch * 86400.0);
    return (n << 32) ^ a;
}

/* passes marked for tracking: norad + AOS, matched with tolerance across recomputes */
typedef struct { uint32_t norad; double aos; } TransitMark;
static std::vector<TransitMark> s_marked;

/** true when this pass is marked for tracking */
static bool TransitIsMarked(const SatPass *p)
{
    if (!p) return false;
    uint32_t n = p->sat ? p->sat->norad_id_num : 0u;
    for (size_t i = 0; i < s_marked.size(); i++)
        if (s_marked[i].norad == n && fabs(p->aos_epoch - s_marked[i].aos) < 1e-3)
            return true;
    return false;
}

/** toggle the tracking mark for this pass */
static void TransitToggleMark(const SatPass *p)
{
    if (!p) return;
    uint32_t n = p->sat ? p->sat->norad_id_num : 0u;
    for (size_t i = 0; i < s_marked.size(); i++)
    {
        if (s_marked[i].norad == n && fabs(p->aos_epoch - s_marked[i].aos) < 1e-3)
        {
            s_marked.erase(s_marked.begin() + i);
            return;
        }
    }
    TransitMark m = { n, p->aos_epoch };
    s_marked.push_back(m);
}

/** map marked passes to current passes[] indices; the rotator module calls this each frame */
static int TransitResolveMarkedPasses(double now, int *out_indices, int max_out)
{
    (void)now;
    int n = 0;
    for (size_t m = 0; m < s_marked.size() && n < max_out; m++)
    {
        for (int i = 0; i < num_passes; i++)
        {
            if (passes[i].sat && passes[i].sat->norad_id_num == s_marked[m].norad &&
                fabs(passes[i].aos_epoch - s_marked[m].aos) < 1e-3)
            {
                out_indices[n++] = i;
                break;
            }
        }
    }
    return n;
}

/* ---- helpers ------------------------------------------------------------- */

/** compact ETA string: time until AOS (future) or time remaining (ongoing) */
static void TransitEtaString(double now, const SatPass *p, char *out, size_t out_sz)
{
    double span = p->los_epoch - p->aos_epoch;
    bool ongoing = (span > 0.0 && now >= p->aos_epoch && now <= p->los_epoch);
    double eta_s = ongoing ? (p->los_epoch - now) * 86400.0
                           : (p->aos_epoch - now) * 86400.0;
    if (eta_s < 0.0) eta_s = 0.0;
    int eta_h = (int)(eta_s / 3600.0);
    int eta_m = (int)((eta_s - eta_h * 3600.0) / 60.0);
    int eta_sec = (int)(eta_s - eta_h * 3600.0 - eta_m * 60.0);
    if (eta_h > 0)
        snprintf(out, out_sz, "%dh %02dm", eta_h, eta_m);
    else if (eta_m > 0)
        snprintf(out, out_sz, "%dm %02ds", eta_m, eta_sec);
    else
        snprintf(out, out_sz, "%ds", eta_sec);
}

static double SatPeriodMinutes(const Satellite *s)
{
    if (!s || s->mean_motion <= 0.0)
        return 0.0;
    const double two_pi = 6.28318530717958647692;
    return (two_pi / s->mean_motion) / 60.0;
}

static bool SatIsDeepSpace(const Satellite *s)
{
    if (!s)
        return false;
    if (s->prop.valid)
        return s->prop.is_deep_space;
    return SatPeriodMinutes(s) > 225.0;
}

/** record the selected pass and its stable key (norad + AOS) */
static void TransitSelectIndex(int i)
{
    g_ui.selected_pass_idx = i;
    s_sel_idx = i;
    s_sel_key_valid = true;
    s_sel_norad = passes[i].sat ? passes[i].sat->norad_id_num : 0;
    s_sel_aos = passes[i].aos_epoch;
    s_sel_pass = passes[i];
}

/** drop the selection and its key */
static void TransitClearSelection(void)
{
    g_ui.selected_pass_idx = -1;
    s_sel_idx = -1;
    s_sel_key_valid = false;
    s_sel_norad = 0;
    s_sel_aos = 0.0;
}

/** the selected pass: live when in the list, else the retained copy */
static SatPass *TransitSelectedPass(void)
{
    if (g_ui.selected_pass_idx >= 0 && g_ui.selected_pass_idx < num_passes)
        return &passes[g_ui.selected_pass_idx];
    if (s_sel_key_valid)
        return &s_sel_pass;
    return NULL;
}

/** re-bind the stable key to its current index after a list recompute */
static void TransitSyncSelection(void)
{
    /* an external writer (e.g. Doppler release) changed the index */
    if (g_ui.selected_pass_idx != s_sel_idx)
    {
        if (g_ui.selected_pass_idx >= 0 && g_ui.selected_pass_idx < num_passes)
            TransitSelectIndex(g_ui.selected_pass_idx);
        else
            TransitClearSelection();
        return;
    }
    if (!s_sel_key_valid)
        return;

    int found = -1;
    for (int i = 0; i < num_passes; i++)
    {
        if (passes[i].sat && passes[i].sat->norad_id_num == s_sel_norad &&
            fabs(passes[i].aos_epoch - s_sel_aos) < 1e-3)
        {
            found = i;
            break;
        }
    }
    if (found >= 0)
    {
        g_ui.selected_pass_idx = found;
        s_sel_idx = found;
        s_sel_pass = passes[found];
    }
    else
    {
        /* pass left the window: keep the key + copy so the view does not jump */
        g_ui.selected_pass_idx = -1;
        s_sel_idx = -1;
    }
}

/** load persisted settings once and keep the pass list current */
static void TransitEnsurePasses(UIContext *ctx, AppConfig *cfg)
{
    if (!s_loaded)
    {
        pass_min_elev = ToolSettingGetFloat(cfg, "passes.min_elev", 0.0f);
        pass_time_span_hours = ToolSettingGetFloat(cfg, "passes.time_span_hours", 24.0f);
        s_mode = ToolSettingGetInt(cfg, "passes.mode", 0); /* default: selected sat */
        if (s_mode < 0 || s_mode > 2)
            s_mode = 0;
        s_list_expanded = ToolSettingGetBool(cfg, "transit.list_expanded", true);
        s_show_deep_space = ToolSettingGetBool(cfg, "transit.show_deep_space", false);
        s_loaded = true;
    }

    // a recompute would reshuffle rows under the cursor and abort the click
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
        ImGui::IsMouseReleased(ImGuiMouseButton_Left) || s_list_interacting)
        return;

    double now = *ctx->current_epoch;

    /* recompute only when the pass-list inputs change, not on a timer */
    if (EnsurePassesUpToDate((unsigned int)s_mode, *ctx->selected_sat, now))
        s_last_calc_epoch = now;

    TransitSyncSelection();
}

/** warp the simulation to the selected transit's AOS (nearest upcoming if none) */
static void TransitJumpToAos(UIContext *ctx)
{
    double now = *ctx->current_epoch;
    double target = -1.0;

    SatPass *sel = TransitSelectedPass();
    if (sel)
    {
        target = sel->aos_epoch;
    }
    else
    {
        for (int i = 0; i < num_passes; i++)
        {
            if (passes[i].aos_epoch >= now &&
                (target < 0.0 || passes[i].aos_epoch < target))
                target = passes[i].aos_epoch;
        }
    }

    if (target >= 0.0)
    {
        *ctx->auto_warp_target = target;
        *ctx->auto_warp_initial_diff = (target - *ctx->current_epoch) * 86400.0;
        *ctx->is_auto_warping = true;
    }
}

/** the satellite of the currently selected transit, or NULL when none is selected */
static Satellite *TransitSelectedSat(void)
{
    SatPass *p = TransitSelectedPass();
    return p ? p->sat : NULL;
}

/** pass filter row shared by the panel and the overview list (same persisted keys) */
static void TransitDrawFilterRow(AppConfig *cfg, const char *id, float avail_w)
{
    char combo_id[64], slider_id[64], btn_id[64], pop_id[64], span_id[64];
    snprintf(combo_id, sizeof(combo_id), "##%s_mode", id);
    snprintf(slider_id, sizeof(slider_id), "##%s_minel", id);
    snprintf(btn_id, sizeof(btn_id), ICON_FA_CLOCK "##%s_span", id);
    snprintf(pop_id, sizeof(pop_id), "##%s_span_pop", id);
    snprintf(span_id, sizeof(span_id), "##%s_span", id);

    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float pop_w = ImGui::GetFrameHeight();
    float combo_w = avail_w * 0.42f;
    float slider_w = avail_w - combo_w - pop_w - spacing * 2.0f;
    if (slider_w < UIPx(60.0f)) slider_w = UIPx(60.0f);

    const char *mode_items[] = { "Selected", "All Active", "Favorites" };
    ImGui::SetNextItemWidth(combo_w);
    if (ImGui::Combo(combo_id, &s_mode, mode_items, 3))
        ToolSettingSetInt(cfg, "passes.mode", s_mode);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pass source");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(slider_w);
    if (ImGui::SliderFloat(slider_id, &pass_min_elev, 0.0f, 90.0f, "%.0f°"))
        ToolSettingSetFloat(cfg, "passes.min_elev", pass_min_elev);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Minimum elevation");

    ImGui::SameLine();
    if (ImGui::Button(btn_id, ImVec2(pop_w, 0)))
        ImGui::OpenPopup(pop_id);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Prediction window");
    if (ImGui::BeginPopup(pop_id))
    {
        ImGui::TextDisabled("Prediction window");
        ImGui::SetNextItemWidth(UIPx(180.0f));
        if (ImGui::SliderFloat(span_id, &pass_time_span_hours, 1.0f, 72.0f, "%.0f h"))
            ToolSettingSetFloat(cfg, "passes.time_span_hours", pass_time_span_hours);
        ImGui::EndPopup();
    }

    /* deep-space (>225 min period) inclusion toggle */
    char deep_id[64];
    snprintf(deep_id, sizeof(deep_id), "Show deep-space##%s_deep", id);
    if (ImGui::Checkbox(deep_id, &s_show_deep_space))
        ToolSettingSetBool(cfg, "transit.show_deep_space", s_show_deep_space);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Include geostationary / high-orbit sats (period > 225 min)");
}

/** hover-reveal skip button shared by the panel and overview rows; returns the slide offset */
static float TransitRowReveal(UIContext *ctx, int i, SatPass *p, bool selected,
                              ImVec2 row_start, float row_h, float content_right, bool overview)
{
    /* hover zone: a short right-edge strip just wider than the button */
    float btn_w = UIPx(30.0f);
    float zone_x0 = content_right - (btn_w + UIPx(14.0f));
    bool reveal_hover = ImGui::IsMouseHoveringRect(
        ImVec2(zone_x0, row_start.y), ImVec2(content_right, row_start.y + row_h));

    float slide_target = btn_w + UIPx(10.0f);
    /* the panel and the modal draw the same passes: keep their slide state separate */
    uint64_t slide_key = TransitRowKey(p) ^ (overview ? (1ull << 63) : 0ull);
    float off = 0.0f;
    {
        std::unordered_map<uint64_t, float>::iterator it = s_row_slide.find(slide_key);
        if (it != s_row_slide.end()) off = it->second;
    }
    float slide_to = reveal_hover ? slide_target : 0.0f;
    off += (slide_to - off) * (1.0f - expf(-14.0f * ImGui::GetIO().DeltaTime));
    if (fabsf(off - slide_to) < 0.05f) off = slide_to;
    if (off > 0.0f || reveal_hover)
        s_row_slide[slide_key] = off;
    else
        s_row_slide.erase(slide_key);

    /* two compact buttons stacked flush right: skip-to-AOS on top, mark below */
    float btn_h = UIPx(22.0f);
    float gap = UIPx(2.0f);
    float stack_h = btn_h * 2.0f + gap;
    float stack_y = row_start.y + (row_h - stack_h) * 0.5f;
    float btn_x = content_right - btn_w - UIPx(4.0f);
    float slide_a = (slide_target > 0.0f) ? off / slide_target : 0.0f;
    ImGui::PushID(i);

    /* keep the alpha on the button only and snap to 1 on hover; draw the tooltip after the pop */
    ImVec2 skip_min = ImVec2(btn_x, stack_y);
    ImVec2 skip_max = ImVec2(btn_x + btn_w, stack_y + btn_h);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                        ImGui::IsMouseHoveringRect(skip_min, skip_max) ? 1.0f : slide_a);
    ImGui::SetCursorScreenPos(skip_min);
    if (ImGui::Button(ICON_FA_FORWARD, ImVec2(btn_w, btn_h)))
    {
        if (!selected)
        {
            TransitSelectIndex(i);
            if (p->sat) *ctx->selected_sat = p->sat;
        }
        TransitJumpToAos(ctx);
    }
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(selected
            ? "skip forward in time to the beginning of this transit"
            : "Select and skip forward in time to the beginning of this transit");

    ImVec2 mark_min = ImVec2(btn_x, stack_y + btn_h + gap);
    ImVec2 mark_max = ImVec2(btn_x + btn_w, stack_y + btn_h + gap + btn_h);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                        ImGui::IsMouseHoveringRect(mark_min, mark_max) ? 1.0f : slide_a);
    ImGui::SetCursorScreenPos(mark_min);
    if (ImGui::Button(ICON_FA_CROSSHAIRS, ImVec2(btn_w, btn_h)))
        TransitToggleMark(p);
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Mark for tracking");

    ImGui::PopID();

    return off;
}

/* ========================================================================== */
/*  Scene hook: accent-colored line along the transit portion of the orbit    */
/* ========================================================================== */

void DrawSceneTransit(SceneContext *sctx, AppConfig *cfg)
{
    (void)cfg;
    SatPass *pass = TransitSelectedPass();
    if (!pass || !pass->sat || !pass->sat->is_active)
        return;

    double aos_epoch = pass->aos_epoch;
    double los_epoch = pass->los_epoch;

    /* nothing to draw */
    if (los_epoch <= aos_epoch)
        return;

    const int TRANSIT_LINE_SAMPLES = 128;

    /* helper to compute the satellite's draw-space position at an epoch */
    auto pos_at = [&](double epoch) -> Vector3 {
        double unix = get_unix_from_epoch(epoch);
        return Vector3Scale(calculate_position(pass->sat, unix), 1.0f / DRAW_SCALE);
    };

    if (sctx->is_2d_view)
    {
        /* ---- 2D map ---- */
        float map_w = sctx->map_w, map_h = sctx->map_h;

        /* project every sample to map space once, then stamp it across copies */
        Vector2 pts[TRANSIT_LINE_SAMPLES + 1];
        for (int i = 0; i <= TRANSIT_LINE_SAMPLES; i++)
        {
            double t = aos_epoch + (los_epoch - aos_epoch) * ((double)i / TRANSIT_LINE_SAMPLES);
            Vector3 p = pos_at(t);
            /* per-sample GMST */
            double gmst_t = epoch_to_gmst(t);
            get_map_coordinates(Vector3Scale(p, DRAW_SCALE), gmst_t,
                                sctx->earth_rotation_offset, map_w, map_h, &pts[i].x, &pts[i].y);
        }

        int first_copy = 0, last_copy = 0;
        MapVisibleCopyRange(*sctx->camera2d, map_w, &first_copy, &last_copy);

        float line_w = 2.0f / sctx->camera2d->zoom;
        for (int offset_i = first_copy; offset_i <= last_copy; offset_i++)
        {
            float x_off = offset_i * map_w;
            for (int i = 1; i <= TRANSIT_LINE_SAMPLES; i++)
            {
                /* skip the seam so the segment doesn't nyoom across */
                if (fabs(pts[i].x - pts[i - 1].x) < map_w * 0.6f)
                {
                    DrawLineEx((Vector2){pts[i - 1].x + x_off, pts[i - 1].y},
                               (Vector2){pts[i].x + x_off, pts[i].y}, line_w, g_theme.ui.accent);
                }
            }
        }
    }
    else
    {
        /* ---- 3D globe ---- */
        rlSetLineWidth(3.0f);
        Vector3 prev = pos_at(aos_epoch);
        for (int i = 1; i <= TRANSIT_LINE_SAMPLES; i++)
        {
            double t = aos_epoch + (los_epoch - aos_epoch) * ((double)i / TRANSIT_LINE_SAMPLES);
            Vector3 cur = pos_at(t);
            if (!IsOrbitSegmentOccluded(sctx->camera3d->position, prev, cur, EARTH_RADIUS_KM / DRAW_SCALE))
                DrawLine3D(prev, cur, g_theme.ui.accent);
            prev = cur;
        }
        rlSetLineWidth(1.0f);
    }
}

/* ========================================================================== */
/*  Compact panel                                                             */
/* ========================================================================== */

void DrawPanelTransit(UIContext *ctx, AppConfig *cfg)
{
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(0.0f);

    TransitEnsurePasses(ctx, cfg);

    /* ---- filter row (shared with the overview list) ---- */
    TransitDrawFilterRow(cfg, "transit", avail_w);

    /* ---- header + overview button ---- */
    ImGui::Separator();
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "%d pass%s", num_passes, num_passes == 1 ? "" : "es");
    ImGui::TextDisabled("%s", hdr);

    const char *ov_label = ICON_FA_EXPAND " Full overview";
    float ov_w = ImGui::CalcTextSize(ov_label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine();
    float remain = ImGui::GetContentRegionAvail().x;
    if (remain > ov_w)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (remain - ov_w));
    if (ImGui::Button(ov_label))
        g_ui.transit_overview_open = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open the full transit overview");

    /* ---- transit list ---- */
    const char *chev = s_list_expanded ? ICON_FA_CHEVRON_DOWN : ICON_FA_CHEVRON_RIGHT;
    ImVec2 chev_sz = ImGui::CalcTextSize(chev);
    float hdr_h = ImGui::GetFrameHeight();
    ImVec2 toggle_pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##transit_list_toggle", ImVec2(avail_w, hdr_h));
    if (ImGui::IsItemClicked())
    {
        s_list_expanded = !s_list_expanded;
        s_list_scroll_reset = true;
        ToolSettingSetBool(cfg, "transit.list_expanded", s_list_expanded);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(s_list_expanded ? "Collapse list" : "Expand list");

    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        Color text_col = g_theme.ui.text;
        float ty = toggle_pos.y + (hdr_h - chev_sz.y) * 0.5f;
        dl->AddText(ImVec2(toggle_pos.x + UIPx(2.0f), ty),
                    IM_COL32(text_col.r, text_col.g, text_col.b, 220), chev);
        dl->AddText(ImVec2(toggle_pos.x + UIPx(2.0f) + chev_sz.x + UIPx(6.0f), ty),
                    IM_COL32(text_col.r, text_col.g, text_col.b, 220), "Upcoming passes");
    }

    if (s_list_expanded && num_passes > 0)
    {
        ImGuiStyle &st = ImGui::GetStyle();
        float sidebar_h = ImGui::GetIO().DisplaySize.y - ImGui::GetFrameHeight();
        float max_list_h = sidebar_h * 0.33f;
        float lh = ImGui::GetTextLineHeight();
        float row_h = lh * 3.0f + UIPx(12.0f);   /* three text lines per row */
        float per_row_h = row_h + st.ItemSpacing.y;
        /* count visible rows (deep-space hidden when the toggle is off) */
        int visible_rows = 0;
        for (int i = 0; i < num_passes; i++)
            if (s_show_deep_space || !SatIsDeepSpace(passes[i].sat))
                visible_rows++;
        float content_h = visible_rows * per_row_h + st.ItemSpacing.y;
        float min_list_h = row_h + st.WindowPadding.y * 2.0f;
        float avail_h = ImGui::GetContentRegionAvail().y;
        float list_h = fminf(fminf(content_h, max_list_h), avail_h);
        if (list_h < min_list_h) list_h = min_list_h;

        if (s_last_num_passes != num_passes)
        {
            s_last_num_passes = num_passes;
            s_list_scroll_reset = true;   /* list refreshed -> keep the first row visible */
        }

        ImGui::BeginChild("##TransitList", ImVec2(0.0f, list_h),
                          ImGuiChildFlags_AlwaysUseWindowPadding);
        if (s_list_scroll_reset)
        {
            ImGui::SetScrollY(0.0f);
            s_list_scroll_reset = false;
        }

        float step = lh + UIPx(1.0f);

        for (int i = 0; i < num_passes; i++)
        {
            SatPass *p = &passes[i];
            bool deep = SatIsDeepSpace(p->sat);
            /* deep-space sats hidden unless the toggle is on */
            if (deep && !s_show_deep_space)
                continue;
            const char *name = p->sat ? p->sat->name : "Unknown";
            const char *norad = (p->sat && p->sat->norad_id[0]) ? p->sat->norad_id : "sat";

            bool selected = (i == g_ui.selected_pass_idx);
            ImVec2 row_start = ImGui::GetCursorScreenPos();

            /* stable id (no ticking text) so a mid-press recompute can't drop the click */
            char row_id[64];
            snprintf(row_id, sizeof(row_id), "##transit_%s_%d", norad, i);
            if (ImGui::Selectable(row_id, selected, ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(0.0f, row_h)))
            {
                /* clicking the already-selected row deselects it */
                if (selected)
                    TransitClearSelection();   /* keep the satellite selected on deselect */
                else
                {
                    TransitSelectIndex(i);
                    if (p->sat) *ctx->selected_sat = p->sat;
                }
            }

            bool clicked_right = ImGui::IsItemClicked(ImGuiMouseButton_Right);

            /* hover-reveal: a short right-edge strip slides the row left to expose a skip button */
            float content_right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
            float off = TransitRowReveal(ctx, i, p, selected, row_start, row_h, content_right, false);
            float shift = -off;

            ImDrawList *dl = ImGui::GetWindowDrawList();
            Color text = g_theme.ui.text;
            Color dim = g_theme.ui.text_dim;
            Color accent = g_theme.ui.accent;
            Color warn = g_theme.ui.warning;
            Color info = g_theme.ui.info;

            dl->PushClipRect(ImVec2(row_start.x, row_start.y),
                             ImVec2(content_right, row_start.y + row_h), true);

            /* notch first, text inset past it so the first glyph stays clear */
            if (selected)
                dl->AddRectFilled(ImVec2(row_start.x + shift, row_start.y),
                                  ImVec2(row_start.x + shift + UIPx(3.0f), row_start.y + row_h),
                                  IM_COL32(accent.r, accent.g, accent.b, 255));

            /* blue right-edge strip marks a pass for tracking */
            if (TransitIsMarked(p))
                dl->AddRectFilled(ImVec2(content_right - UIPx(3.0f), row_start.y),
                                  ImVec2(content_right, row_start.y + row_h),
                                  IM_COL32(info.r, info.g, info.b, 255));

            float tx = row_start.x + shift + UIPx(12.0f);
            float ty = row_start.y + UIPx(4.0f);

            char line1[128];
            snprintf(line1, sizeof(line1), "%s  #%s", name, norad);
            dl->AddText(ImVec2(tx, ty), IM_COL32(text.r, text.g, text.b, 235), line1);

            if (deep)
            {
                /* deep-space / GEO / HEO: not a transiting pass, no AOS/LOS applies */
                char line2[192];
                snprintf(line2, sizeof(line2), "Deep-space / GEO / HEO   %s",
                         (p->sat && p->sat->is_active) ? "always in view" : "inactive");
                dl->AddText(ImVec2(tx, ty + step), IM_COL32(dim.r, dim.g, dim.b, 220), line2);

                double period_min = SatPeriodMinutes(p->sat);
                char line3[192];
                if (period_min > 0.0)
                    snprintf(line3, sizeof(line3),
                             "Period %.0f min   Longitude drifts slowly", period_min);
                else
                    snprintf(line3, sizeof(line3), "Longitude drifts slowly");
                dl->AddText(ImVec2(tx, ty + step * 2.0f),
                            IM_COL32(warn.r, warn.g, warn.b, 220), line3);
            }
            else
            {
                char aos_str[64];
                epoch_to_time_str(p->aos_epoch, aos_str);
                char line2[160];
                snprintf(line2, sizeof(line2), "AOS %s   Max El %.1f°", aos_str, p->max_el);
                dl->AddText(ImVec2(tx, ty + step), IM_COL32(dim.r, dim.g, dim.b, 220), line2);

                char eta_str[32];
                TransitEtaString(*ctx->current_epoch, p, eta_str, sizeof(eta_str));
                double span = p->los_epoch - p->aos_epoch;
                bool ongoing = (span > 0.0 && *ctx->current_epoch >= p->aos_epoch &&
                                *ctx->current_epoch <= p->los_epoch);
                double dur_s = span * 86400.0;
                int dur_m = (int)(dur_s / 60.0);
                int dur_sec = (int)(dur_s - dur_m * 60.0);
                char line3[160];
                snprintf(line3, sizeof(line3), "%s%s   %dm %02ds",
                         ongoing ? "T-" : "in ", eta_str, dur_m, dur_sec);
                dl->AddText(ImVec2(tx, ty + step * 2.0f), IM_COL32(warn.r, warn.g, warn.b, 220), line3);
            }

            dl->PopClipRect();

            /* right-click -> hand the pass off to Doppler (transit modal tab) */
            if (clicked_right)
            {
                TransitSelectIndex(i);
                if (p->sat) *ctx->selected_sat = p->sat;
                g_ui.locked_pass_sat = p->sat;
                g_ui.locked_pass_aos = p->aos_epoch;
                g_ui.locked_pass_los = p->los_epoch;
                TransitOpenDopplerTab(cfg);
            }

            ImGui::SetCursorScreenPos(ImVec2(row_start.x,
                                             row_start.y + row_h + ImGui::GetStyle().ItemSpacing.y));
        }

        /* record interaction so TransitEnsurePasses() can defer a recompute */
        s_list_interacting = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                             ImGui::IsMouseDown(ImGuiMouseButton_Left);

        /* drag-to-scroll: dragging on empty list space scrolls the list */
        if (ImGui::IsWindowHovered() &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemActive())
        {
            ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
        }

        ImGui::EndChild();
    }
    else
    {
        
        if (s_list_expanded)
            ImGui::TextDisabled("No passes in the current window.");
        s_list_interacting = false;
    }

    bool has_sel = (TransitSelectedPass() != NULL);

    /* ---- polar plot ---- */
    ImGui::Separator();
    float plot_avail_w = ImGui::GetContentRegionAvail().x;
    float plot_size = plot_avail_w;
    float min_plot = UIPx(120.0f);
    if (plot_size < min_plot)
        plot_size = min_plot;
    float indent = (plot_avail_w - plot_size) * 0.5f;
    if (indent > 0.0f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);

    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##transit_polar", ImVec2(plot_size, plot_size));
    ImVec2 center = ImVec2(canvas_pos.x + plot_size * 0.5f, canvas_pos.y + plot_size * 0.5f);
    float radius = plot_size * 0.5f - UIPx(18.0f);

    ImDrawList *pdl = ImGui::GetWindowDrawList();
    Color bg_theme = g_theme.ui.bg;
    pdl->AddCircleFilled(center, radius + UIPx(4.0f),
                         IM_COL32(bg_theme.r, bg_theme.g, bg_theme.b, 200), 64);
    PlotDrawPolarGrid(pdl, center, radius);

    if (has_sel)
        PlotDrawPolarTrack(pdl, center, radius, TransitSelectedPass(),
                           *ctx->current_epoch, 0.75f);

    /* dot follows the selected transit, not the global satellite selection */
    Satellite *sat = TransitSelectedSat();
    Location *home = GetHomeLocation();
    if (sat && sat->is_active && home)
    {
        double az = 0.0, el = 0.0;
        get_az_el(sat->current_pos, ctx->gmst_deg, home->lat, home->lon, home->alt,
                  &az, &el);
        PlotDrawSkyDot(pdl, center, radius, az, el);
    }

    if (!has_sel)
    {
        const char *msg = "No transit selected";
        ImVec2 ms = ImGui::CalcTextSize(msg);
        Color dim = g_theme.ui.text_dim;
        pdl->AddText(ImVec2(center.x - ms.x * 0.5f, center.y - ms.y * 0.5f),
                     IM_COL32(dim.r, dim.g, dim.b, 170), msg);
    }

    ImGui::PopTextWrapPos();
}

/* ========================================================================== */
/*  Overview modal                                                            */
/* ========================================================================== */

/** LEFT column: maximal-info transit list with inline selection/deselect */
static void DrawTransitOverviewList(UIContext *ctx, AppConfig *cfg)
{
    double now = *ctx->current_epoch;
    Location *home = GetHomeLocation();

    /* same filters as the panel, kept in sync through the shared persisted keys */
    TransitDrawFilterRow(cfg, "transit_ov", ImGui::GetContentRegionAvail().x);
    ImGui::Separator();

    if (num_passes <= 0)
    {
        ImGui::TextDisabled("No passes in the current window.");
        return;
    }

    for (int i = 0; i < num_passes; i++)
    {
        SatPass *p = &passes[i];
        bool deep = SatIsDeepSpace(p->sat);
        /* deep-space sats hidden unless the toggle is on */
        if (deep && !s_show_deep_space)
            continue;
        const char *name = p->sat ? p->sat->name : "Unknown";
        const char *norad = (p->sat && p->sat->norad_id[0]) ? p->sat->norad_id : "sat";

        bool selected = (i == g_ui.selected_pass_idx);
        ImGui::PushID(i);

        float lh = ImGui::GetTextLineHeight();
        float row_h = lh * 5.0f + UIPx(22.0f);
        ImVec2 row_start = ImGui::GetCursorScreenPos();

        /* stable id (no shifting epoch) so the active row survives a recompute */
        char row_id[64];
        snprintf(row_id, sizeof(row_id), "##ov_row_%s_%d", norad, i);
        if (ImGui::Selectable(row_id, selected, ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(0.0f, row_h)))
        {
            if (selected)
                TransitClearSelection();   /* keep the satellite selected on deselect */
            else
            {
                TransitSelectIndex(i);
                if (p->sat) *ctx->selected_sat = p->sat;
            }
        }

        ImDrawList *dl = ImGui::GetWindowDrawList();
        Color text = g_theme.ui.text;
        Color dim = g_theme.ui.text_dim;
        Color accent = g_theme.ui.accent;
        Color warn = g_theme.ui.warning;
        Color info = g_theme.ui.info;

        /* right content edge, recomputed at draw time (accounts for scrollbar) */
        float content_right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;

        /* hover-reveal: a short right-edge strip slides the row left to expose a skip button */
        float off = TransitRowReveal(ctx, i, p, selected, row_start, row_h, content_right, true);

        float shift = -off;
        float sx = row_start.x + shift;
        float tx = sx + UIPx(12.0f);
        float ty = row_start.y + UIPx(4.0f);
        float step = lh + UIPx(1.0f);

        dl->PushClipRect(ImVec2(row_start.x, row_start.y),
                         ImVec2(content_right, row_start.y + row_h), true);

        /* accent bar first so it can never paint over the first glyph */
        if (selected)
        {
            dl->AddRectFilled(ImVec2(sx, row_start.y),
                              ImVec2(sx + UIPx(3.0f), row_start.y + row_h),
                              IM_COL32(accent.r, accent.g, accent.b, 255));
        }

        /* blue right-edge strip marks a pass for tracking */
        if (TransitIsMarked(p))
            dl->AddRectFilled(ImVec2(content_right - UIPx(3.0f), row_start.y),
                              ImVec2(content_right, row_start.y + row_h),
                              IM_COL32(info.r, info.g, info.b, 255));

        char line1[128];
        snprintf(line1, sizeof(line1), "%s  #%s", name, norad);
        dl->AddText(ImVec2(tx, ty), IM_COL32(text.r, text.g, text.b, 235), line1);

        if (deep)
        {
            /* deep-space / GEO / HEO: no discrete AOS/LOS exists for these */
            char line2[160];
            snprintf(line2, sizeof(line2), "Deep-space / GEO / HEO   always in view");
            dl->AddText(ImVec2(tx, ty + step), IM_COL32(dim.r, dim.g, dim.b, 220), line2);

            double period_min = SatPeriodMinutes(p->sat);
            char line3[160];
            if (period_min > 0.0)
                snprintf(line3, sizeof(line3), "Period %.0f min   no AOS/LOS", period_min);
            else
                snprintf(line3, sizeof(line3), "no AOS/LOS");
            dl->AddText(ImVec2(tx, ty + step * 2.0f), IM_COL32(dim.r, dim.g, dim.b, 220), line3);

            char line4[192];
            snprintf(line4, sizeof(line4), "Longitude drifts slowly");
            dl->AddText(ImVec2(tx, ty + step * 3.0f), IM_COL32(warn.r, warn.g, warn.b, 220), line4);

            char line5[192];
            snprintf(line5, sizeof(line5), "Not a transiting pass");
            dl->AddText(ImVec2(tx, ty + step * 4.0f), IM_COL32(dim.r, dim.g, dim.b, 220), line5);
        }
        else
        {
            char aos_str[64], los_str[64];
            epoch_to_datetime_str(p->aos_epoch, aos_str);
            epoch_to_datetime_str(p->los_epoch, los_str);
            char line2[160];
            snprintf(line2, sizeof(line2), "AOS %s", aos_str);
            dl->AddText(ImVec2(tx, ty + step), IM_COL32(dim.r, dim.g, dim.b, 220), line2);
            char line3[160];
            snprintf(line3, sizeof(line3), "LOS %s", los_str);
            dl->AddText(ImVec2(tx, ty + step * 2.0f), IM_COL32(dim.r, dim.g, dim.b, 220), line3);

            double dur_s = (p->los_epoch - p->aos_epoch) * 86400.0;
            int dur_m = (int)(dur_s / 60.0);
            int dur_sec = (int)(dur_s - dur_m * 60.0);
            char eta_str[32];
            TransitEtaString(now, p, eta_str, sizeof(eta_str));
            char line4[192];
            snprintf(line4, sizeof(line4), "Max El %.1f°   %dm %02ds   in %s",
                     p->max_el, dur_m, dur_sec, eta_str);
            dl->AddText(ImVec2(tx, ty + step * 3.0f), IM_COL32(warn.r, warn.g, warn.b, 220), line4);

            double aos_az = (p->num_pts > 0) ? p->path_pts[0].x : 0.0;
            double peak_az = aos_az;
            int peak = 0;
            for (int k = 1; k < p->num_pts; k++)
                if (p->path_pts[k].y > p->path_pts[peak].y) peak = k;
            if (p->num_pts > 0) peak_az = p->path_pts[peak].x;
            char line5[192];
            snprintf(line5, sizeof(line5), "AOS Az %.1f°   Peak Az %.1f°", aos_az, peak_az);
            dl->AddText(ImVec2(tx, ty + step * 4.0f), IM_COL32(dim.r, dim.g, dim.b, 220), line5);
        }

        char line6[192];
        if (p->sat && home)
        {
            double cur_az = 0.0, cur_el = 0.0;
            get_az_el(p->sat->current_pos, ctx->gmst_deg, home->lat, home->lon, home->alt,
                      &cur_az, &cur_el);
            double rng = get_sat_range(p->sat, now, *home);
            if (cur_el >= 0.0)
                snprintf(line6, sizeof(line6), "Now El %.1f°  Az %.1f°  Rng %.0f km",
                         cur_el, cur_az, rng);
            else
                snprintf(line6, sizeof(line6), "Now below horizon   Rng %.0f km", rng);
        }
        else
        {
            snprintf(line6, sizeof(line6), "No live position");
        }
        // top-right, on the first-line baseline, clear of the object name
        // Draw only if it fits without colliding with line1
        ImVec2 now_sz = ImGui::CalcTextSize(line6);
        ImVec2 name_sz = ImGui::CalcTextSize(line1);
        float now_x = content_right - UIPx(6.0f) - now_sz.x + shift;
        if (now_x >= tx + name_sz.x + UIPx(12.0f))
            dl->AddText(ImVec2(now_x, ty), IM_COL32(dim.r, dim.g, dim.b, 200), line6);

        double span = p->los_epoch - p->aos_epoch;
        if (deep || (span > 0.0 && now >= p->aos_epoch && now <= p->los_epoch))
        {
            float progress = deep ? 1.0f : (float)((now - p->aos_epoch) / span);
            float bar_h = UIPx(4.0f);
            ImVec2 bar0 = ImVec2(tx, row_start.y + row_h - bar_h - UIPx(6.0f));
            float bar_right = content_right - UIPx(6.0f) + shift;
            if (bar_right < bar0.x + bar_h) bar_right = bar0.x + bar_h;
            ImVec2 bar1 = ImVec2(bar_right, bar0.y + bar_h);
            Color surface = g_theme.ui.surface;
            Color border = g_theme.ui.border;
            float round = UIPx(2.0f);
            dl->AddRectFilled(bar0, bar1, IM_COL32(surface.r, surface.g, surface.b, 220), round);
            if (progress > 0.0f)
            {
                ImVec2 fill = ImVec2(bar0.x + (bar1.x - bar0.x) * progress, bar1.y);
                dl->AddRectFilled(bar0, fill, IM_COL32(accent.r, accent.g, accent.b, 230), round);
            }
            dl->AddRect(bar0, bar1, IM_COL32(border.r, border.g, border.b, 200), round);
        }

        dl->PopClipRect();

        ImGui::SetCursorScreenPos(ImVec2(row_start.x,
                                         row_start.y + row_h + ImGui::GetStyle().ItemSpacing.y));

        ImGui::PopID();
    }
}

/** RIGHT column tab 1: large polar canvas */
static void DrawTransitPolarTab(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    bool has_sel = (TransitSelectedPass() != NULL);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float avail_w = avail.x;
    float size = (avail.x < avail.y) ? avail.x : avail.y;
    if (size < UIPx(120.0f)) size = UIPx(120.0f);

    float indent = (avail_w - size) * 0.5f;
    if (indent > 0.0f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);

    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##ov_polar", ImVec2(size, size));
    bool hovered = ImGui::IsItemHovered();
    ImVec2 center = ImVec2(canvas_pos.x + size * 0.5f, canvas_pos.y + size * 0.5f);
    float radius = size * 0.5f - UIPx(22.0f);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    Color bg = g_theme.ui.bg;
    dl->AddCircleFilled(center, radius + UIPx(4.0f), IM_COL32(bg.r, bg.g, bg.b, 200), 64);
    PlotDrawPolarGrid(dl, center, radius);

    if (has_sel)
        PlotDrawPolarTrack(dl, center, radius, TransitSelectedPass(),
                           *ctx->current_epoch);

    /* dot follows the selected transit, not the global satellite selection */
    Satellite *sat = TransitSelectedSat();
    Location *home = GetHomeLocation();
    if (sat && sat->is_active && home)
    {
        double az = 0.0, el = 0.0;
        get_az_el(sat->current_pos, ctx->gmst_deg, home->lat, home->lon, home->alt,
                  &az, &el);
        PlotDrawSkyDot(dl, center, radius, az, el);
    }

    /* hover: mark the trajectory point nearest the cursor */
    if (has_sel && hovered)
    {
        const SatPass *pass = TransitSelectedPass();
        ImVec2 mouse = ImGui::GetIO().MousePos;
        float dx = mouse.x - center.x;
        float dy = mouse.y - center.y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist <= radius && pass->num_pts >= 2)
        {
            bool found = false;
            float best_d2 = 1e18f;
            ImVec2 best_pt = ImVec2(0.0f, 0.0f);
            double best_az = 0.0, best_el = 0.0, best_epoch = 0.0;
            double span = pass->los_epoch - pass->aos_epoch;

            for (int k = 0; k + 1 < pass->num_pts; k++)
            {
                double a0 = pass->path_pts[k].x, e0 = pass->path_pts[k].y;
                double a1 = pass->path_pts[k + 1].x, e1 = pass->path_pts[k + 1].y;
                if (fabs(a1 - a0) > 180.0)
                    continue;   /* skip the north-seam break, same as the track */
                ImVec2 p0 = PlotPolarProject(center, radius, a0, e0);
                ImVec2 p1 = PlotPolarProject(center, radius, a1, e1);
                float sx = p1.x - p0.x, sy = p1.y - p0.y;
                float len2 = sx * sx + sy * sy;
                float t = 0.0f;
                if (len2 > 1e-6f)
                    t = ((mouse.x - p0.x) * sx + (mouse.y - p0.y) * sy) / len2;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                ImVec2 cp = ImVec2(p0.x + sx * t, p0.y + sy * t);
                float ddx = mouse.x - cp.x, ddy = mouse.y - cp.y;
                float d2 = ddx * ddx + ddy * ddy;
                if (d2 < best_d2)
                {
                    best_d2 = d2;
                    best_pt = cp;
                    best_az = a0 + (a1 - a0) * t;
                    best_el = e0 + (e1 - e0) * t;
                    best_epoch = pass->aos_epoch +
                                 (double)(k + t) / (double)(pass->num_pts - 1) * span;
                    found = true;
                }
            }

            if (found)
            {
                Color accent = g_theme.ui.accent;
                dl->AddCircleFilled(best_pt, UIPx(3.5f),
                                    IM_COL32(accent.r, accent.g, accent.b, 255), 12);
                char tm[64];
                epoch_to_time_str(best_epoch, tm);
                char buf[128];
                snprintf(buf, sizeof(buf), "%s   El %.1f°  Az %.1f°", tm, best_el, best_az);
                PlotDrawHoverLabel(dl, best_pt,
                                   ImVec2(canvas_pos.x + size, canvas_pos.y + size), buf);
            }
        }
    }

    if (!has_sel)
    {
        const char *msg = "Select a transit to plot its sky track";
        ImGui::SetWindowFontScale(1.3f);
        ImVec2 ms = ImGui::CalcTextSize(msg);
        Color dim = g_theme.ui.text_dim;
        dl->AddText(ImVec2(center.x - ms.x * 0.5f, center.y - ms.y * 0.5f),
                    IM_COL32(dim.r, dim.g, dim.b, 200), msg);
        ImGui::SetWindowFontScale(1.0f);
    }
}

/* ---- overview chart hover readouts --------------------------------------- */

typedef struct {
    double aos_epoch;
    double los_epoch;
    const char *unit;
    double base_freq_hz; /* Doppler: carrier frequency for the absolute readout */
    float  scale;        /* Doppler: Hz per chart unit */
    Satellite *sat;      /* Elev/Az: pass satellite for the slant range */
    Location *home;      /* Elev/Az: observer for the slant range */
} TransitChartHover;

static bool TransitDopplerHover(void *user, float x_frac, float y_a, float y_b,
                                char *buf, int buf_n)
{
    (void)y_b;
    TransitChartHover *c = (TransitChartHover *)user;
    char tm[64];
    epoch_to_time_str(c->aos_epoch + (double)x_frac * (c->los_epoch - c->aos_epoch), tm);
    double abs_hz = c->base_freq_hz + (double)y_a * c->scale;
    snprintf(buf, buf_n, "%s   offset %+.2f %s   %.6f MHz", tm, y_a, c->unit, abs_hz / 1e6);
    return true;
}

static bool TransitElevAzHover(void *user, float x_frac, float y_a, float y_b,
                               char *buf, int buf_n)
{
    TransitChartHover *c = (TransitChartHover *)user;
    double epoch = c->aos_epoch + (double)x_frac * (c->los_epoch - c->aos_epoch);
    char tm[64];
    epoch_to_time_str(epoch, tm);
    if (c->sat && c->home)
    {
        double rng = get_sat_range(c->sat, epoch, *c->home);
        snprintf(buf, buf_n, "%s   El %.1f°  Az %.1f°  Rng %.0f km", tm, y_a, y_b, rng);
    }
    else
    {
        snprintf(buf, buf_n, "%s   El %.1f°  Az %.1f°", tm, y_a, y_b);
    }
    return true;
}

/** RIGHT column tab 2: Doppler shift XY chart + frequency presets */
static void DrawTransitDopplerTab(UIContext *ctx, AppConfig *cfg)
{
    SatPass *pass = TransitSelectedPass();
    Location *home = GetHomeLocation();

    /* base frequency (MHz) persisted in transit.doppler_freq */
    float freq_mhz = ToolSettingGetFloat(cfg, "transit.doppler_freq", 145.8f);

    ImGui::TextDisabled("Base frequency (MHz)");
    ImGui::SetNextItemWidth(UIPx(140.0f));
    if (ImGui::InputFloat("##transit_freq", &freq_mhz, 0.1f, 1.0f, "%.3f"))
    {
        if (freq_mhz < 0.1f) freq_mhz = 0.1f;
        ToolSettingSetFloat(cfg, "transit.doppler_freq", freq_mhz);
    }
    ImGui::SameLine();
    for (int i = 0; i < 5; i++)
    {
        if (i > 0) ImGui::SameLine();
        if (ImGui::SmallButton(s_bands[i].label))
        {
            freq_mhz = (float)(s_bands[i].freq_hz / 1e6);
            ToolSettingSetFloat(cfg, "transit.doppler_freq", freq_mhz);
        }
    }

    if (!pass || !pass->sat)
    {
        ImGui::TextDisabled("Select a transit to compute its Doppler profile.");
        return;
    }
    if (!home)
    {
        ImGui::TextDisabled("No home location configured.");
        return;
    }

    float samples[TRANSIT_DOPPLER_SAMPLES];
    PlotSampleDoppler(pass->sat, pass->aos_epoch, pass->los_epoch,
                      (double)freq_mhz * 1e6, *home, samples, TRANSIT_DOPPLER_SAMPLES);

    float max_off = 0.0f;
    for (int i = 0; i < TRANSIT_DOPPLER_SAMPLES; i++)
        if (fabsf(samples[i]) > max_off) max_off = fabsf(samples[i]);
    if (max_off <= 0.0f) max_off = 1.0f;

    /* auto unit: Hz / kHz / MHz */
    const char *unit = "Hz";
    float scale = 1.0f;
    if (max_off >= 1e6f) { unit = "MHz"; scale = 1e6f; }
    else if (max_off >= 1e3f) { unit = "kHz"; scale = 1e3f; }

    float scaled[TRANSIT_DOPPLER_SAMPLES];
    for (int i = 0; i < TRANSIT_DOPPLER_SAMPLES; i++)
        scaled[i] = samples[i] / scale;

    float ymax = max_off / scale;
    char title[96];
    snprintf(title, sizeof(title), "Doppler offset  (base %.3f MHz)", freq_mhz);

    /* current-time marker: normalized position within the AOS..LOS window */
    float marker = -1.0f;
    double span = pass->los_epoch - pass->aos_epoch;
    if (span > 0.0)
    {
        double f = (*ctx->current_epoch - pass->aos_epoch) / span;
        if (f < 0.0) f = 0.0;
        if (f > 1.0) f = 1.0;
        marker = (float)f;
    }

    TransitChartHover hc;
    hc.aos_epoch = pass->aos_epoch;
    hc.los_epoch = pass->los_epoch;
    hc.unit = unit;
    hc.base_freq_hz = (double)freq_mhz * 1e6;
    hc.scale = scale;
    hc.sat = pass->sat;
    hc.home = home;

    ImGui::Spacing();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetContentRegionAvail().y - UIPx(8.0f);
    if (h < UIPx(140.0f)) h = UIPx(140.0f);
    ImGui::InvisibleButton("##ov_doppler_chart", ImVec2(w, h));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    PlotDrawXYChart(dl, p, ImVec2(p.x + w, p.y + h), scaled, TRANSIT_DOPPLER_SAMPLES,
                    -ymax, ymax, title, unit, marker, TransitDopplerHover, &hc);
}

/** RIGHT column tab 3: dual-series elevation & azimuth chart */
static void DrawTransitElevAzTab(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    (void)cfg;
    SatPass *pass = TransitSelectedPass();
    if (!pass)
    {
        ImGui::TextDisabled("Select a transit to plot its elevation and azimuth.");
        return;
    }
    Location *home = GetHomeLocation();

    float el[TRANSIT_DOPPLER_SAMPLES];
    float az[TRANSIT_DOPPLER_SAMPLES];
    PlotSampleElevAz(pass, el, az, TRANSIT_DOPPLER_SAMPLES);

    TransitChartHover hc;
    hc.aos_epoch = pass->aos_epoch;
    hc.los_epoch = pass->los_epoch;
    hc.unit = "deg";
    hc.base_freq_hz = 0.0;
    hc.scale = 1.0f;
    hc.sat = pass->sat;
    hc.home = home;

    ImGui::TextDisabled("Elevation & Azimuth over the pass");
    ImGui::Spacing();

    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetContentRegionAvail().y - UIPx(8.0f);
    if (h < UIPx(140.0f)) h = UIPx(140.0f);
    ImGui::InvisibleButton("##ov_elevaz_chart", ImVec2(w, h));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    PlotDrawXYChart(dl, p, ImVec2(p.x + w, p.y + h), el, az, TRANSIT_DOPPLER_SAMPLES,
                    0.0f, 360.0f, "Elevation & Azimuth", "deg", "El", "Az",
                    -1.0f, TransitElevAzHover, &hc);
}

/** RIGHT column: the three plot tabs */
static void DrawTransitOverviewTabs(UIContext *ctx, AppConfig *cfg, int *pending_tab)
{
    int active_tab = -1;
    if (ImGui::BeginTabBar("##transit_ov_tabs"))
    {
        if (ImGui::BeginTabItem("Polar Plot", NULL,
                (*pending_tab == 0) ? ImGuiTabItemFlags_SetSelected : 0))
        {
            active_tab = 0;
            DrawTransitPolarTab(ctx, cfg);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Doppler Shift", NULL,
                (*pending_tab == 1) ? ImGuiTabItemFlags_SetSelected : 0))
        {
            active_tab = 1;
            DrawTransitDopplerTab(ctx, cfg);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Elevation & Azimuth", NULL,
                (*pending_tab == 2) ? ImGuiTabItemFlags_SetSelected : 0))
        {
            active_tab = 2;
            DrawTransitElevAzTab(ctx, cfg);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Imager Simulation", NULL,
                (*pending_tab == 3) ? ImGuiTabItemFlags_SetSelected : 0))
        {
            active_tab = 3;
            DrawTransitSimulateScanTab(ctx, cfg);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    *pending_tab = -1;
    if (active_tab >= 0)
        ToolSettingSetInt(cfg, "transit.modal_tab", active_tab);
}

void TransitOpenDopplerTab(AppConfig *cfg)
{
    s_requested_tab = 1;                 /* "Doppler Shift" tab (0=Polar,1=Doppler,2=El/Az) */
    g_ui.transit_overview_open = true;
    ToolSettingSetInt(cfg, "transit.modal_tab", 1);
}

void DrawTransitOverviewModal(UIContext *ctx, AppConfig *cfg)
{
    static bool s_tab_restored = false;
    static int  s_pending_tab = -1;

    /* Keep BeginPopupModal reachable even when closed so an externally-cleared
       flag (global Escape router) actually closes the popup via p_open. */
    if (!g_ui.transit_overview_open)
    {
        s_tab_restored = false;
        SimScanCancel();
    }
    else
    {
        TransitEnsurePasses(ctx, cfg);
        ImGui::OpenPopup("Transit Overview");
    }

    ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImVec2 size = ImVec2(screen.x * 0.8f, screen.y * 0.8f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(400.0f, 300.0f),
                                        ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    bool *p_open = &g_ui.transit_overview_open;
    if (ImGui::BeginPopupModal("Transit Overview", p_open, ImGuiWindowFlags_NoCollapse))
    {
        if (!s_tab_restored)
        {
            s_pending_tab = (s_requested_tab >= 0)
                                ? s_requested_tab
                                : ToolSettingGetInt(cfg, "transit.modal_tab", 0);
            s_requested_tab = -1;
            s_tab_restored = true;
        }

        float avail_w = ImGui::GetContentRegionAvail().x;
        float footer_h = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        float body_h = ImGui::GetContentRegionAvail().y - footer_h;
        if (body_h < UIPx(120.0f)) body_h = UIPx(120.0f);

        float left_w = avail_w * 0.35f;
        float right_w = avail_w - left_w - ImGui::GetStyle().ItemSpacing.x;

        /* ---- LEFT: maximal-info transit list ---- */
        ImGui::BeginChild("##transit_ov_left", ImVec2(left_w, body_h), true);
        DrawTransitOverviewList(ctx, cfg);
        ImGui::EndChild();

        ImGui::SameLine();

        /* ---- RIGHT: three plot tabs ---- */
        ImGui::BeginChild("##transit_ov_right", ImVec2(right_w, body_h), true);
        DrawTransitOverviewTabs(ctx, cfg, &s_pending_tab);
        ImGui::EndChild();

        /* ---- footer ---- */
        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(UIPx(140.0f), 0)))
        {
            g_ui.transit_overview_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
