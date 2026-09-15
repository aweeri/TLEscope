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

#include <raylib.h>
#include <stddef.h> /* offsetof */

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
} LayerDef;

static const LayerDef s_layers[] = {
    /* -- Universal (both 2D map and 3D globe) ------------------------------ */
    { "Night Lights",      ICON_FA_MOON,       "Show night-side city lights (N)", LAYER_UNIVERSAL, (int)offsetof(AppConfig, show_night_lights), NULL, false },
    { "Markers",           ICON_FA_MAP_PIN,     "Show ground markers (L)", LAYER_UNIVERSAL, (int)offsetof(AppConfig, show_markers), NULL, false },
    { "Highlight Sunlit",   ICON_FA_BOLT,       "Highlight sunlit portions of orbits", LAYER_UNIVERSAL, (int)offsetof(AppConfig, highlight_sunlit), NULL, false },
    { "Slant Range",       ICON_FA_RULER,       "Show slant range line to home", LAYER_UNIVERSAL, (int)offsetof(AppConfig, show_slant_range), NULL, false },
    { "Ground Coverage",   ICON_FA_ROUTE,       "Show the line-of-sight ground coverage footprint", LAYER_UNIVERSAL, (int)offsetof(AppConfig, show_ground_coverage), NULL, false },
    { "Apsides",           ICON_FA_CIRCLE_DOT, "Show perigee/apogee markers and altitude labels", LAYER_UNIVERSAL, (int)offsetof(AppConfig, show_apsides), NULL, false },

    /* -- 2D map only ------------------------------------------------------- */
    { "Coast Lines",       ICON_FA_WATER,       "Show coastline outlines on the map (not implemented yet)", LAYER_2D, -1, "layers.coast_lines", true },
    { "Lat/Lon Grid",      ICON_FA_GRIP_LINES,  "Show a 30-degree latitude/longitude grid on the map", LAYER_2D, -1, "layers.latlon_grid", false },

    /* -- 3D globe only ----------------------------------------------------- */
    { "Clouds",            ICON_FA_CLOUD,       "Show cloud layer (C)", LAYER_3D, (int)offsetof(AppConfig, show_clouds), NULL, false },
    { "Scattering",        ICON_FA_SUN,         "Atmospheric scattering effect", LAYER_3D, (int)offsetof(AppConfig, show_scattering), NULL, false },
    { "Skybox",            ICON_FA_STAR,        "Show starfield skybox", LAYER_3D, (int)offsetof(AppConfig, show_skybox), NULL, false },
};

#define LAYER_COUNT (sizeof(s_layers) / sizeof(s_layers[0]))

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
        DrawLayerCheckbox(def->label, &val, def->icon, def->tooltip);
        if (val != prev)
            SetLayerValue(def, val);
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
        if (s_layers[i].scope == active_scope)
            DrawLayerDef(&s_layers[i]);

    ImGui::PopTextWrapPos();
}

void DrawSceneLayers(SceneContext *sctx, AppConfig *cfg)
{
    if (sctx->is_2d_view && sctx->camera2d && ToolSettingGetBool(cfg, "layers.latlon_grid", false))
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

        for (int lon = -150; lon <= 150; lon += 30)
        {
            const float x = ((float)lon / 360.0f) * sctx->map_w;
            const bool prime_meridian = lon == 0;
            DrawLineEx(
                {x, -sctx->map_h * 0.5f},
                {x, sctx->map_h * 0.5f},
                prime_meridian ? strong_width : thin_width,
                prime_meridian ? strong_color : thin_color);
        }

        for (int lat = -60; lat <= 60; lat += 30)
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
