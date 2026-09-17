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
#include <math.h>   /* fabsf, fmaxf */
#include <stddef.h> /* offsetof */
#include <string.h> /* strcmp */

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
} LayerDef;

/* persisted lat/lon grid keys (ToolSettings store, see tools_settings.h) */
#define GRID_KEY_ENABLED "layers.latlon_grid"
#define GRID_KEY_SPACING "layers.latlon_grid_spacing"
#define BORDER_KEY_ENABLED "layers.country_borders"

static const int GRID_SPACINGS[] = { 10, 15, 30, 45, 60 };
static const char *GRID_SPACING_LABELS[] = { "10°", "15°", "30°", "45°", "60°" };
#define GRID_SPACING_COUNT (sizeof(GRID_SPACINGS) / sizeof(GRID_SPACINGS[0]))
#define GRID_SPACING_DEFAULT 2 /* index of 30° in GRID_SPACINGS */

static const LayerDef s_layers[] = {
    /* -- Universal (both 2D map and 3D globe) ------------------------------ */
    { "Night Lights",      ICON_FA_MOON,       "Show night-side city lights (N)", LAYER_UNIVERSAL, (int)offsetof(AppConfig, show_night_lights), NULL, false, NULL },
    { "Markers",           ICON_FA_MAP_PIN,     "Show ground markers (L)", LAYER_UNIVERSAL, (int)offsetof(AppConfig, show_markers), NULL, false, NULL },
    { "Highlight Sunlit",   ICON_FA_BOLT,       "Highlight sunlit portions of orbits", LAYER_UNIVERSAL, (int)offsetof(AppConfig, highlight_sunlit), NULL, false, NULL },
    { "Slant Range",       ICON_FA_RULER,       "Show slant range line to home", LAYER_UNIVERSAL, (int)offsetof(AppConfig, show_slant_range), NULL, false, NULL },
    { "Ground Coverage",   ICON_FA_ROUTE,       "Show the line-of-sight ground coverage footprint", LAYER_UNIVERSAL, (int)offsetof(AppConfig, show_ground_coverage), NULL, false, LAYERS_KEY_GC_MODE },
    { "Apsides",           ICON_FA_CIRCLE_DOT, "Show perigee/apogee markers and altitude labels", LAYER_UNIVERSAL, (int)offsetof(AppConfig, show_apsides), NULL, false, NULL },

    /* -- 2D map only ------------------------------------------------------- */
    { "Coast Lines",       ICON_FA_WATER,       "Show coastline outlines on the map", LAYER_2D, -1, "layers.coast_lines", false },
    { "Country Borders",   ICON_FA_DRAW_POLYGON, "Show country borders on the map", LAYER_2D, -1, BORDER_KEY_ENABLED, false },
    { "Lat/Lon Grid",      ICON_FA_GRIP_LINES,  "Show a latitude/longitude grid on the map", LAYER_2D, -1, GRID_KEY_ENABLED, false },

    /* -- 3D globe only ----------------------------------------------------- */
    { "Clouds",            ICON_FA_CLOUD,       "Show cloud layer (C)", LAYER_3D, (int)offsetof(AppConfig, show_clouds), NULL, false, NULL },
    { "Scattering",        ICON_FA_SUN,         "Atmospheric scattering effect", LAYER_3D, (int)offsetof(AppConfig, show_scattering), NULL, false, NULL },
    { "Skybox",            ICON_FA_STAR,        "Show starfield skybox", LAYER_3D, (int)offsetof(AppConfig, show_skybox), NULL, false, NULL },
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
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(*value ? g_theme.ui.ui_accent : g_theme.ui.text_secondary));
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
        ImGui::TextColored(ThemeColor(g_theme.ui.ui_accent), "%s", title);
        ImGui::Separator();
    };

    auto GetLayerValue = [&](const LayerDef *def) -> bool {
        if (def->cfg_offset >= 0)
            return *(bool *)((char *)cfg + def->cfg_offset);
        return ToolSettingGetBool(cfg, def->settings_key, false);
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
        /* Future Orbits: master toggle + slider, styled to match the other layer rows (icon cell + label) */
        bool future_orbits_enabled = ToolSettingGetBool(cfg, LAYERS_KEY_FUTURE_ORBITS, true);
        bool future_orbits_prev = future_orbits_enabled;
        float future_row_avail = ImGui::GetContentRegionAvail().x; /* full row width, for right-aligning the slider */

        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(future_orbits_enabled ? g_theme.ui.ui_accent : g_theme.ui.text_secondary));
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
            ImGui::SetTooltip("Show the predicted future orbit track on the 2D map");
        if (future_orbits_enabled != future_orbits_prev)
            ToolSettingSetBool(cfg, LAYERS_KEY_FUTURE_ORBITS, future_orbits_enabled);

        /* right-align the slider on the row, like the Labels Sel/All dropdown above */
        const float slider_w = 140.0f;
        ImGui::SameLine(future_row_avail - slider_w);
        ImGui::SetNextItemWidth(slider_w);
        ImGui::BeginDisabled(!future_orbits_enabled);
        float future_orbits = cfg->orbits_to_draw;
        if (ImGui::SliderFloat("##future_orbits", &future_orbits, 0.25f, 10.0f, "%.2f"))
            cfg->orbits_to_draw = future_orbits;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Number of predicted orbits shown on the 2D map");
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
    Color color = g_theme.ui.text_main;
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

void DrawSceneLayers(SceneContext *sctx, AppConfig *cfg)
{
    if (sctx->is_2d_view && sctx->camera2d)
    {
        const float zoom = fmaxf(sctx->camera2d->zoom, 0.10f);

        if (ToolSettingGetBool(cfg, "layers.coast_lines", false))
            DrawMapDetailLines(sctx, MAP_COAST_POINTS, MAP_COAST_LINES, MAP_COAST_LINE_COUNT, 1.0f / zoom, 0.5f);

        if (ToolSettingGetBool(cfg, BORDER_KEY_ENABLED, false))
            DrawMapDetailLines(sctx, MAP_BORDER_POINTS, MAP_BORDER_LINES, MAP_BORDER_LINE_COUNT, 0.8f / zoom, 0.20f);
    }

    if (sctx->is_2d_view && sctx->camera2d && ToolSettingGetBool(cfg, GRID_KEY_ENABLED, false))
    {
        float zoom = sctx->camera2d->zoom;
        if (zoom < 0.10f)
            zoom = 0.10f;

        const float thin_width = 0.8f / zoom;
        const float strong_width = 1.1f / zoom;

        Color thin_color = g_theme.ui.text_main;
        thin_color.a = (unsigned char)(thin_color.a * 0.15f);
        Color strong_color = g_theme.ui.text_main;
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
