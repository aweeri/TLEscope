/*
 * tool_layers.cpp - Layers panel
 */

#include "tools.h"
#include "tools_common.h"
#include "core/theme.h"
#include "core/config.h"
#include "ui/labels.h"
#include "ui/ui_layout.h"
#include "ui/tools/tools_settings.h"
#include "map_detail_data.h"

#include <raylib.h>
#include <raymath.h> /* DEG2RAD for the 3D sphere mapping */
#include <rlgl.h>    /* batched line submission for the 3D overlays */
#include <math.h>    /* fabsf, fmaxf, cosf, sinf */
#include <stddef.h> /* offsetof */
#include <stdio.h>  /* snprintf */
#include <float.h>  /* FLT_MAX */
#include <string.h> /* strcmp */
#include <vector>

#include "imgui.h"
#include "IconsFontAwesome6.h"

/* -- Layer registry -------------------------------------------------------- */

/** which views a layer toggle applies to */
typedef enum
{
    LAYER_UNIVERSAL, /* shown in both 2D map and 3D globe */
    LAYER_2D,        /* 2D map only */
    LAYER_3D         /* 3D globe only */
} LayerScope;

/**
 * One row in the layers panel.
 *
 * The value is backed either by a direct AppConfig bool field
 * (cfg_offset, via offsetof) or by a key in the generic tool-settings store
 * (settings_key, via tools_settings.h). cfg_offset == -1 means the row uses
 * settings_key; exactly one of the two backends applies.
 *
 * To add a new layer toggle: append one row to s_layers[] below. If it needs no
 * AppConfig field, give it a namespaced settings_key (e.g. "mytool.enabled")
 * and it will persist automatically via tools_settings.h. Set placeholder=true
 * for toggles that have no rendering effect yet (UI-only, state still saved).
 */
typedef struct
{
    const char *label;
    const char *icon;
    const char *tooltip;
    LayerScope scope;
    int cfg_offset;           /* offsetof(AppConfig, field), or -1 */
    const char *settings_key; /* tool-settings key, or NULL */
    bool placeholder;         /* true = UI only, no render effect yet */
    const char *mode_key;     /* tool-settings key for a Sel/All scope combo, or NULL */
    bool default_on;          /* default value for settings_key-backed rows */
} LayerDef;

/* persisted map overlay keys (ToolSettings store, see tools_settings.h) */
#define GRID_KEY_ENABLED "layers.latlon_grid"
#define GRID_KEY_SPACING "layers.latlon_grid_spacing"
#define COAST_KEY_ENABLED "layers.coast_lines"
#define BORDER_KEY_ENABLED "layers.country_borders"

static const int GRID_SPACINGS[] = { 10, 15, 30, 45, 60 };
static const char *GRID_SPACING_LABELS[] = { "10°", "15°", "30°", "45°", "60°" };
#define GRID_SPACING_COUNT (sizeof(GRID_SPACINGS) / sizeof(GRID_SPACINGS[0]))
#define GRID_SPACING_DEFAULT 2 /* index of 30° in GRID_SPACINGS */

static const LayerDef s_layers[] = {
    /* -- Universal (both 2D map and 3D globe) ------------------------------ */
    { "Earth Texture",     ICON_FA_GLOBE,       "Show the Earth surface texture (off = plain black)", LAYER_UNIVERSAL, -1, LAYERS_KEY_EARTH_TEXTURE, false, NULL, true },
    { "Night Lights",      ICON_FA_MOON,       "Show night-side city lights (N)", LAYER_UNIVERSAL, (int)offsetof(AppConfig, show_night_lights), NULL, false, NULL, false },
    { "Markers",           ICON_FA_MAP_PIN,     "Show ground markers (L)", LAYER_UNIVERSAL, (int)offsetof(AppConfig, show_markers), NULL, false, NULL, false },
    { "Highlight Sunlit",   ICON_FA_BOLT,       "Highlight sunlit portions of orbits", LAYER_UNIVERSAL, (int)offsetof(AppConfig, highlight_sunlit), NULL, false, NULL, false },
    { "Slant Range",       ICON_FA_RULER,       "Show slant range line to home", LAYER_UNIVERSAL, (int)offsetof(AppConfig, show_slant_range), NULL, false, NULL, false },
    { "Ground Coverage",   ICON_FA_ROUTE,       "Show the line-of-sight ground coverage footprint", LAYER_UNIVERSAL, (int)offsetof(AppConfig, show_ground_coverage), NULL, false, LAYERS_KEY_GC_MODE, false },
    { "Apsides",           ICON_FA_CIRCLE_DOT, "Show perigee/apogee markers and altitude labels", LAYER_UNIVERSAL, (int)offsetof(AppConfig, show_apsides), NULL, false, NULL, false },
    { "Coast Lines",       ICON_FA_WATER,       "Show coastline outlines on the map and globe", LAYER_UNIVERSAL, -1, COAST_KEY_ENABLED, false, NULL, false },
    { "Country Borders",   ICON_FA_DRAW_POLYGON, "Show country borders on the map and globe", LAYER_UNIVERSAL, -1, BORDER_KEY_ENABLED, false, NULL, false },

    /* -- 2D map only ------------------------------------------------------- */
    { "Lat/Lon Grid",      ICON_FA_GRIP_LINES,  "Show a latitude/longitude grid on the map", LAYER_2D, -1, GRID_KEY_ENABLED, false, NULL, false },

    /* -- 3D globe only ----------------------------------------------------- */
    { "Clouds",            ICON_FA_CLOUD,       "Show cloud layer (C)", LAYER_3D, (int)offsetof(AppConfig, show_clouds), NULL, false, NULL, false },
    { "Scattering",        ICON_FA_SUN,         "Atmospheric scattering effect", LAYER_3D, (int)offsetof(AppConfig, show_scattering), NULL, false, NULL, false },
    { "Skybox",            ICON_FA_STAR,        "Show starfield skybox", LAYER_3D, (int)offsetof(AppConfig, show_skybox), NULL, false, NULL, false },
};

#define LAYER_COUNT (sizeof(s_layers) / sizeof(s_layers[0]))

static int GridSpacingIndex(AppConfig *cfg)
{
    const int spacing = ToolSettingGetInt(cfg, GRID_KEY_SPACING, GRID_SPACINGS[GRID_SPACING_DEFAULT]);
    for (size_t i = 0; i < GRID_SPACING_COUNT; i++)
        if (GRID_SPACINGS[i] == spacing)
            return (int)i;
    return GRID_SPACING_DEFAULT;
}

void DrawPanelLayers(UIContext *ctx, AppConfig *cfg)
{
    const bool is_2d = ctx->is_2d_view ? *ctx->is_2d_view : false;

    ImGui::PushTextWrapPos(0.0f);

    /* fixed icon width so all checkboxes align vertically */
    const float icon_w = 24.0f;

    auto DrawLayerCheckbox = [&](const char *label, bool *value, const char *icon, const char *tooltip) {
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(*value ? g_theme.ui.accent : g_theme.ui.text_dim));
        ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
        /* center the icon within a fixed-width cell so all rows align */
        ImVec2 icon_sz = ImGui::CalcTextSize(icon);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + (icon_w - icon_sz.x) * 0.5f, pos.y), col, icon);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(icon_w, ImGui::GetFrameHeight()));
        ImGui::SameLine();
        ImGui::Checkbox(label, value);
        if (tooltip && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);
    };

    auto DrawSectionHeader = [&](const char *title) {
        ImGui::Spacing();
        ImGui::TextColored(ThemeColor(g_theme.ui.accent), "%s", title);
        ImGui::Separator();
    };

    auto GetLayerValue = [&](const LayerDef *def) -> bool {
        if (def->cfg_offset >= 0)
            return *(bool *)((char *)cfg + def->cfg_offset);
        return ToolSettingGetBool(cfg, def->settings_key, def->default_on);
    };

    auto SetLayerValue = [&](const LayerDef *def, bool val) {
        if (def->cfg_offset >= 0)
            *(bool *)((char *)cfg + def->cfg_offset) = val;
        else
            ToolSettingSetBool(cfg, def->settings_key, val);
    };

    auto DrawLayerDef = [&](const LayerDef *def) {
        bool val = GetLayerValue(def);
        bool prev = val;
        float row_avail = ImGui::GetContentRegionAvail().x; /* full row width, for right-aligning the combo */
        DrawLayerCheckbox(def->label, &val, def->icon, def->tooltip);
        if (val != prev)
            SetLayerValue(def, val);

        /* optional right-aligned Sel/All scope combo, mirroring the Labels row */
        if (def->mode_key)
        {
            const float combo_w = 60.0f;
            ImGui::SameLine(row_avail - combo_w);
            ImGui::SetNextItemWidth(combo_w);
            int mode = ToolSettingGetInt(cfg, def->mode_key, LAYERS_GC_MODE_SELECTED);
            const char *modes[] = { "Sel", "All" };
            if (ImGui::Combo("##layer_mode", &mode, modes, 2))
                ToolSettingSetInt(cfg, def->mode_key, mode);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Sel: only the active satellite's footprint; All: footprints for all active satellites");
        }
    };

    /* -- Universal: shown in both 2D map and 3D globe ----------------------- */
    DrawSectionHeader("Universal");
    for (size_t i = 0; i < LAYER_COUNT; i++)
        if (s_layers[i].scope == LAYER_UNIVERSAL)
            DrawLayerDef(&s_layers[i]);

    /* Labels layer: master toggle + Sel/All scope dropdown (see labels.h / labels.cpp) */
    bool labels_enabled = ToolSettingGetBool(cfg, LABELS_KEY_ENABLED, true);
    bool labels_prev = labels_enabled;
    float labels_row_avail = ImGui::GetContentRegionAvail().x; /* full row width, for right-aligning the combo */
    DrawLayerCheckbox("Labels", &labels_enabled, ICON_FA_TAG, "Show name labels for satellites and markers");
    if (labels_enabled != labels_prev)
        ToolSettingSetBool(cfg, LABELS_KEY_ENABLED, labels_enabled);

    const float combo_w = 60.0f;
    ImGui::SameLine(labels_row_avail - combo_w);
    ImGui::SetNextItemWidth(combo_w);
    int label_mode = ToolSettingGetInt(cfg, LABELS_KEY_MODE, LABELS_MODE_SELECTED_ONLY);
    const char *modes[] = { "Sel", "All" };
    if (ImGui::Combo("##label_mode", &label_mode, modes, 2))
        ToolSettingSetInt(cfg, LABELS_KEY_MODE, label_mode);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Sel: only the selected satellite's label; All: labels for all active satellites");

    /* -- current view's exclusive layers ------------------------------------ */
    const LayerScope active_scope = is_2d ? LAYER_2D : LAYER_3D;
    DrawSectionHeader(is_2d ? "2D Map" : "3D Globe");
    for (size_t i = 0; i < LAYER_COUNT; i++)
    {
        if (s_layers[i].scope != active_scope)
            continue;

        const float row_avail = ImGui::GetContentRegionAvail().x; /* full row width, for right-aligning the combo */
        DrawLayerDef(&s_layers[i]);

        if (s_layers[i].settings_key && strcmp(s_layers[i].settings_key, GRID_KEY_ENABLED) == 0)
        {
            int spacing = GridSpacingIndex(cfg);
            ImGui::SameLine(row_avail - combo_w);
            ImGui::SetNextItemWidth(combo_w);
            if (ImGui::Combo("##grid_spacing", &spacing, GRID_SPACING_LABELS, (int)GRID_SPACING_COUNT))
                ToolSettingSetInt(cfg, GRID_KEY_SPACING, GRID_SPACINGS[spacing]);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Spacing between grid lines, in degrees");
        }
    }

    if (is_2d)
    {
        /* Future Orbits: master toggle + focused/multi scope + orbit span. */
        bool future_orbits_enabled = ToolSettingGetBool(cfg, LAYERS_KEY_FUTURE_ORBITS, true);
        bool future_orbits_prev = future_orbits_enabled;
        float future_row_avail = ImGui::GetContentRegionAvail().x;

        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(future_orbits_enabled ? g_theme.ui.accent : g_theme.ui.text_dim));
        ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
        const char *icon = ICON_FA_CLOCK_ROTATE_LEFT;
        ImVec2 icon_sz = ImGui::CalcTextSize(icon);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + (icon_w - icon_sz.x) * 0.5f, pos.y), col, icon);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(icon_w, ImGui::GetFrameHeight()));
        ImGui::SameLine();
        ImGui::Checkbox("Future Orbits", &future_orbits_enabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Show predicted future ground tracks on the 2D map");
        if (future_orbits_enabled != future_orbits_prev)
            ToolSettingSetBool(cfg, LAYERS_KEY_FUTURE_ORBITS, future_orbits_enabled);

        ImGui::BeginDisabled(!future_orbits_enabled);

        ImGui::SameLine(future_row_avail - combo_w);
        ImGui::SetNextItemWidth(combo_w);
        int future_mode = ToolSettingGetInt(
            cfg, LAYERS_KEY_FUTURE_ORBITS_MODE, LAYERS_FUTURE_ORBITS_FOCUSED);
        const char *future_modes[] = { "Sel", "Multi" };
        if (ImGui::Combo("##future_orbits_mode", &future_mode, future_modes, 2))
            ToolSettingSetInt(cfg, LAYERS_KEY_FUTURE_ORBITS_MODE, future_mode);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Sel: hovered satellite, otherwise selected satellite; "
                "Multi: up to %d active satellite tracks total",
                LAYERS_FUTURE_ORBITS_MAX_TRACKS);

        ImGui::Indent(icon_w);
        int orbit_quarters = (int)roundf(cfg->orbits_to_draw * 4.0f);
        if (orbit_quarters < 1) orbit_quarters = 1;
        if (orbit_quarters > 40) orbit_quarters = 40;

        const float orbit_value_w =
            ImGui::CalcTextSize("10.00").x + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(future_row_avail - icon_w - orbit_value_w);
        ImGui::SliderInt("##future_orbits", &orbit_quarters, 1, 40, "");
        const bool orbit_slider_hovered = ImGui::IsItemHovered();
        cfg->orbits_to_draw = orbit_quarters * 0.25f;

        ImGui::SameLine();
        ImGui::Text("%.2f", cfg->orbits_to_draw);
        if (orbit_slider_hovered || ImGui::IsItemHovered())
            ImGui::SetTooltip("Number of predicted orbits shown for each future ground track (0.25-orbit steps)");
        ImGui::Unindent(icon_w);

        ImGui::EndDisabled();
    }

    ImGui::PopTextWrapPos();
}

static Vector2 MapDetailToWorld(MapDetailPoint point, float map_w, float map_h)
{
    return {
        ((float)point.lon100 / 100.0f / 360.0f) * map_w,
        -((float)point.lat100 / 100.0f / 180.0f) * map_h
    };
}

/**
 * Draws one MapDetail polyline set (coastlines, borders) in map world space.
 *
 * alpha scales the theme text color; segments that jump the antimeridian are
 * skipped so a polyline wrapping the map edge does not draw a seam across it.
 */
static void DrawMapDetailLines(const SceneContext *sctx, const MapDetailPoint *points,
                               const MapDetailLine *lines, int line_count,
                               float line_width, float alpha)
{
    Color color = g_theme.ui.text;
    color.a = (unsigned char)(color.a * alpha);

    for (int i = 0; i < line_count; i++)
    {
        const int end = lines[i].start + lines[i].count;
        for (int p = lines[i].start; p + 1 < end; p++)
        {
            Vector2 a = MapDetailToWorld(points[p], sctx->map_w, sctx->map_h);
            Vector2 b = MapDetailToWorld(points[p + 1], sctx->map_w, sctx->map_h);
            if (fabsf(a.x - b.x) <= sctx->map_w * 0.45f)
                DrawLineEx(a, b, line_width, color);
        }
    }
}

/* -- 3D globe rendering of the same overlays ------------------------------- */

/* radial lift (km) that clears the 80x80 Earth mesh's faceting (its chord sag
 * is ~6 km) so the lines sit above the surface instead of z-fighting it */
#define MAP_DETAIL_LIFT_KM 6.5f

#define DETAIL_POINT_COUNT(arr) ((int)(sizeof(arr) / sizeof((arr)[0])))

/** Earth-fixed sphere positions (rotation 0) for one MapDetail point set */
static std::vector<Vector3> BuildDetailSphereVerts(const MapDetailPoint *points, int count)
{
    const float r = (EARTH_RADIUS_KM + MAP_DETAIL_LIFT_KM) / DRAW_SCALE;
    std::vector<Vector3> verts(count);
    for (int i = 0; i < count; i++)
    {
        const float lat = (points[i].lat100 / 100.0f) * DEG2RAD;
        const float lon = (points[i].lon100 / 100.0f) * DEG2RAD;
        const float cl = cosf(lat);
        verts[i] = { cl * cosf(lon) * r, sinf(lat) * r, -cl * sinf(lon) * r };
    }
    return verts;
}

/** one batched line pass for a precomputed point set, spun with the globe */
static void DrawDetailLines3D(const std::vector<Vector3> &verts,
                              const MapDetailLine *lines, int line_count,
                              Color color, float rot_rad)
{
    if (verts.empty())
        return;

    const float cr = cosf(rot_rad);
    const float sr = sinf(rot_rad);

    rlBegin(RL_LINES);
    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int i = 0; i < line_count; i++)
    {
        const int end = lines[i].start + lines[i].count;
        for (int p = lines[i].start; p + 1 < end; p++)
        {
            const Vector3 a = verts[p];
            const Vector3 b = verts[p + 1];
            /* rotate about +Y by the globe's sidereal angle (matches earthModel) */
            rlVertex3f(a.x * cr + a.z * sr, a.y, -a.x * sr + a.z * cr);
            rlVertex3f(b.x * cr + b.z * sr, b.y, -b.x * sr + b.z * cr);
        }
    }
    rlEnd();
}

/**
 * Draws coastlines/borders on the 3D globe. The sphere positions are built
 * once; each frame only the sidereal rotation is applied and the segments are
 * submitted as one batched RL_LINES pass, so depth testing occludes the far
 * side against the Earth model drawn before the scene hooks.
 */
static void DrawMapDetailLines3D(const SceneContext *sctx, AppConfig *cfg)
{
    const bool show_coast = ToolSettingGetBool(cfg, COAST_KEY_ENABLED, false);
    const bool show_border = ToolSettingGetBool(cfg, BORDER_KEY_ENABLED, false);
    if (!show_coast && !show_border)
        return;

    static const std::vector<Vector3> coast_verts =
        BuildDetailSphereVerts(MAP_COAST_POINTS, DETAIL_POINT_COUNT(MAP_COAST_POINTS));
    static const std::vector<Vector3> border_verts =
        BuildDetailSphereVerts(MAP_BORDER_POINTS, DETAIL_POINT_COUNT(MAP_BORDER_POINTS));

    const float rot_rad = (float)((sctx->gmst_deg + sctx->earth_rotation_offset) * DEG2RAD);

    rlDrawRenderBatchActive();
    if (show_coast)
    {
        Color c = g_theme.ui.text;
        c.a = (unsigned char)(c.a * 0.5f);
        DrawDetailLines3D(coast_verts, MAP_COAST_LINES, MAP_COAST_LINE_COUNT, c, rot_rad);
    }
    if (show_border)
    {
        Color c = g_theme.ui.text;
        c.a = (unsigned char)(c.a * 0.20f);
        DrawDetailLines3D(border_verts, MAP_BORDER_LINES, MAP_BORDER_LINE_COUNT, c, rot_rad);
    }
    rlDrawRenderBatchActive();
}

void DrawSceneLayers(SceneContext *sctx, AppConfig *cfg)
{
    if (sctx->is_2d_view && sctx->camera2d)
    {
        const float zoom = fmaxf(sctx->camera2d->zoom, 0.10f);

        if (ToolSettingGetBool(cfg, COAST_KEY_ENABLED, false))
            DrawMapDetailLines(sctx, MAP_COAST_POINTS, MAP_COAST_LINES, MAP_COAST_LINE_COUNT, 1.0f / zoom, 0.5f);

        if (ToolSettingGetBool(cfg, BORDER_KEY_ENABLED, false))
            DrawMapDetailLines(sctx, MAP_BORDER_POINTS, MAP_BORDER_LINES, MAP_BORDER_LINE_COUNT, 0.8f / zoom, 0.20f);
    }
    else if (!sctx->is_2d_view)
    {
        DrawMapDetailLines3D(sctx, cfg);
    }

    if (sctx->is_2d_view && sctx->camera2d && ToolSettingGetBool(cfg, GRID_KEY_ENABLED, false))
    {
        float zoom = sctx->camera2d->zoom;
        if (zoom < 0.10f)
            zoom = 0.10f;

        const float thin_width = 0.8f / zoom;
        const float strong_width = 1.1f / zoom;

        Color thin_color = g_theme.ui.text;
        thin_color.a = (unsigned char)(thin_color.a * 0.15f);
        Color strong_color = g_theme.ui.text;
        strong_color.a = (unsigned char)(strong_color.a * 0.28f);

        const int spacing = GRID_SPACINGS[GridSpacingIndex(cfg)];

        /* lines fall on multiples of the spacing from the prime meridian/equator, never on the map edges */
        const int lon_max = (179 / spacing) * spacing;
        for (int lon = -lon_max; lon <= lon_max; lon += spacing)
        {
            const float x = ((float)lon / 360.0f) * sctx->map_w;
            const bool prime_meridian = lon == 0;
            DrawLineEx(
                {x, -sctx->map_h * 0.5f},
                {x, sctx->map_h * 0.5f},
                prime_meridian ? strong_width : thin_width,
                prime_meridian ? strong_color : thin_color);
        }

        const int lat_max = (89 / spacing) * spacing;
        for (int lat = -lat_max; lat <= lat_max; lat += spacing)
        {
            const float y = -((float)lat / 180.0f) * sctx->map_h;
            const bool equator = lat == 0;
            DrawLineEx(
                {-sctx->map_w * 0.5f, y},
                {sctx->map_w * 0.5f, y},
                equator ? strong_width : thin_width,
                equator ? strong_color : thin_color);
        }
    }

    static bool clean_view = false;
    static bool saved_left = true;
    static bool saved_right = true;
    static bool saved_bottom = true;

    if (!ImGui::GetCurrentContext())
        return;

    if (ImGui::GetIO().WantTextInput || !IsKeyPressed(KEY_H))
        return;

    clean_view = !clean_view;
    if (clean_view)
    {
        saved_left = LayoutSidebarVisible(SIDEBAR_LEFT);
        saved_right = LayoutSidebarVisible(SIDEBAR_RIGHT);
        saved_bottom = LayoutBottomBarVisible();

        LayoutSetSidebarVisible(SIDEBAR_LEFT, false);
        LayoutSetSidebarVisible(SIDEBAR_RIGHT, false);
        LayoutSetBottomBarVisible(false);
    }
    else
    {
        LayoutSetSidebarVisible(SIDEBAR_LEFT, saved_left);
        LayoutSetSidebarVisible(SIDEBAR_RIGHT, saved_right);
        LayoutSetBottomBarVisible(saved_bottom);
    }
}

/**
 * Numeric lat/lon labels for the 2D grid.
 *
 * Drawn as a screen-space ImGui overlay (after rlImGuiBegin) rather than in the
 * world-space grid pass, because the raylib custom font has no '°' glyph while
 * the ImGui atlas does. Latitude values sit on the left edge of the visible
 * map, longitude values along its top edge; both follow panning and are culled
 * when their grid line leaves the view.
 */
void DrawMapGridLabels(UIContext *ctx, AppConfig *cfg)
{
    if (!ctx || !cfg || !ctx->camera2d)
        return;
    if (!ctx->is_2d_view || !*ctx->is_2d_view)
        return;
    if (!ToolSettingGetBool(cfg, GRID_KEY_ENABLED, false))
        return;

    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    ImFont *font = ImGui::GetFont();
    if (!dl || !font)
        return;

    const float map_w = ctx->map_w;
    const float map_h = ctx->map_h;
    const float screen_w = (float)GetScreenWidth();
    const float screen_h = (float)GetScreenHeight();

    
    const float nav_h = ImGui::GetFrameHeight();
    const float left_edge = LayoutSidebarVisible(SIDEBAR_LEFT) ? g_layout.left_width : 0.0f;
    const float right_edge = LayoutSidebarVisible(SIDEBAR_RIGHT) ? (screen_w - g_layout.right_width) : screen_w;

    
    Vector2 vis_a = GetScreenToWorld2D((Vector2){left_edge, nav_h}, *ctx->camera2d);
    Vector2 vis_b = GetScreenToWorld2D((Vector2){right_edge, screen_h}, *ctx->camera2d);
    const float clip_min_x = fmaxf(fminf(vis_a.x, vis_b.x), -map_w * 0.5f);
    const float clip_max_x = fminf(fmaxf(vis_a.x, vis_b.x), map_w * 0.5f);
    const float clip_min_y = fmaxf(fminf(vis_a.y, vis_b.y), -map_h * 0.5f);
    const float clip_max_y = fminf(fmaxf(vis_a.y, vis_b.y), map_h * 0.5f);
    if (clip_min_x >= clip_max_x || clip_min_y >= clip_max_y)
        return;

    const int spacing = GRID_SPACINGS[GridSpacingIndex(cfg)];

    const float font_size = font->FontSize * 0.8f;
    Color label_col = g_theme.ui.text;
    label_col.a = (unsigned char)(label_col.a * 0.7f);
    const ImU32 col = IM_COL32(label_col.r, label_col.g, label_col.b, label_col.a);
    const ImU32 shadow_col = IM_COL32(0, 0, 0, 160);
    const float pad = 4.0f * cfg->ui_scale;

    char buf[16];

    /* latitude labels down the left edge of the visible map */
    const int lat_max = (89 / spacing) * spacing;
    for (int lat = -lat_max; lat <= lat_max; lat += spacing)
    {
        const float y = -((float)lat / 180.0f) * map_h;
        if (y < clip_min_y || y > clip_max_y)
            continue;

        if (lat > 0)
            snprintf(buf, sizeof(buf), "%d°N", lat);
        else if (lat < 0)
            snprintf(buf, sizeof(buf), "%d°S", -lat);
        else
            snprintf(buf, sizeof(buf), "0°");

        Vector2 sp = GetWorldToScreen2D((Vector2){clip_min_x, y}, *ctx->camera2d);
        ImVec2 tsz = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, buf);
        /* pin to the free left edge so the label can't slide under a sidebar */
        ImVec2 pos(fmaxf(sp.x + pad, left_edge + pad), sp.y - tsz.y * 0.5f);
        if (pos.y + tsz.y < nav_h || pos.y > screen_h)
            continue;

        dl->AddText(font, font_size, ImVec2(pos.x + 1.0f, pos.y + 1.0f), shadow_col, buf);
        dl->AddText(font, font_size, pos, col, buf);
    }

    /* longitude labels along the top edge of the visible map */
    const int lon_max = (179 / spacing) * spacing;
    for (int lon = -lon_max; lon <= lon_max; lon += spacing)
    {
        const float x = ((float)lon / 360.0f) * map_w;
        if (x < clip_min_x || x > clip_max_x)
            continue;

        if (lon > 0)
            snprintf(buf, sizeof(buf), "%d°E", lon);
        else if (lon < 0)
            snprintf(buf, sizeof(buf), "%d°W", -lon);
        else
            snprintf(buf, sizeof(buf), "0°");

        Vector2 sp = GetWorldToScreen2D((Vector2){x, clip_min_y}, *ctx->camera2d);
        ImVec2 tsz = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, buf);
        /* stick just below the nav bar and keep the label inside the free area */
        ImVec2 pos(sp.x - tsz.x * 0.5f, fmaxf(sp.y + pad, nav_h + pad));
        const float min_x = left_edge + pad;
        const float max_x = fmaxf(min_x, right_edge - tsz.x - pad);
        pos.x = fminf(fmaxf(pos.x, min_x), max_x);
        if (pos.x + tsz.x < left_edge || pos.x > right_edge)
            continue;
        if (pos.y + tsz.y < nav_h || pos.y > screen_h)
            continue;

        dl->AddText(font, font_size, ImVec2(pos.x + 1.0f, pos.y + 1.0f), shadow_col, buf);
        dl->AddText(font, font_size, pos, col, buf);
    }
}
