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
#include "io/rotator.h"
#include "ui/notifications.h"
#include "core/theme.h"
#include "core/location.h"
#include "ui/hints.h"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

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

// queue is the source of truth, these wrappers keep both surfaces in sync
static bool TransitIsMarked(const SatPass *p)
{
    if (!p) return false;
    uint32_t n = p->sat ? p->sat->norad_id_num : 0u;
    return RotatorQueueIsQueued(n, p->aos_epoch);
}

static void TransitToggleMark(const SatPass *p)
{
    if (!p) return;
    uint32_t n = p->sat ? p->sat->norad_id_num : 0u;
    const char *name = (p->sat && p->sat->name[0]) ? p->sat->name : "pass";
    // toggle returns false for both removal and refused add
    bool was_queued = RotatorQueueIsQueued(n, p->aos_epoch);
    bool now_queued = RotatorQueueToggle(n, p->aos_epoch, p->los_epoch, p->max_el);
    if (now_queued)
        NotifyPush(NOTIFY_SUCCESS, ICON_FA_CROSSHAIRS, "Queued %s for rotator tracking", name);
    else if (was_queued)
        NotifyPush(NOTIFY_INFO, ICON_FA_CROSSHAIRS, "Removed %s from rotator queue", name);
    else
        NotifyPush(NOTIFY_WARNING, ICON_FA_TRIANGLE_EXCLAMATION,
                   "Rotator queue is full (max %d)", MAX_ROTATOR_QUEUE);
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
    bool queued = TransitIsMarked(p);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                        ImGui::IsMouseHoveringRect(mark_min, mark_max) ? 1.0f : slide_a);
    if (queued)
    {
        Color info = g_theme.ui.info;
        ImGui::PushStyleColor(ImGuiCol_Button,
                              IM_COL32(info.r, info.g, info.b, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              IM_COL32(info.r, info.g, info.b, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              IM_COL32(info.r, info.g, info.b, 255));
    }
    ImGui::SetCursorScreenPos(mark_min);
    if (ImGui::Button(ICON_FA_CROSSHAIRS, ImVec2(btn_w, btn_h)))
        TransitToggleMark(p);
    if (queued)
        ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    ImGui::PopID();

    return off;
}

/* ========================================================================== */
/*  Scene hook: accent-colored line along the transit portion of the orbit    */
/* ========================================================================== */

// drawn before the pass early-returns so it works with no pass selected
static void DrawRotatorBeam3D(SceneContext *sctx, AppConfig *cfg)
{
    if (!sctx || sctx->is_2d_view)
        return;
    if (!ToolSettingGetBool(cfg, "rotator.beam_3d", false))
        return;
    if (!RotatorIsConnected() || !RotatorHasPosition())
        return;

    Location *home = GetHomeLocation();
    if (!home)
        return;

    float beam_w = ToolSettingGetFloat(cfg, "rotator.beam_width_deg", 10.0f);
    if (beam_w < 1.0f) beam_w = 1.0f;
    if (beam_w > 90.0f) beam_w = 90.0f;

    // same mapping main.cpp uses for the 3D home marker
    float lat = home->lat * DEG2RAD;
    float lon = (home->lon + (float)sctx->gmst_deg + sctx->earth_rotation_offset) * DEG2RAD;
    float r = sctx->draw_earth_radius;
    Vector3 home_pos = { cosf(lat) * cosf(lon) * r, sinf(lat) * r, -cosf(lat) * sinf(lon) * r };

    Vector3 up = Vector3Normalize(home_pos);
    Vector3 pole = { 0.0f, 1.0f, 0.0f };
    Vector3 east_c = Vector3CrossProduct(pole, up);
    // degenerate at a pole, where up is parallel to the pole axis
    Vector3 east = (Vector3LengthSqr(east_c) < 1e-8f)
                 ? (Vector3){ 1.0f, 0.0f, 0.0f }
                 : Vector3Normalize(east_c);
    Vector3 north = Vector3CrossProduct(up, east);

    // reported angles drive the beam, not the commanded target
    float az = RotatorGetAz() * DEG2RAD;
    float el = RotatorGetEl() * DEG2RAD;
    float ce = cosf(el), se = sinf(el);
    Vector3 dir = Vector3Add(Vector3Scale(up, se),
                  Vector3Scale(Vector3Add(Vector3Scale(east, sinf(az)),
                                          Vector3Scale(north, cosf(az))), ce));
    dir = Vector3Normalize(dir);

    // lift off a touch so the apex does not z-fight the globe
    Vector3 apex = Vector3Scale(home_pos, 1.001f);

    const float BEAM_LEN = 2.0f;   // draw units, visible at globe scale
    const int RING_SEGS = 24;
    const float half = beam_w * 0.5f * DEG2RAD;

    // orthonormal pair perpendicular to dir
    Vector3 ref = (fabsf(dir.y) < 0.9f) ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
    Vector3 perp = Vector3Normalize(Vector3CrossProduct(dir, ref));
    Vector3 other = Vector3CrossProduct(dir, perp);

    Vector3 ring[RING_SEGS];
    float ch = cosf(half), sh = sinf(half);
    for (int i = 0; i < RING_SEGS; i++)
    {
        float th = (2.0f * PI * (float)i) / (float)RING_SEGS;
        Vector3 rd = Vector3Add(Vector3Scale(dir, ch),
                     Vector3Add(Vector3Scale(perp, sh * cosf(th)),
                                Vector3Scale(other, sh * sinf(th))));
        rd = Vector3Normalize(rd);
        ring[i] = Vector3Add(apex, Vector3Scale(rd, BEAM_LEN));
    }

    Color col = g_theme.ui.accent;
    unsigned char a = 150;

    rlDrawRenderBatchActive();
    rlSetLineWidth(1.5f);
    rlBegin(RL_LINES);
    rlColor4ub(col.r, col.g, col.b, a);
    for (int i = 0; i < RING_SEGS; i++)
    {
        Vector3 p0 = ring[i];
        Vector3 p1 = ring[(i + 1) % RING_SEGS];
        rlVertex3f(p0.x, p0.y, p0.z);
        rlVertex3f(p1.x, p1.y, p1.z);
    }
    // four generatrices from apex to ring
    for (int k = 0; k < 4; k++)
    {
        Vector3 p = ring[(k * RING_SEGS) / 4];
        rlVertex3f(apex.x, apex.y, apex.z);
        rlVertex3f(p.x, p.y, p.z);
    }
    rlEnd();
    // flush while the line width is still set, then restore the default
    rlDrawRenderBatchActive();
    rlSetLineWidth(1.0f);
}

void DrawSceneTransit(SceneContext *sctx, AppConfig *cfg)
{
    DrawRotatorBeam3D(sctx, cfg);

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

    PlotDrawRotatorMarker(pdl, center, radius);

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

    PlotDrawRotatorMarker(dl, center, radius);

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

static SatPass *TransitRotatorFindPass(uint32_t norad, double aos)
{
    for (int i = 0; i < num_passes; i++)
    {
        if (passes[i].sat && passes[i].sat->norad_id_num == norad &&
            fabs(passes[i].aos_epoch - aos) < 1e-3)
            return &passes[i];
    }
    return NULL;
}

static void TransitRotatorSatName(uint32_t norad, char *out, size_t out_sz)
{
    for (int i = 0; i < num_passes; i++)
    {
        if (passes[i].sat && passes[i].sat->norad_id_num == norad)
        {
            snprintf(out, out_sz, "%s", passes[i].sat->name);
            return;
        }
    }
    snprintf(out, out_sz, "NORAD %u", (unsigned)norad);
}

static bool TransitRotatorPassAzEl(const SatPass *pass, double now, float *az_out, float *el_out)
{
    if (!pass || !pass->sat)
        return false;
    Location *home = GetHomeLocation();
    if (!home)
        return false;
    double t = now;
    if (t < pass->aos_epoch) t = pass->aos_epoch;
    if (t > pass->los_epoch) t = pass->los_epoch;
    double gmst = epoch_to_gmst(t);
    Vector3 pos = calculate_position(pass->sat, get_unix_from_epoch(t));
    double az = 0.0, el = 0.0;
    get_az_el(pos, gmst, home->lat, home->lon, home->alt, &az, &el);
    if (az_out) *az_out = (float)az;
    if (el_out) *el_out = (float)el;
    return true;
}

static void TransitRotatorCountdown(double secs, char *out, size_t out_sz)
{
    const char *sign = (secs < 0.0) ? "+" : "-";
    double s = fabs(secs);
    int h = (int)(s / 3600.0);
    int m = (int)((s - h * 3600.0) / 60.0);
    int sec = (int)(s - h * 3600.0 - m * 60.0);
    snprintf(out, out_sz, "T%s%02d:%02d:%02d", sign, h, m, sec);
}

static float TransitRotatorAzDelta(float target, float current)
{
    float d = fmodf(target - current, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}

static void TransitRotatorStateChip(RotatorQueueState s, const char **label, Color *col)
{
    *label = PlotRotatorStateLabel((int)s, true, false);
    *col = PlotRotatorStateColorC((int)s, true);
}

// every motion goes through RotatorGotoAzEl(), never raw SetPosition
static void TransitRotatorManualJog(bool conn, bool auto_driving,
                                    bool have_current, float cur_az, float cur_el,
                                    bool have_cmd, float tgt_az, float tgt_el)
{
    Color accent  = g_theme.ui.accent;
    Color warning = g_theme.ui.warning;
    float avail_w = ImGui::GetContentRegionAvail().x;

    if (auto_driving)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(
            IM_COL32(warning.r, warning.g, warning.b, 255)));
        ImGui::TextWrapped("%s The queue is driving the rotator.", ICON_FA_TRIANGLE_EXCLAMATION);
        ImGui::PopStyleColor();
        ImGui::TextDisabled("Stop the queue to take manual control.");
        ImGui::Spacing();
    }

    static float jog_az = 0.0f;
    static float jog_el = 0.0f;
    static int   jog_step_idx = 1; // 0=1deg, 1=5deg, 2=10deg
    static const float jog_steps[3] = { 1.0f, 5.0f, 10.0f };
    static const char *jog_step_labels[3] = { "1 deg", "5 deg", "10 deg" };
    static bool  jog_init = false;

    if (!jog_init)
    {
        if (have_current) { jog_az = cur_az; jog_el = cur_el; }
        else if (have_cmd) { jog_az = tgt_az; jog_el = tgt_el; }
        jog_init = true;
    }

    const float step = jog_steps[jog_step_idx];
    const bool jog_enabled = conn && !auto_driving;

    ImGui::TextDisabled("Step");
    ImGui::SameLine();
    for (int i = 0; i < 3; i++)
    {
        if (i > 0) ImGui::SameLine();
        bool sel = (jog_step_idx == i);
        if (sel)
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImGui::ColorConvertU32ToFloat4(
                    IM_COL32(accent.r, accent.g, accent.b, 200)));
        if (ImGui::Button(jog_step_labels[i], ImVec2(UIPx(52.0f), 0.0f)))
            jog_step_idx = i;
        if (sel) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Jog step size (degrees)");
    }

    ImGui::BeginDisabled(!jog_enabled);
    ImGui::SetNextItemWidth(avail_w);
    ImGui::InputFloat("##jog_az", &jog_az, 0.0f, 0.0f, "Az %.1f°");
    ImGui::SetNextItemWidth(avail_w);
    ImGui::InputFloat("##jog_el", &jog_el, 0.0f, 0.0f, "El %.1f°");

    {
        float bw = UIPx(46.0f);
        float gap = ImGui::GetStyle().ItemSpacing.x;
        float pad_w = bw * 3.0f + gap * 2.0f;
        float pad_off = (avail_w - pad_w) * 0.5f;
        if (pad_off < 0.0f) pad_off = 0.0f;

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_off + bw + gap);
        if (ImGui::Button(ICON_FA_ARROW_UP "##jogN", ImVec2(bw, 0.0f))) jog_el += step;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Jog Up / North (el +%.0f deg)", step);

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_off);
        if (ImGui::Button(ICON_FA_ARROW_LEFT "##jogW", ImVec2(bw, 0.0f))) jog_az -= step;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Jog West (az -%.0f deg)", step);

        ImGui::SameLine();
        ImGui::Dummy(ImVec2(bw, 0.0f));

        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ARROW_RIGHT "##jogE", ImVec2(bw, 0.0f))) jog_az += step;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Jog East (az +%.0f deg)", step);

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_off + bw + gap);
        if (ImGui::Button(ICON_FA_ARROW_DOWN "##jogS", ImVec2(bw, 0.0f))) jog_el -= step;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Jog Down / South (el -%.0f deg)", step);
    }

    if (jog_az < 0.0f) jog_az += 360.0f;
    if (jog_az >= 360.0f) jog_az -= 360.0f;
    if (jog_el < -90.0f) jog_el = -90.0f;
    if (jog_el > 90.0f) jog_el = 90.0f;

    ImGui::Spacing();
    if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS " Goto Az/El", ImVec2(avail_w, 0.0f)))
        RotatorGotoAzEl(jog_az, jog_el);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Slew to the entered target. Passes through all safety limits.");

    if (!conn && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Connect rotator first");
    else if (auto_driving && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Stop the queue to jog");

    ImGui::BeginDisabled(!conn);
    if (ImGui::Button("Load current heading", ImVec2(avail_w, 0.0f)))
    {
        if (have_current) { jog_az = cur_az; jog_el = cur_el; }
    }
    ImGui::EndDisabled();
    if (!conn && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Connect rotator first");
    ImGui::EndDisabled();
}

static void DrawTransitRotatorConfig(UIContext *ctx, AppConfig *cfg);

// beam settings live in the tool KV store, the 3D scene reads the same keys
static void DrawRotatorBeamControls(AppConfig *cfg)
{
    bool beam_on = ToolSettingGetBool(cfg, "rotator.beam_3d", false);
    float beam_w = ToolSettingGetFloat(cfg, "rotator.beam_width_deg", 10.0f);
    if (beam_w < 1.0f) beam_w = 1.0f;
    if (beam_w > 90.0f) beam_w = 90.0f;

    if (ImGui::Checkbox("Draw beam in 3D Mode", &beam_on))
        ToolSettingSetBool(cfg, "rotator.beam_3d", beam_on);

    if (beam_on)
    {
        ImGui::Indent();
        ImGui::SetNextItemWidth(UIPx(110.0f));
        if (ImGui::InputFloat("Beam width (deg)", &beam_w, 1.0f, 5.0f, "%.1f"))
        {
            if (beam_w < 1.0f) beam_w = 1.0f;
            if (beam_w > 90.0f) beam_w = 90.0f;
            ToolSettingSetFloat(cfg, "rotator.beam_width_deg", beam_w);
        }
        ImGui::Unindent();
    }
}

static void DrawTransitRotatorTab(UIContext *ctx, AppConfig *cfg)
{
    double now = *ctx->current_epoch;

    bool conn = RotatorIsConnected();
    bool comm_lost = RotatorCommLost();
    bool has_pos = RotatorHasPosition();
    bool running = RotatorQueueIsRunning();
    float cur_az = RotatorGetAz();
    float cur_el = RotatorGetEl();

    const bool auto_driving = conn && (running || RotatorGetAutoSteer());

    int qcount = RotatorQueueCount();
    const RotatorQueueItem *items = RotatorQueueItems();
    int active_idx = RotatorQueueActiveIndex();
    const RotatorQueueItem *active_item =
        (items && active_idx >= 0 && active_idx < qcount) ? &items[active_idx] : NULL;

    SatPass *target_pass = NULL;
    if (active_item)
        target_pass = TransitRotatorFindPass(active_item->norad, active_item->aos);
    if (!target_pass)
        target_pass = TransitSelectedPass();

    float tgt_az = 0.0f, tgt_el = 0.0f;
    bool have_target = false;
    if (target_pass && TransitRotatorPassAzEl(target_pass, now, &tgt_az, &tgt_el))
        have_target = true;
    else if (RotatorGetCommandedTarget(&tgt_az, &tgt_el))
        have_target = true;

    bool have_current = conn && has_pos;

    float d_az = 0.0f, d_el = 0.0f;
    bool have_err = have_target && have_current;
    if (have_err)
    {
        d_az = TransitRotatorAzDelta(tgt_az, cur_az);
        d_el = tgt_el - cur_el;
    }

    char act_name[96] = "";
    if (active_item)
        TransitRotatorSatName(active_item->norad, act_name, sizeof(act_name));

    int b_state = PLOT_ROT_STATE_NONE;
    bool b_slewing = false;
    if (comm_lost)
        b_state = PLOT_ROT_STATE_COMM_LOST;
    else if (active_item)
    {
        b_state = (int)active_item->state;
        b_slewing = (active_item->state == RQ_ACTIVE && !active_item->arrived);
    }
    const bool b_have_pos = conn && has_pos;
    const char *b_word = PlotRotatorStateLabel(b_state, b_have_pos, b_slewing);
    Color b_col = PlotRotatorStateColorC(b_state, b_have_pos);

    char b_title[192];
    char b_detail[224];
    char azel[64];
    if (!conn)
    {
        snprintf(b_title, sizeof(b_title), "%s  %s", ICON_FA_PLUG, b_word);
        snprintf(b_detail, sizeof(b_detail), "Connect the rotator to start pointing. Target preview only.");
    }
    else if (comm_lost)
    {
        snprintf(b_title, sizeof(b_title), "%s  %s - retrying", ICON_FA_TRIANGLE_EXCLAMATION, b_word);
        snprintf(b_detail, sizeof(b_detail), "No position update from the rotator. Holding last command.");
    }
    else if (!has_pos)
    {
        snprintf(b_title, sizeof(b_title), "%s  Connected - waiting for position", ICON_FA_SATELLITE_DISH);
        snprintf(b_detail, sizeof(b_detail), "Link up, but no heading reported yet.");
    }
    else if (!running)
    {
        snprintf(b_title, sizeof(b_title), "%s  Connected - Idle (queue stopped)", ICON_FA_CIRCLE_CHECK);
        snprintf(b_detail, sizeof(b_detail), "%d pass%s queued. Press Start to drive the queue.",
                 qcount, qcount == 1 ? "" : "es");
    }
    else if (active_item)
    {
        if (active_item->state == RQ_LOS_HOLD)
        {
            double hold_left = RotatorGetLosHoldRemaining(now);
            if (hold_left < 0.0) hold_left = 0.0;
            snprintf(b_title, sizeof(b_title), "%s  %s (%.0f s left)",
                     ICON_FA_SATELLITE_DISH, b_word, hold_left);
            PlotFormatAzEl(azel, sizeof(azel), (double)active_item->los_az,
                           (double)active_item->los_el);
            snprintf(b_detail, sizeof(b_detail), "Holding %s", azel);
        }
        else if (active_item->state == RQ_AWAITING)
        {
            char cd[32];
            TransitRotatorCountdown((active_item->aos - now) * 86400.0, cd, sizeof(cd));
            snprintf(b_title, sizeof(b_title), "%s  %s (%s)", ICON_FA_CLOCK, b_word, cd);
            snprintf(b_detail, sizeof(b_detail), "Next up: %s  #%u", act_name, (unsigned)active_item->norad);
        }
        else if (active_item->state == RQ_AWAITING_AOS)
        {
            char cd[32];
            TransitRotatorCountdown((active_item->aos - now) * 86400.0, cd, sizeof(cd));
            snprintf(b_title, sizeof(b_title), "%s  %s (%s)", ICON_FA_SATELLITE_DISH, b_word, cd);
            if (have_err)
            {
                PlotFormatAzElDelta(azel, sizeof(azel), d_az, d_el);
                snprintf(b_detail, sizeof(b_detail), "On target. %s", azel);
            }
            else
                snprintf(b_detail, sizeof(b_detail), "On target.");
        }
        else if (!active_item->arrived)
        {
            snprintf(b_title, sizeof(b_title), "%s  %s to target", ICON_FA_ROTATE, b_word);
            if (have_err)
            {
                PlotFormatAzElDelta(azel, sizeof(azel), d_az, d_el);
                snprintf(b_detail, sizeof(b_detail), "%s", azel);
            }
            else
                snprintf(b_detail, sizeof(b_detail), "Next up: %s  #%u", act_name, (unsigned)active_item->norad);
        }
        else
        {
            snprintf(b_title, sizeof(b_title), "%s  %s %s", ICON_FA_SATELLITE_DISH, b_word, act_name);
            if (have_err)
            {
                PlotFormatAzElDelta(azel, sizeof(azel), d_az, d_el);
                snprintf(b_detail, sizeof(b_detail), "On target. %s", azel);
            }
            else
                snprintf(b_detail, sizeof(b_detail), "On target.");
        }
    }
    else
    {
        const RotatorQueueItem *nextp = NULL;
        for (int i = 0; i < qcount; i++)
        {
            if (items[i].armed && items[i].state == RQ_PENDING) { nextp = &items[i]; break; }
        }
        if (nextp)
        {
            char cd[32];
            TransitRotatorCountdown((nextp->aos - now) * 86400.0, cd, sizeof(cd));
            char nm[96];
            TransitRotatorSatName(nextp->norad, nm, sizeof(nm));
            snprintf(b_title, sizeof(b_title), "%s  %s (%s)", ICON_FA_CLOCK, b_word, cd);
            snprintf(b_detail, sizeof(b_detail), "Next up: %s  #%u", nm, (unsigned)nextp->norad);
        }
        else
        {
            snprintf(b_title, sizeof(b_title), "%s  Running - nothing queued", ICON_FA_CIRCLE_CHECK);
            snprintf(b_detail, sizeof(b_detail), "Mark a pass with the crosshair button to add it here.");
        }
    }
    auto draw_live = [&]() {
    ImGui::Dummy(ImVec2(0.0f, UIPx(4.0f)));
    ToolsDrawBanner(b_title, b_detail, b_col);

    ImVec2 lav = ImGui::GetContentRegionAvail();
    const bool two_up = lav.x >= UIPx(380.0f);
    float plot = two_up ? lav.x * 0.55f : lav.x;
    float plot_h = lav.y - UIPx(64.0f);
    if (plot > plot_h) plot = plot_h;
    if (plot < UIPx(140.0f)) plot = UIPx(140.0f);
    if (plot > UIPx(340.0f)) plot = UIPx(340.0f);
    {
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##rot_polar", ImVec2(plot, plot));
        ImVec2 center = ImVec2(canvas_pos.x + plot * 0.5f, canvas_pos.y + plot * 0.5f);
        float radius = plot * 0.5f - UIPx(18.0f);

        ImDrawList *dl = ImGui::GetWindowDrawList();
        Color bg = g_theme.ui.bg;
        Color warn = g_theme.ui.warning;

        dl->AddCircleFilled(center, radius + UIPx(4.0f),
                            IM_COL32(bg.r, bg.g, bg.b, 200), 64);
        PlotDrawPolarGrid(dl, center, radius);

        if (target_pass)
            PlotDrawPolarTrack(dl, center, radius, target_pass, now, 0.85f);

        if (have_target && have_err)
        {
            ImVec2 cp = PlotPolarProject(center, radius, cur_az, cur_el);
            ImVec2 tp = PlotPolarProject(center, radius, tgt_az, tgt_el);
            dl->AddLine(cp, tp, IM_COL32(warn.r, warn.g, warn.b, 220), UIPx(2.0f));
        }

        PlotDrawRotatorMarker(dl, center, radius);

        const char *msg = NULL;
        if (!conn)
            msg = "Not connected";
        else if (!has_pos)
            msg = "No position fix";
        else if (!have_target)
            msg = "No target selected";

        if (msg)
        {
            ImGui::SetWindowFontScale(1.05f);
            ImVec2 ms = ImGui::CalcTextSize(msg);
            dl->AddText(ImVec2(center.x - ms.x * 0.5f, center.y - ms.y * 0.5f),
                        IM_COL32(warn.r, warn.g, warn.b, 230), msg);
            ImGui::SetWindowFontScale(1.0f);
        }

        if (two_up) ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextDisabled("Azimuth / Elevation");

        const ImGuiTableFlags tf = ImGuiTableFlags_BordersInnerV |
                                   ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable("##rot_readout", 3, tf))
        {
            ImGui::TableSetupColumn("item");
            ImGui::TableSetupColumn("az");
            ImGui::TableSetupColumn("el");
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Target");
            ImGui::TableSetColumnIndex(1);
            if (have_target) ImGui::Text("%.1f°", tgt_az); else ImGui::TextDisabled("%s", "-");
            ImGui::TableSetColumnIndex(2);
            if (have_target) ImGui::Text("%.1f°", tgt_el); else ImGui::TextDisabled("%s", "-");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Current");
            ImGui::TableSetColumnIndex(1);
            if (have_current) ImGui::Text("%.1f°", cur_az); else ImGui::TextDisabled("%s", "-");
            ImGui::TableSetColumnIndex(2);
            if (have_current) ImGui::Text("%.1f°", cur_el); else ImGui::TextDisabled("%s", "-");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Delta");
            ImVec4 dcol = (have_err) ? ImGui::ColorConvertU32ToFloat4(
                              IM_COL32(warn.r, warn.g, warn.b, 255))
                          : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            ImGui::PushStyleColor(ImGuiCol_Text, dcol);
            ImGui::TableSetColumnIndex(1);
            if (have_err) ImGui::Text("%+.1f°", d_az); else ImGui::TextDisabled("%s", "-");
            ImGui::TableSetColumnIndex(2);
            if (have_err) ImGui::Text("%+.1f°", d_el); else ImGui::TextDisabled("%s", "-");
            ImGui::PopStyleColor();
            ImGui::EndTable();
        }

        if (!conn)
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(
                IM_COL32(g_theme.ui.error.r, g_theme.ui.error.g, g_theme.ui.error.b, 255)),
                "%s Disconnected", ICON_FA_PLUG);
        else
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(
                IM_COL32(g_theme.ui.success.r, g_theme.ui.success.g, g_theme.ui.success.b, 255)),
                "%s Connected - %s", ICON_FA_PLUG,
                has_pos ? RotatorGetStatusText() : "no position fix");
        ImGui::EndGroup();
    }
    };

    auto draw_settings = [&]() {
    {
        ImGui::Spacing();
        ImGui::Text("%s  Rotator", ICON_FA_SATELLITE_DISH);
        ImGui::SameLine();

        if (!conn)
        {
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImGui::ColorConvertU32ToFloat4(IM_COL32(
                    g_theme.ui.accent.r, g_theme.ui.accent.g, g_theme.ui.accent.b, 200)));
            if (ImGui::Button(ICON_FA_PLUG "  Connect"))
                RotatorConnect();
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Open the stored link to the rotator");
        }
        else
        {
            if (ImGui::Button(ICON_FA_POWER_OFF "  Disconnect"))
                RotatorDisconnect();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Close the rotator link (motion stops)");
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!conn);
        if (ImGui::Button(ICON_FA_HOUSE "  Park"))
            RotatorParkNow();
        ImGui::EndDisabled();
        if (!conn && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Connect rotator first");
        else if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Stop motion and send the rotator to its home/park position");
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::CollapsingHeader(ICON_FA_SLIDERS "  Configuration"))
    {
        DrawTransitRotatorConfig(ctx, cfg);
    }

    if (ImGui::CollapsingHeader(ICON_FA_GAMEPAD "  Manual jog"))
    {
        TransitRotatorManualJog(conn, auto_driving, have_current, cur_az, cur_el,
                                have_target, tgt_az, tgt_el);
    }

    if (ImGui::CollapsingHeader(ICON_FA_SATELLITE_DISH "  Beam"))
        DrawRotatorBeamControls(cfg);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("%s  Queue  (%d)", ICON_FA_LIST, qcount);
    ImGui::SameLine();

    {
        ImGui::BeginDisabled(!conn);
        if (running)
        {
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImGui::ColorConvertU32ToFloat4(IM_COL32(
                    g_theme.ui.warning.r, g_theme.ui.warning.g, g_theme.ui.warning.b, 200)));
            if (ImGui::Button(ICON_FA_STOP "  Stop", ImVec2(UIPx(96.0f), 0.0f)))
            {
                RotatorQueueStop();
                RotatorSetAutoSteer(false);
            }
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImGui::ColorConvertU32ToFloat4(IM_COL32(
                    g_theme.ui.accent.r, g_theme.ui.accent.g, g_theme.ui.accent.b, 200)));
            if (ImGui::Button(ICON_FA_PLAY "  Start", ImVec2(UIPx(96.0f), 0.0f)))
            {
                RotatorQueueStart();
                if (!RotatorGetAutoSteer())
                    RotatorSetAutoSteer(true);
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    {
        ImGui::BeginDisabled(qcount == 0);
        if (ImGui::Button(ICON_FA_TRASH_CAN "  Clear"))
            RotatorQueueClear();
        ImGui::EndDisabled();
    }

    ImVec2 qav = ImGui::GetContentRegionAvail();
    float qh = qav.y - UIPx(4.0f);
    if (qh < UIPx(60.0f)) qh = UIPx(60.0f);
    ImGui::BeginChild("##rot_queue", ImVec2(0.0f, qh), true);

    if (qcount <= 0)
    {
        ImGui::TextDisabled("No passes queued.");
    }
    else
    {
        int next_pending = -1;
        for (int i = 0; i < qcount; i++)
            if (items[i].armed && items[i].state == RQ_PENDING) { next_pending = i; break; }

        const ImGuiTableFlags qtf = ImGuiTableFlags_BordersInnerV |
                                    ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("##rot_queue_tbl", 5, qtf))
        {
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("AOS", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Max El", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("##act", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();

            for (int i = 0; i < qcount; i++)
            {
                const RotatorQueueItem *it = &items[i];
                ImGui::TableNextRow();

                bool is_active = (i == active_idx);
                bool is_next = (i == next_pending);
                if (is_active)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                        IM_COL32(g_theme.ui.accent.r, g_theme.ui.accent.g,
                                 g_theme.ui.accent.b, 55));

                const char *chip = NULL;
                Color ccol;
                TransitRotatorStateChip(it->state, &chip, &ccol);
                ImU32 cui = IM_COL32(ccol.r, ccol.g, ccol.b, 255);

                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(cui), "%s", chip);

                ImGui::TableSetColumnIndex(1);
                char nm[96];
                TransitRotatorSatName(it->norad, nm, sizeof(nm));
                if (is_active)
                {
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(cui),
                                       "%s  #%u", nm, (unsigned)it->norad);
                    ImGui::SameLine();
                    ImGui::TextDisabled("(now)");
                }
                else if (is_next)
                {
                    ImGui::Text("%s  #%u", nm, (unsigned)it->norad);
                    ImGui::SameLine();
                    ImGui::TextDisabled("(next up)");
                }
                else
                {
                    ImGui::Text("%s  #%u", nm, (unsigned)it->norad);
                }

                ImGui::TableSetColumnIndex(2);
                char aos_str[64];
                epoch_to_datetime_str(it->aos, aos_str);
                ImGui::TextUnformatted(aos_str);

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.1f", it->max_el);

                ImGui::TableSetColumnIndex(4);
                ImGui::PushID(i);
                if (ImGui::SmallButton(ICON_FA_TRASH_CAN))
                {
                    RotatorQueueToggle(it->norad, it->aos, it->los, it->max_el);
                    ImGui::PopID();
                    ImGui::EndTable();
                    ImGui::EndChild();
                    return; // list changed, bail out of this frame
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    };

    float avail_w = ImGui::GetContentRegionAvail().x;
    if (avail_w >= UIPx(560.0f))
    {
        float left_w = avail_w * 0.42f;
        if (left_w < UIPx(240.0f)) left_w = UIPx(240.0f);
        float right_w = avail_w - left_w - ImGui::GetStyle().ItemSpacing.x;
        if (right_w < UIPx(200.0f)) right_w = UIPx(200.0f);

        ImGui::BeginChild("##rot_left", ImVec2(left_w, 0.0f), false);
        draw_settings();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##rot_right", ImVec2(right_w, 0.0f), false);
        draw_live();
        ImGui::EndChild();
    }
    else
    {
        draw_live();
        ImGui::Separator();
        draw_settings();
    }
}

// limits apply now, the link on next Connect
static void RotatorConfigSave(AppConfig *cfg)
{
    RotatorLoadSettings(cfg);
    SaveAppConfig("settings.json", cfg);
}

static void RotatorConfigField(const char *label, char *buf, size_t buf_sz,
                               float width, const char *tip)
{
    ImGui::SetNextItemWidth(width);
    ImGui::InputText(label, buf, buf_sz);
    if (tip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip);
}

static void DrawTransitRotatorConfig(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    RotatorSettings *R = &cfg->rotator_settings;

    ImGui::TextDisabled("Connection");

    static char proto_keys[256];
    if (proto_keys[0] == '\0')
    {
        int n = RotatorProtocolCount();
        size_t off = 0;
        for (int i = 0; i < n && off + 32 < sizeof(proto_keys); i++)
        {
            const char *k = RotatorProtocolKey(i);
            if (!k) continue;
            size_t len = strlen(k);
            memcpy(proto_keys + off, k, len);
            off += len;
            proto_keys[off++] = '\0';
        }
        proto_keys[off] = '\0';
    }

    int proto_idx = 0;
    bool proto_known = false;
    int pcount = RotatorProtocolCount();
    for (int i = 0; i < pcount; i++)
    {
        const char *k = RotatorProtocolKey(i);
        if (k && strcmp(k, R->protocol) == 0) { proto_idx = i; proto_known = true; break; }
    }
    if (!proto_known)
    {
        strncpy(R->protocol, "rotctld", sizeof(R->protocol) - 1);
        R->protocol[sizeof(R->protocol) - 1] = '\0';
        proto_idx = 0;
    }

    ImGui::SetNextItemWidth(UIPx(230.0f));
    if (ImGui::Combo("Protocol##rot", &proto_idx, proto_keys))
    {
        const char *k = RotatorProtocolKey(proto_idx);
        if (k) { strncpy(R->protocol, k, sizeof(R->protocol) - 1); R->protocol[sizeof(R->protocol) - 1] = '\0'; }
        RotatorConfigSave(cfg);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How TLEscope talks to the rotator.\n"
                          "rotctld / generic: Hamlib rotctld (default; works with most rigs).\n"
                          "GS-232, EasyComm II: serial-native command sets sent over TCP.\n"
                          "Custom: your own get/set format strings (advanced).");

    ImGui::SetNextItemWidth(UIPx(230.0f));
    ImGui::InputText("Host##rot", R->host, sizeof(R->host));
    if (ImGui::IsItemEdited()) RotatorLoadSettings(cfg);
    if (ImGui::IsItemDeactivatedAfterEdit()) SaveAppConfig("settings.json", cfg);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UIPx(80.0f));
    ImGui::InputText("Port##rot", R->port, sizeof(R->port));
    if (ImGui::IsItemEdited()) RotatorLoadSettings(cfg);
    if (ImGui::IsItemDeactivatedAfterEdit()) SaveAppConfig("settings.json", cfg);
    ImGui::TextDisabled("Connection fields apply on the next Connect.");

    bool show_fmt = (strcmp(R->protocol, "rotctld") == 0 || strcmp(R->protocol, "custom") == 0);
    if (show_fmt)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("Command formats (advanced)");
        ImGui::SetNextItemWidth(UIPx(230.0f));
        ImGui::InputText("Get format##rot", R->get_fmt, sizeof(R->get_fmt));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Query sent to read the position (rotctld default: \"p\").");
        if (ImGui::IsItemEdited()) RotatorLoadSettings(cfg);
        if (ImGui::IsItemDeactivatedAfterEdit()) SaveAppConfig("settings.json", cfg);

        ImGui::SetNextItemWidth(UIPx(230.0f));
        ImGui::InputText("Set format##rot", R->set_fmt, sizeof(R->set_fmt));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Slew command template. Two %%f placeholders, az then el\n"
                              "(rotctld default: \"P %%.1f %%.1f\").");
        if (ImGui::IsItemEdited()) RotatorLoadSettings(cfg);
        if (ImGui::IsItemDeactivatedAfterEdit()) SaveAppConfig("settings.json", cfg);

        ImGui::SetNextItemWidth(UIPx(230.0f));
        ImGui::InputText("Custom command##rot", R->custom_cmd, sizeof(R->custom_cmd));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Optional raw command fired by the Custom button.");
        if (ImGui::IsItemEdited()) RotatorLoadSettings(cfg);
        if (ImGui::IsItemDeactivatedAfterEdit()) SaveAppConfig("settings.json", cfg);
    }
    else
    {
        ImGui::TextDisabled("Command formats are fixed by the selected protocol.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Limits & Safety");
    ImGui::TextWrapped("Hard limits applied to EVERY commanded move, before it is sent.");
    if (RotatorIsConnected())
        ImGui::TextDisabled("Rotator is connected; limits apply immediately.");
    else
        ImGui::TextDisabled("Not connected. Safety limits apply immediately once connected.");

    RotatorConfigField("Min elevation##rot", R->min_el_track, sizeof(R->min_el_track), UIPx(90.0f),
                       "Never command below this elevation (deg) - protects low-angle rigs.");
    if (ImGui::IsItemEdited()) RotatorLoadSettings(cfg);
    if (ImGui::IsItemDeactivatedAfterEdit()) SaveAppConfig("settings.json", cfg);
    ImGui::SameLine();
    RotatorConfigField("Max elevation##rot", R->max_el_track, sizeof(R->max_el_track), UIPx(90.0f),
                       "Never command above this elevation (deg) - avoids zenith keyholes.");
    if (ImGui::IsItemEdited()) RotatorLoadSettings(cfg);
    if (ImGui::IsItemDeactivatedAfterEdit()) SaveAppConfig("settings.json", cfg);

    RotatorConfigField("Az min##rot", R->az_min, sizeof(R->az_min), UIPx(90.0f),
                       "Lower azimuth travel limit (deg). Leave 0/360 for full rotation.");
    if (ImGui::IsItemEdited()) RotatorLoadSettings(cfg);
    if (ImGui::IsItemDeactivatedAfterEdit()) SaveAppConfig("settings.json", cfg);
    ImGui::SameLine();
    RotatorConfigField("Az max##rot", R->az_max, sizeof(R->az_max), UIPx(90.0f),
                       "Upper azimuth travel limit (deg). Only enforced when the span is less\n"
                       "than the full 360 degrees.");
    if (ImGui::IsItemEdited()) RotatorLoadSettings(cfg);
    if (ImGui::IsItemDeactivatedAfterEdit()) SaveAppConfig("settings.json", cfg);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Motion");
    ImGui::TextDisabled("Targets are sent at full value (no smoothing).");
    ImGui::TextDisabled("Only the send cadence is paced; the rotator ramps itself.");

    RotatorConfigField("Slew tolerance##rot", R->slew_tol_deg, sizeof(R->slew_tol_deg), UIPx(90.0f),
                       "Considered \"on target\" (deg) when within this of the commanded angle.");
    if (ImGui::IsItemEdited()) RotatorLoadSettings(cfg);
    if (ImGui::IsItemDeactivatedAfterEdit()) SaveAppConfig("settings.json", cfg);

    RotatorConfigField("Slew timeout##rot", R->slew_timeout_s, sizeof(R->slew_timeout_s), UIPx(90.0f),
                       "Stop steering a pass that is not on target within this many seconds.");
    if (ImGui::IsItemEdited()) RotatorLoadSettings(cfg);
    if (ImGui::IsItemDeactivatedAfterEdit()) SaveAppConfig("settings.json", cfg);

    RotatorConfigField("Comm timeout##rot", R->comm_timeout_s, sizeof(R->comm_timeout_s), UIPx(90.0f),
                       "Treat the link as lost if no position update arrives within this many seconds.");
    if (ImGui::IsItemEdited()) RotatorLoadSettings(cfg);
    if (ImGui::IsItemDeactivatedAfterEdit()) SaveAppConfig("settings.json", cfg);

    RotatorConfigField("Send interval (ms)##rot", R->send_interval_ms, sizeof(R->send_interval_ms), UIPx(90.0f),
                       "Minimum gap between transmitted position commands (ms).\n"
                       "Paces link traffic only - the target value itself is unchanged\n"
                       "(there is no software slew-rate limiting). 150 = ~6.7 Hz.");
    if (ImGui::IsItemEdited()) RotatorLoadSettings(cfg);
    if (ImGui::IsItemDeactivatedAfterEdit()) SaveAppConfig("settings.json", cfg);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Park");
    RotatorConfigField("Park AZ##rot", R->park_az, sizeof(R->park_az), UIPx(90.0f),
                       "Azimuth to slew to when parking (degrees).");
    if (ImGui::IsItemEdited()) RotatorLoadSettings(cfg);
    if (ImGui::IsItemDeactivatedAfterEdit()) SaveAppConfig("settings.json", cfg);
    ImGui::SameLine();
    RotatorConfigField("Park EL##rot", R->park_el, sizeof(R->park_el), UIPx(90.0f),
                       "Elevation to slew to when parking (degrees).");
    if (ImGui::IsItemEdited()) RotatorLoadSettings(cfg);
    if (ImGui::IsItemDeactivatedAfterEdit()) SaveAppConfig("settings.json", cfg);
    if (ImGui::Checkbox("Park on stop / exit##rot", &R->park_on_exit))
        RotatorConfigSave(cfg);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Slew to the park position when tracking stops or the app exits.");

    if (ImGui::Checkbox("Park between passes##rot", &R->park_between_passes))
        RotatorConfigSave(cfg);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Slew to the park position in the gap between queued passes\n"
                          "(and resume tracking when the next pass starts).");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Passes / Targets");
    RotatorConfigField("Lead time (s)##rot", R->lead_time, sizeof(R->lead_time), UIPx(90.0f),
                       "Begin slewing this many seconds before AOS.");
    if (ImGui::IsItemEdited()) RotatorLoadSettings(cfg);
    if (ImGui::IsItemDeactivatedAfterEdit()) SaveAppConfig("settings.json", cfg);

    {
        int hold_sz = RotatorGetLosHoldBufferSize();
        if (hold_sz > (int)sizeof(R->los_hold_s)) hold_sz = (int)sizeof(R->los_hold_s);
        RotatorConfigField("LOS hold (s)##rot", R->los_hold_s, (size_t)hold_sz, UIPx(90.0f),
                           "After LOS, keep pointing at the last az/el for this many seconds\n"
                           "(0 = disable the post-LOS hold).");
        if (ImGui::IsItemEdited()) RotatorLoadSettings(cfg);
        if (ImGui::IsItemDeactivatedAfterEdit()) SaveAppConfig("settings.json", cfg);
    }

    if (ImGui::Checkbox("Auto-steer##rot", &R->auto_steer))
    {
        RotatorSetAutoSteer(R->auto_steer);
        RotatorConfigSave(cfg);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Automatically steer the rotator to follow queued passes.");

    if (ImGui::Checkbox("Queue tracking (persist)##rot", &R->queue_tracking))
        RotatorConfigSave(cfg);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Remember the queue run-state across restarts.\n"
                          "The Start/Stop buttons remain the live session gate.");

    const char *steer_modes = "Polar\0";
    R->steer_mode = 0;
    ImGui::BeginDisabled();
    ImGui::Combo("Steer mode##rot", &R->steer_mode, steer_modes);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(polar only)");

    const char *wrap_modes = "Shortest path\0Clockwise only\0Counter-clockwise only\0";
    if (ImGui::Combo("Az wrap##rot", &R->az_wrap_mode, wrap_modes))
        RotatorConfigSave(cfg);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Which way to turn when the target azimuth crosses 0/360.\n"
                          "Shortest path: always the smaller arc (default).\n"
                          "Clockwise / Counter-clockwise only: always turn that way,\n"
                          "e.g. to avoid cable wrap on a rotator without a stop.");
}

void DrawTransitRotatorWindow(UIContext *ctx, AppConfig *cfg)
{
    if (!g_ui.rot_show_window) return;

    ImVec2 screen = ImGui::GetIO().DisplaySize;
    const float def_w = UIPx(840.0f);
    const float def_h = UIPx(680.0f);
    float max_w = (UIPx(1100.0f) < screen.x * 0.62f) ? UIPx(1100.0f) : screen.x * 0.62f;
    if (max_w < UIPx(560.0f)) max_w = UIPx(560.0f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(UIPx(560.0f), UIPx(340.0f)),
                                        ImVec2(max_w, screen.y - UIPx(48.0f)));
    ImGui::SetNextWindowSize(ImVec2(def_w, def_h), ImGuiCond_FirstUseEver);
    float pos_x = screen.x - def_w - UIPx(24.0f);
    if (pos_x < UIPx(8.0f)) pos_x = UIPx(8.0f);
    ImGui::SetNextWindowPos(ImVec2(pos_x, ImGui::GetFrameHeight() + UIPx(16.0f)),
                            ImGuiCond_FirstUseEver);

    if (ImGui::Begin(ICON_FA_SATELLITE_DISH "  Antenna / Rotator", &g_ui.rot_show_window,
                     ImGuiWindowFlags_NoCollapse))
    {
        DrawTransitRotatorTab(ctx, cfg);
    }
    ImGui::End();
}

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

void TransitOpenRotatorTab(AppConfig *cfg)
{
    (void)cfg;
    g_ui.rot_show_window = true;
}

void DrawTransitOverviewModal(UIContext *ctx, AppConfig *cfg)
{
    static bool s_tab_restored = false;
    static int  s_pending_tab = -1;

    // always reach BeginPopupModal so an external close lands on p_open
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
            // old configs may store 4, the removed Rotator tab
            if (s_pending_tab < 0 || s_pending_tab > 3)
                s_pending_tab = 0;
            s_requested_tab = -1;
            s_tab_restored = true;
        }

        float avail_w = ImGui::GetContentRegionAvail().x;
        float footer_h = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        float body_h = ImGui::GetContentRegionAvail().y - footer_h;
        if (body_h < UIPx(120.0f)) body_h = UIPx(120.0f);

        float left_w = avail_w * 0.35f;
        float right_w = avail_w - left_w - ImGui::GetStyle().ItemSpacing.x;

        ImGui::BeginChild("##transit_ov_left", ImVec2(left_w, body_h), true);
        DrawTransitOverviewList(ctx, cfg);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##transit_ov_right", ImVec2(right_w, body_h), true);
        DrawTransitOverviewTabs(ctx, cfg, &s_pending_tab);
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(UIPx(140.0f), 0)))
        {
            g_ui.transit_overview_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
