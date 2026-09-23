/*
 * labels.cpp - Screen-space label overlay for the 3D/2D scene.
 *
 * Replaces the old raylib DrawTextEx/DrawUIText label calls in main.cpp with a
 * unified Dear ImGui draw-list overlay. Labels are collected each frame,
 * projected to screen space, decluttered (priority + overlap rejection), and
 * rendered via ImGui::GetBackgroundDrawList() so they draw on top of the scene
 * with full theme support (text shadow / background for contrast).
 *
 * Config is stored in the generic ToolSettings store (see tools_settings.h):
 *   labels.enabled       bool   master switch (default true)
 *   labels.mode          int    0=selected only (Sel), 1=all active (All)
 *   labels.show_altitude bool   append altitude (km) to satellite labels
 *   labels.size          float  scale multiplier on the UI font (default 1.0)
 *   labels.background    bool   draw a rounded background behind text
 *   labels.max_count     int    declutter cap (default 200)
 */

#include "labels.h"
#include "core/astro.h"
#include "core/theme.h"
#include "core/location.h"
#include "data/storage.h"
#include "ui/tools/tools_settings.h"

#include <raylib.h>
#include <raymath.h>

#include "imgui.h"

#include <vector>
#include <cstring>
#include <cstdio>
#include <algorithm>

/* -- Config keys (shared with the Layers panel via labels.h) --------------- */

/* -- Label candidate ------------------------------------------------------- */

struct LabelCandidate
{
    ImVec2 anchor;      /* screen-space anchor (top-left of text) */
    char text[96];
    ImU32 color;
    int priority;       /* higher = placed first / survives declutter */
    float size;
    bool centered;      /* anchor is the text center (slant range) */
};

static ImU32 ToImU32(Color c)
{
    return IM_COL32(c.r, c.g, c.b, c.a);
}

/* -- Decluttering ---------------------------------------------------------- */

static bool RectsOverlap(const ImVec2 &aMin, const ImVec2 &aMax,
                         const ImVec2 &bMin, const ImVec2 &bMax)
{
    return !(aMax.x < bMin.x || aMin.x > bMax.x ||
             aMax.y < bMin.y || aMin.y > bMax.y);
}

/* -- Main entry ------------------------------------------------------------ */

void DrawSceneLabels(UIContext *ctx, AppConfig *cfg)
{
    if (!ctx || !cfg) return;

    bool enabled = ToolSettingGetBool(cfg, LABELS_KEY_ENABLED, true);
    if (!enabled) return;

    int mode = ToolSettingGetInt(cfg, LABELS_KEY_MODE, LABELS_MODE_SELECTED_ONLY);
    /* normalize: only Sel (0), All (1), and Fav (2) are valid now; treat other
     * legacy values (e.g. the old "None" = 2) as the default Sel scope */
    if (mode != LABELS_MODE_ALL && mode != LABELS_MODE_FAV)
        mode = LABELS_MODE_SELECTED_ONLY;
    bool show_alt = ToolSettingGetBool(cfg, LABELS_KEY_ALTITUDE, false);
    float size_mult = ToolSettingGetFloat(cfg, LABELS_KEY_SIZE, 1.0f);
    if (size_mult < 0.25f) size_mult = 0.25f;
    bool use_bg = ToolSettingGetBool(cfg, LABELS_KEY_BG, true);
    int max_count = ToolSettingGetInt(cfg, LABELS_KEY_MAX_COUNT, 200);
    if (max_count < 1) max_count = 1;

    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    ImFont *font = ImGui::GetFont();
    if (!dl || !font) return;

    /* render at the UI font's native size (crisp, no glyph scaling);
     * the size setting is a multiplier around it */
    float size = font->FontSize * size_mult;

    float ui_scale = cfg->ui_scale;
    float icon_half = 12.0f * ui_scale; /* half of the 24px sat/marker icon */
    float pad = 4.0f;                   /* gap between icon and text */

    std::vector<LabelCandidate> cands;
    cands.reserve(256);

    bool is_2d = ctx->is_2d_view ? *ctx->is_2d_view : false;
    bool hide_unselected = ctx->hide_unselected ? *ctx->hide_unselected : false;
    bool is_pov = ctx->is_pov_mode ? *ctx->is_pov_mode : false;
    double epoch = ctx->current_epoch ? *ctx->current_epoch : 0.0;

    Satellite *selected = ctx->selected_sat ? *ctx->selected_sat : NULL;
    Satellite *hovered = ctx->hovered_sat;
    Satellite *active = ctx->active_sat;

    /* screen-space clip rect for the 2D map (labels must stay on the map) */
    ImVec2 clipMin(0.0f, 0.0f);
    ImVec2 clipMax(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    if (is_2d && ctx->camera2d)
    {
        Vector2 m0 = GetWorldToScreen2D((Vector2){-ctx->map_w / 2.0f, -ctx->map_h / 2.0f}, *ctx->camera2d);
        Vector2 m1 = GetWorldToScreen2D((Vector2){ ctx->map_w / 2.0f,  ctx->map_h / 2.0f}, *ctx->camera2d);
        /* the map wraps horizontally, so only clip vertically to the map edges */
        clipMin = ImVec2(0.0f, fmaxf(m0.y, 0.0f));
        clipMax = ImVec2(ImGui::GetIO().DisplaySize.x,
                         fminf(m1.y, ImGui::GetIO().DisplaySize.y));
    }

    /* the 2D map wraps horizontally; pick the on-screen copy of a map point */
    auto MapToScreen2D = [&](float mx, float my, ImVec2 *out) -> bool {
        if (!ctx->camera2d) return false;
        float screen_cx = (clipMin.x + clipMax.x) * 0.5f;
        bool found = false;
        float best_dist = 0.0f;
        for (int off = -1; off <= 1; off++)
        {
            Vector2 sp = GetWorldToScreen2D((Vector2){mx + off * ctx->map_w, my}, *ctx->camera2d);
            if (sp.x >= clipMin.x && sp.x <= clipMax.x)
            {
                float d = fabsf(sp.x - screen_cx);
                if (!found || d < best_dist)
                {
                    best_dist = d;
                    *out = ImVec2(sp.x, sp.y);
                    found = true;
                }
            }
        }
        return found;
    };

    /* ---- satellite labels ---- */
    for (int i = 0; i < sat_count; i++)
    {
        Satellite &s = satellites[i];
        if (!s.is_active) continue;

        bool is_selected = (selected == &s);
        bool is_hovered = (hovered == &s);
        bool is_active = (active == &s);

        /* Sel mode: only the selected satellite gets a label (hovered always shows) */
        if (mode == LABELS_MODE_SELECTED_ONLY && !is_selected && !is_hovered) continue;

        /* Fav mode: favorite satellites get a label, plus the selected one (hovered always shows) */
        if (mode == LABELS_MODE_FAV && !IsFavorite(s.norad_id_num) && !is_selected && !is_hovered) continue;

        /* respect hide-unselected isolation */
        if (hide_unselected && selected != NULL && !is_selected) continue;

        /* skip the POV camera's own satellite (its icon is hidden too) */
        if (is_pov && is_selected) continue;

        ImVec2 anchor;
        if (is_2d)
        {
            float mx, my;
            get_map_coordinates(s.current_pos, ctx->gmst_deg, cfg->earth_rotation_offset,
                                ctx->map_w, ctx->map_h, &mx, &my);
            ImVec2 sp;
            if (!MapToScreen2D(mx, my, &sp)) continue;
            anchor = ImVec2(sp.x + icon_half + pad, sp.y - icon_half);
        }
        else
        {
            if (!ctx->camera3d) continue;
            Vector3 draw_pos = Vector3Scale(s.current_pos, 1.0f / DRAW_SCALE);
            Vector3 toTarget = Vector3Subtract(draw_pos, ctx->camera3d->position);
            Vector3 camForward = Vector3Normalize(Vector3Subtract(ctx->camera3d->target, ctx->camera3d->position));
            float draw_earth_radius = EARTH_RADIUS_KM / DRAW_SCALE;
            if (Vector3DotProduct(toTarget, camForward) <= 0.0f) continue;
            if (IsOccludedByEarth(ctx->camera3d->position, draw_pos, draw_earth_radius)) continue;
            Vector2 sp = GetWorldToScreen(draw_pos, *ctx->camera3d);
            anchor = ImVec2(sp.x + icon_half + pad, sp.y - icon_half);
        }

        LabelCandidate c;
        if (show_alt)
        {
            float alt = Vector3Length(s.current_pos) - EARTH_RADIUS_KM;
            snprintf(c.text, sizeof(c.text), "%s  %.0f km", s.name, alt);
        }
        else
        {
            snprintf(c.text, sizeof(c.text), "%s", s.name);
        }
        c.anchor = anchor;
        c.size = size;
        c.centered = false;
        /* the selected/hovered satellite's name always wins decluttering:
         * it must outrank its own apo/peri labels (priority 5) and every
         * other satellite label so the focused sat is never occluded */
        if (is_selected)      { c.color = ToImU32(g_theme.world.sat_selected);   c.priority = 10; }
        else if (is_hovered)  { c.color = ToImU32(g_theme.world.sat_hover); c.priority = 9; }
        else if (is_active)   { c.color = ToImU32(g_theme.world.sat_hover); c.priority = 2; }
        else                  { c.color = ToImU32(g_theme.world.sat);      c.priority = 1; }
        cands.push_back(c);
    }

    /* ---- apsis labels (active sat) ---- */
    if (cfg->show_apsides && active && active->is_active)
    {
        bool is_unselected = (selected != NULL && active != selected);
        if (!(hide_unselected && is_unselected))
        {
            double t_peri_unix, t_apo_unix;
            get_apsis_times(active, epoch, &t_peri_unix, &t_apo_unix);
            Vector3 draw_p = Vector3Scale(calculate_position(active, t_peri_unix), 1.0f / DRAW_SCALE);
            Vector3 draw_a = Vector3Scale(calculate_position(active, t_apo_unix), 1.0f / DRAW_SCALE);

            if (!is_2d && ctx->camera3d)
            {
                float draw_earth_radius = EARTH_RADIUS_KM / DRAW_SCALE;
                if (!IsOccludedByEarth(ctx->camera3d->position, draw_p, draw_earth_radius))
                {
                    Vector2 sp = GetWorldToScreen(draw_p, *ctx->camera3d);
                    LabelCandidate c;
                    snprintf(c.text, sizeof(c.text), "P %.0f km", calc_perigee_km(active));
                    c.anchor = ImVec2(sp.x + 16.0f * ui_scale + pad, sp.y - 16.0f * ui_scale);
                    c.color = ToImU32(g_theme.world.periapsis);
                    c.priority = 5;
                    c.size = size;
                    c.centered = false;
                    cands.push_back(c);
                }
                if (!IsOccludedByEarth(ctx->camera3d->position, draw_a, draw_earth_radius))
                {
                    Vector2 sp = GetWorldToScreen(draw_a, *ctx->camera3d);
                    LabelCandidate c;
                    snprintf(c.text, sizeof(c.text), "A %.0f km", calc_apogee_km(active));
                    c.anchor = ImVec2(sp.x + 16.0f * ui_scale + pad, sp.y - 16.0f * ui_scale);
                    c.color = ToImU32(g_theme.world.apoapsis);
                    c.priority = 5;
                    c.size = size;
                    c.centered = false;
                    cands.push_back(c);
                }
            }
            else if (is_2d && ctx->camera2d)
            {
                Vector2 peri2d, apo2d;
                get_apsis_2d(active, epoch, false, ctx->gmst_deg, cfg->earth_rotation_offset,
                             ctx->map_w, ctx->map_h, &peri2d);
                get_apsis_2d(active, epoch, true, ctx->gmst_deg, cfg->earth_rotation_offset,
                             ctx->map_w, ctx->map_h, &apo2d);
                Vector2 sp_p = GetWorldToScreen2D(peri2d, *ctx->camera2d);
                Vector2 sp_a = GetWorldToScreen2D(apo2d, *ctx->camera2d);

                LabelCandidate c;
                snprintf(c.text, sizeof(c.text), "P %.0f km", calc_perigee_km(active));
                c.anchor = ImVec2(sp_p.x + 16.0f * ui_scale + pad, sp_p.y - 16.0f * ui_scale);
                c.color = ToImU32(g_theme.world.periapsis);
                c.priority = 5;
                c.size = size;
                c.centered = false;
                cands.push_back(c);

                snprintf(c.text, sizeof(c.text), "A %.0f km", calc_apogee_km(active));
                c.anchor = ImVec2(sp_a.x + 16.0f * ui_scale + pad, sp_a.y - 16.0f * ui_scale);
                c.color = ToImU32(g_theme.world.apoapsis);
                c.priority = 5;
                c.size = size;
                c.centered = false;
                cands.push_back(c);
            }
        }
    }

    /* ---- home + location labels ---- */
    if (cfg->show_markers)
    {
        Location *home = GetHomeLocation();
        if (home)
        {
            LabelCandidate c;
            snprintf(c.text, sizeof(c.text), "%s", home->name);
            c.color = ToImU32(WHITE);
            c.priority = 2;
            c.size = size;
            c.centered = false;
            if (is_2d && ctx->camera2d)
            {
                float hx = (home->lon / 360.0f) * ctx->map_w;
                float hy = -(home->lat / 180.0f) * ctx->map_h;
                ImVec2 sp;
                if (MapToScreen2D(hx, hy, &sp))
                {
                    c.anchor = ImVec2(sp.x + icon_half + pad, sp.y - icon_half);
                    cands.push_back(c);
                }
            }
            else if (!is_2d && ctx->camera3d)
            {
                float h_lat_rad = home->lat * DEG2RAD;
                float h_lon_rad = (home->lon + ctx->gmst_deg + cfg->earth_rotation_offset) * DEG2RAD;
                float r = EARTH_RADIUS_KM / DRAW_SCALE;
                Vector3 h_pos = {cosf(h_lat_rad) * cosf(h_lon_rad) * r, sinf(h_lat_rad) * r,
                                 -cosf(h_lat_rad) * sinf(h_lon_rad) * r};
                Vector3 h_normal = Vector3Normalize(h_pos);
                Vector3 h_viewDir = Vector3Normalize(Vector3Subtract(ctx->camera3d->position, h_pos));
                Vector3 h_toTarget = Vector3Subtract(h_pos, ctx->camera3d->position);
                Vector3 camForward = Vector3Normalize(Vector3Subtract(ctx->camera3d->target, ctx->camera3d->position));
                if (Vector3DotProduct(h_normal, h_viewDir) > 0.0f &&
                    Vector3DotProduct(h_toTarget, camForward) > 0.0f)
                {
                    Vector2 sp = GetWorldToScreen(h_pos, *ctx->camera3d);
                    c.anchor = ImVec2(sp.x + icon_half + pad, sp.y - icon_half);
                    cands.push_back(c);
                }
            }
        }

        for (int i = 0; i < location_count; i++)
        {
            if (locations[i].is_home) continue;
            LabelCandidate c;
            snprintf(c.text, sizeof(c.text), "%s", locations[i].name);
            c.color = ToImU32(WHITE);
            c.priority = 1;
            c.size = size;
            c.centered = false;
            if (is_2d && ctx->camera2d)
            {
                float mx = (locations[i].lon / 360.0f) * ctx->map_w;
                float my = -(locations[i].lat / 180.0f) * ctx->map_h;
                ImVec2 sp;
                if (MapToScreen2D(mx, my, &sp))
                {
                    c.anchor = ImVec2(sp.x + icon_half + pad, sp.y - icon_half);
                    cands.push_back(c);
                }
            }
            else if (!is_2d && ctx->camera3d)
            {
                float lat_rad = locations[i].lat * DEG2RAD;
                float lon_rad = (locations[i].lon + ctx->gmst_deg + cfg->earth_rotation_offset) * DEG2RAD;
                float r = EARTH_RADIUS_KM / DRAW_SCALE;
                Vector3 m_pos = {cosf(lat_rad) * cosf(lon_rad) * r, sinf(lat_rad) * r,
                                 -cosf(lat_rad) * sinf(lon_rad) * r};
                Vector3 normal = Vector3Normalize(m_pos);
                Vector3 viewDir = Vector3Normalize(Vector3Subtract(ctx->camera3d->position, m_pos));
                Vector3 toTarget = Vector3Subtract(m_pos, ctx->camera3d->position);
                Vector3 camForward = Vector3Normalize(Vector3Subtract(ctx->camera3d->target, ctx->camera3d->position));
                if (Vector3DotProduct(normal, viewDir) > 0.0f &&
                    Vector3DotProduct(toTarget, camForward) > 0.0f)
                {
                    Vector2 sp = GetWorldToScreen(m_pos, *ctx->camera3d);
                    c.anchor = ImVec2(sp.x + icon_half + pad, sp.y - icon_half);
                    cands.push_back(c);
                }
            }
        }
    }

    /* ---- slant range label ---- */
    if (cfg->show_slant_range && active && active->is_active)
    {
        Location *home = GetHomeLocation();
        if (home)
        {
            double range = get_sat_range(active, epoch, *home);
            LabelCandidate c;
            snprintf(c.text, sizeof(c.text), "%.1f km", range);
            c.color = ToImU32(g_theme.ui.accent);
            c.priority = 5;
            c.size = size;
            c.centered = true;
            if (is_2d && ctx->camera2d)
            {
                float sx, sy;
                get_map_coordinates(active->current_pos, ctx->gmst_deg, cfg->earth_rotation_offset,
                                    ctx->map_w, ctx->map_h, &sx, &sy);
                float hx = (home->lon / 360.0f) * ctx->map_w;
                float hy = -(home->lat / 180.0f) * ctx->map_h;
                if (sx - hx > ctx->map_w / 2.0f) sx -= ctx->map_w;
                else if (hx - sx > ctx->map_w / 2.0f) sx += ctx->map_w;
                Vector2 mid = {(hx + sx) / 2.0f, (hy + sy) / 2.0f};
                ImVec2 sp;
                if (MapToScreen2D(mid.x, mid.y, &sp))
                {
                    c.anchor = ImVec2(sp.x, sp.y);
                    cands.push_back(c);
                }
            }
            else if (!is_2d && ctx->camera3d)
            {
                float h_lat_rad = home->lat * DEG2RAD;
                float h_lon_rad = (home->lon + ctx->gmst_deg + cfg->earth_rotation_offset) * DEG2RAD;
                float r = EARTH_RADIUS_KM / DRAW_SCALE;
                Vector3 h_pos = {cosf(h_lat_rad) * cosf(h_lon_rad) * r, sinf(h_lat_rad) * r,
                                 -cosf(h_lat_rad) * sinf(h_lon_rad) * r};
                Vector3 s_pos = Vector3Scale(active->current_pos, 1.0f / DRAW_SCALE);
                Vector3 mid_pos = Vector3Lerp(h_pos, s_pos, 0.5f);
                Vector3 toMid = Vector3Subtract(mid_pos, ctx->camera3d->position);
                Vector3 camForward = Vector3Normalize(Vector3Subtract(ctx->camera3d->target, ctx->camera3d->position));
                if (Vector3DotProduct(Vector3Normalize(toMid), camForward) > 0.0f)
                {
                    Vector2 sp = GetWorldToScreen(mid_pos, *ctx->camera3d);
                    c.anchor = ImVec2(sp.x, sp.y);
                    cands.push_back(c);
                }
            }
        }
    }

    /* ---- declutter + render ---- */
    if (cands.empty()) return;

    /* sort by priority descending so important labels are placed first */
    std::stable_sort(cands.begin(), cands.end(),
                     [](const LabelCandidate &a, const LabelCandidate &b) {
                         return a.priority > b.priority;
                     });

    ImU32 bg_col = ToImU32(ThemeAlpha(g_theme.ui.bg, 0.82f));
    ImU32 border_col = IM_COL32(255, 255, 255, 36);
    ImU32 shadow_col = IM_COL32(0, 0, 0, 200);

    /* clip to the 2D map rect (or the screen in 3D) so labels never bleed
     * past the map edge like the old scissor-mode rendering prevented */
    dl->PushClipRect(clipMin, clipMax, true);

    std::vector<ImVec2> placedMin, placedMax;
    placedMin.reserve(max_count);
    placedMax.reserve(max_count);

    int placed = 0;
    for (const LabelCandidate &c : cands)
    {
        if (placed >= max_count) break;

        ImVec2 tsz = font->CalcTextSizeA(c.size, FLT_MAX, 0.0f, c.text);
        float bx = 6.0f, by = 3.0f; /* background padding */
        ImVec2 text_pos = c.anchor;
        if (c.centered)
            text_pos = ImVec2(c.anchor.x - tsz.x * 0.5f, c.anchor.y - tsz.y * 0.5f);
        ImVec2 rmin = ImVec2(text_pos.x - bx, text_pos.y - by);
        ImVec2 rmax = ImVec2(text_pos.x + tsz.x + bx, text_pos.y + tsz.y + by);

        /* off-screen culling */
        if (rmax.x < clipMin.x || rmin.x > clipMax.x ||
            rmax.y < clipMin.y || rmin.y > clipMax.y)
            continue;

        /* overlap rejection */
        bool overlap = false;
        for (int k = 0; k < placed; k++)
        {
            if (RectsOverlap(rmin, rmax, placedMin[k], placedMax[k]))
            {
                overlap = true;
                break;
            }
        }
        if (overlap) continue;

        /* draw */
        if (use_bg)
        {
            dl->AddRectFilled(rmin, rmax, bg_col, 4.0f);
            dl->AddRect(rmin, rmax, border_col, 4.0f);
        }
        else
        {
            /* text shadow for contrast without a background box */
            dl->AddText(font, c.size, ImVec2(text_pos.x + 1.0f, text_pos.y + 1.0f), shadow_col, c.text);
        }
        dl->AddText(font, c.size, text_pos, c.color, c.text);

        placedMin.push_back(rmin);
        placedMax.push_back(rmax);
        placed++;
    }

    dl->PopClipRect();
}