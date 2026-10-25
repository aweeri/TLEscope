/*
 * tool_layers.cpp - Layers panel
 */

#include "tools.h"
#include "tools_common.h"
#include "core/theme.h"
#include "core/config.h"
#include "core/astro.h"
#include "ui/labels.h"
#include "ui/ui_layout.h"
#include "ui/tools/tools_settings.h"
#include "map_detail_data.h"
#include "render/map_view.h"

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

/* persisted grid spacing key (ToolSettings store, see tools_settings.h) */
#define GRID_KEY_SPACING "layers.latlon_grid_spacing"

static const int GRID_SPACINGS[] = { 10, 15, 30, 45, 60 };
static const char *GRID_SPACING_LABELS[] = { "10°", "15°", "30°", "45°", "60°" };
#define GRID_SPACING_COUNT (sizeof(GRID_SPACINGS) / sizeof(GRID_SPACINGS[0]))
#define GRID_SPACING_DEFAULT 2 /* index of 30° in GRID_SPACINGS */

static int GridSpacingIndex(AppConfig *cfg)
{
    const int spacing = ToolSettingGetInt(cfg, GRID_KEY_SPACING, GRID_SPACINGS[GRID_SPACING_DEFAULT]);
    for (size_t i = 0; i < GRID_SPACING_COUNT; i++)
        if (GRID_SPACINGS[i] == spacing)
            return (int)i;
    return GRID_SPACING_DEFAULT;
}

/* -- Layer visibility presets ---------------------------------------------- */
/* A preset names desired values for the primary + secondary layer toggles;
 * "Custom" shows when the live state matches no preset. */

/* order of the primary (master visibility) toggles covered by presets */
enum LayerToggleId
{
    LAYER_TOGGLE_ORBITS = 0,
    LAYER_TOGGLE_ORBITS_DIMMED,
    LAYER_TOGGLE_LABELS,
    LAYER_TOGGLE_LOS,
    LAYER_TOGGLE_APSIDES,
    LAYER_TOGGLE_SUNLIT,
    LAYER_TOGGLE_DIRECTION,
    LAYER_TOGGLE_SLANT_RANGE,
    LAYER_TOGGLE_COAST_LINES,
    LAYER_TOGGLE_COUNTRY_BORDERS,
    LAYER_TOGGLE_GRID,
    LAYER_TOGGLE_MARKERS,
    LAYER_TOGGLE_EARTH_TEXTURE,
    LAYER_TOGGLE_SUNLIGHT,
    LAYER_TOGGLE_SCATTERING,
    LAYER_TOGGLE_CLOUDS,
    LAYER_TOGGLE_SKYBOX,
    LAYER_TOGGLE_ECLIPTIC,
    LAYER_TOGGLE_MOON_ORBIT,
    LAYER_TOGGLE_SUN,
    LAYER_TOGGLE_SUN_FLARE,
    LAYER_TOGGLE_COUNT
};

/* secondary (sub/scope) options covered by presets; stored as int (scope
 * combos use 0/1/2, on/off sub-toggles use 0/1) */
enum LayerSubToggleId
{
    LAYER_SUB_LABELS_MODE = 0,      /* Labels Sel/All/Fav      (LABELS_KEY_MODE)      */
    LAYER_SUB_GC_MODE,              /* LOS scope Sel/All/Fav   (LAYERS_KEY_GC_MODE)   */
    LAYER_SUB_SUNLIT_SCOPE,         /* Sunlit scope Sel/All/Fav(LAYERS_KEY_ORBITS_SUNLIT_SCOPE) */
    LAYER_SUB_DIRECTION_ANIM,       /* Direction animate pulse (LAYERS_KEY_ORBIT_DIRECTION_ANIM) */
    LAYER_SUB_FAV_ORBITS_COLOR,     /* coloured starred orbits (LAYERS_KEY_FAV_ORBITS_3D) */
    LAYER_SUB_COUNT
};

static const char *LAYER_PRESET_LABELS[LAYER_PRESET_COUNT] = { "Visual", "Simplified", "Minimal" };

/* primary toggle values for one preset; LayerPresetSubValues() fills the rest */
static void LayerPresetValues(int preset, bool out[LAYER_TOGGLE_COUNT])
{
    switch (preset)
    {
    case LAYER_PRESET_VISUAL:
        out[LAYER_TOGGLE_ORBITS] = true;
        out[LAYER_TOGGLE_ORBITS_DIMMED] = true;
        out[LAYER_TOGGLE_LABELS] = true;
        out[LAYER_TOGGLE_LOS] = true;
        out[LAYER_TOGGLE_APSIDES] = true;
        out[LAYER_TOGGLE_SUNLIT] = false;
        out[LAYER_TOGGLE_DIRECTION] = true;
        out[LAYER_TOGGLE_SLANT_RANGE] = false;
        out[LAYER_TOGGLE_COAST_LINES] = true;
        out[LAYER_TOGGLE_COUNTRY_BORDERS] = true;
        out[LAYER_TOGGLE_GRID] = true;
        out[LAYER_TOGGLE_MARKERS] = true;
        out[LAYER_TOGGLE_EARTH_TEXTURE] = true;
        out[LAYER_TOGGLE_SUNLIGHT] = true;
        out[LAYER_TOGGLE_SCATTERING] = true;
        out[LAYER_TOGGLE_CLOUDS] = true;
        out[LAYER_TOGGLE_SKYBOX] = true;
        out[LAYER_TOGGLE_ECLIPTIC] = false;
        out[LAYER_TOGGLE_MOON_ORBIT] = true;
        out[LAYER_TOGGLE_SUN] = true;
        out[LAYER_TOGGLE_SUN_FLARE] = true;
        break;

    case LAYER_PRESET_SIMPLIFIED:
        out[LAYER_TOGGLE_ORBITS] = true;
        out[LAYER_TOGGLE_ORBITS_DIMMED] = true;
        out[LAYER_TOGGLE_LABELS] = true;
        out[LAYER_TOGGLE_LOS] = true;
        out[LAYER_TOGGLE_APSIDES] = true;
        out[LAYER_TOGGLE_SUNLIT] = false;
        out[LAYER_TOGGLE_DIRECTION] = true;
        out[LAYER_TOGGLE_SLANT_RANGE] = false;
        out[LAYER_TOGGLE_COAST_LINES] = true;
        out[LAYER_TOGGLE_COUNTRY_BORDERS] = true;
        out[LAYER_TOGGLE_GRID] = true;
        out[LAYER_TOGGLE_MARKERS] = true;
        out[LAYER_TOGGLE_EARTH_TEXTURE] = true;
        out[LAYER_TOGGLE_SUNLIGHT] = false;
        out[LAYER_TOGGLE_SCATTERING] = false;
        out[LAYER_TOGGLE_CLOUDS] = false;
        out[LAYER_TOGGLE_SKYBOX] = false;
        out[LAYER_TOGGLE_ECLIPTIC] = false;
        out[LAYER_TOGGLE_MOON_ORBIT] = true;
        out[LAYER_TOGGLE_SUN] = true;
        out[LAYER_TOGGLE_SUN_FLARE] = false;
        break;

    case LAYER_PRESET_MINIMAL:
    default:
        out[LAYER_TOGGLE_ORBITS] = true;
        out[LAYER_TOGGLE_ORBITS_DIMMED] = true;
        out[LAYER_TOGGLE_LABELS] = true;
        out[LAYER_TOGGLE_LOS] = true;
        out[LAYER_TOGGLE_APSIDES] = true;
        out[LAYER_TOGGLE_SUNLIT] = false;
        out[LAYER_TOGGLE_DIRECTION] = true;
        out[LAYER_TOGGLE_SLANT_RANGE] = false;
        out[LAYER_TOGGLE_COAST_LINES] = true;
        out[LAYER_TOGGLE_COUNTRY_BORDERS] = true;
        out[LAYER_TOGGLE_GRID] = true;
        out[LAYER_TOGGLE_MARKERS] = true;
        out[LAYER_TOGGLE_EARTH_TEXTURE] = false;
        out[LAYER_TOGGLE_SUNLIGHT] = false;
        out[LAYER_TOGGLE_SCATTERING] = false;
        out[LAYER_TOGGLE_CLOUDS] = false;
        out[LAYER_TOGGLE_SKYBOX] = false;
        out[LAYER_TOGGLE_ECLIPTIC] = false;
        out[LAYER_TOGGLE_MOON_ORBIT] = true;
        out[LAYER_TOGGLE_SUN] = false;
        out[LAYER_TOGGLE_SUN_FLARE] = false;
        break;
    }
}

/* secondary toggle values for one preset (Simplified and Minimal share these) */
static void LayerPresetSubValues(int preset, int out[LAYER_SUB_COUNT])
{
    /* Simplified and Minimal deliberately share identical secondary values */
    switch (preset)
    {
    case LAYER_PRESET_VISUAL:
        out[LAYER_SUB_LABELS_MODE] = LABELS_MODE_FAV;
        out[LAYER_SUB_GC_MODE] = LAYERS_GC_MODE_SELECTED;
        out[LAYER_SUB_SUNLIT_SCOPE] = LAYERS_ORBITS_SUNLIT_SELECTED;
        out[LAYER_SUB_DIRECTION_ANIM] = 0;
        out[LAYER_SUB_FAV_ORBITS_COLOR] = 1; /* coloured starred satellites: on */
        break;

    case LAYER_PRESET_SIMPLIFIED:
    case LAYER_PRESET_MINIMAL:
    default:
        out[LAYER_SUB_LABELS_MODE] = LABELS_MODE_FAV;
        out[LAYER_SUB_GC_MODE] = LAYERS_GC_MODE_SELECTED;
        out[LAYER_SUB_SUNLIT_SCOPE] = LAYERS_ORBITS_SUNLIT_SELECTED;
        out[LAYER_SUB_DIRECTION_ANIM] = 0; /* direction animate: off */
        out[LAYER_SUB_FAV_ORBITS_COLOR] = 0;
        break;
    }
}

/** read the live value of every preset-managed layer into cur[] */
static void ReadLayerState(UIContext *ctx, AppConfig *cfg, bool cur[LAYER_TOGGLE_COUNT], int sub[LAYER_SUB_COUNT])
{
    cur[LAYER_TOGGLE_ORBITS] = ToolSettingGetBool(cfg, LAYERS_KEY_ORBITS, true);
    cur[LAYER_TOGGLE_ORBITS_DIMMED] = ToolSettingGetBool(cfg, LAYERS_KEY_ORBITS_DIMMED, true);
    cur[LAYER_TOGGLE_LABELS] = ToolSettingGetBool(cfg, LABELS_KEY_ENABLED, true);
    cur[LAYER_TOGGLE_LOS] = cfg->show_ground_coverage;
    cur[LAYER_TOGGLE_APSIDES] = cfg->show_apsides;
    cur[LAYER_TOGGLE_SUNLIT] = cfg->highlight_sunlit;
    cur[LAYER_TOGGLE_DIRECTION] = ToolSettingGetBool(cfg, LAYERS_KEY_ORBIT_DIRECTION, true);
    cur[LAYER_TOGGLE_SLANT_RANGE] = cfg->show_slant_range;
    cur[LAYER_TOGGLE_COAST_LINES] = cfg->show_coast_lines;
    cur[LAYER_TOGGLE_COUNTRY_BORDERS] = cfg->show_country_borders;
    cur[LAYER_TOGGLE_GRID] = cfg->show_latlon_grid;
    cur[LAYER_TOGGLE_MARKERS] = cfg->show_markers;
    cur[LAYER_TOGGLE_EARTH_TEXTURE] = cfg->show_earth_texture;
    cur[LAYER_TOGGLE_SUNLIGHT] = cfg->show_night_lights;
    cur[LAYER_TOGGLE_SCATTERING] = cfg->show_scattering;
    cur[LAYER_TOGGLE_CLOUDS] = cfg->show_clouds;
    cur[LAYER_TOGGLE_SKYBOX] = cfg->show_skybox;
    cur[LAYER_TOGGLE_ECLIPTIC] = *ctx->is_ecliptic_frame;
    cur[LAYER_TOGGLE_MOON_ORBIT] = ToolSettingGetBool(cfg, LAYERS_KEY_MOON_ORBIT, true);
    cur[LAYER_TOGGLE_SUN] = cfg->show_sun;
    /* the flare is only meaningful while the sun is visible, so mirror that gating */
    cur[LAYER_TOGGLE_SUN_FLARE] = cfg->show_sun && cfg->show_sun_flare;

    sub[LAYER_SUB_LABELS_MODE] = ToolSettingGetInt(cfg, LABELS_KEY_MODE, LABELS_MODE_SELECTED_ONLY);
    sub[LAYER_SUB_GC_MODE] = ToolSettingGetInt(cfg, LAYERS_KEY_GC_MODE, LAYERS_GC_MODE_SELECTED);
    sub[LAYER_SUB_SUNLIT_SCOPE] = ToolSettingGetInt(cfg, LAYERS_KEY_ORBITS_SUNLIT_SCOPE, LAYERS_ORBITS_SUNLIT_SELECTED);
    sub[LAYER_SUB_DIRECTION_ANIM] = ToolSettingGetBool(cfg, LAYERS_KEY_ORBIT_DIRECTION_ANIM, false) ? 1 : 0;
    sub[LAYER_SUB_FAV_ORBITS_COLOR] = ToolSettingGetBool(cfg, LAYERS_KEY_FAV_ORBITS_3D, false) ? 1 : 0;
}

/** true when cur[]/sub[] exactly match every desired value of the given preset */
static bool LayerStateMatchesPreset(int preset, const bool cur[LAYER_TOGGLE_COUNT], const int sub[LAYER_SUB_COUNT])
{
    bool want[LAYER_TOGGLE_COUNT];
    LayerPresetValues(preset, want);
    for (int i = 0; i < LAYER_TOGGLE_COUNT; i++)
        if (cur[i] != want[i])
            return false;

    int want_sub[LAYER_SUB_COUNT];
    LayerPresetSubValues(preset, want_sub);
    for (int i = 0; i < LAYER_SUB_COUNT; i++)
        if (sub[i] != want_sub[i])
            return false;
    return true;
}

/** apply every layer toggle to the preset's values and persist the selection */
void ApplyLayerPreset(AppConfig *cfg, int preset, bool *is_ecliptic_frame)
{
    if (preset < 0 || preset >= LAYER_PRESET_COUNT)
        return;

    bool want[LAYER_TOGGLE_COUNT];
    LayerPresetValues(preset, want);
    int want_sub[LAYER_SUB_COUNT];
    LayerPresetSubValues(preset, want_sub);

    ToolSettingSetBool(cfg, LAYERS_KEY_ORBITS, want[LAYER_TOGGLE_ORBITS]);
    ToolSettingSetBool(cfg, LAYERS_KEY_ORBITS_DIMMED, want[LAYER_TOGGLE_ORBITS_DIMMED]);
    ToolSettingSetBool(cfg, LABELS_KEY_ENABLED, want[LAYER_TOGGLE_LABELS]);
    cfg->show_ground_coverage = want[LAYER_TOGGLE_LOS];
    cfg->show_apsides = want[LAYER_TOGGLE_APSIDES];
    cfg->highlight_sunlit = want[LAYER_TOGGLE_SUNLIT];
    ToolSettingSetBool(cfg, LAYERS_KEY_ORBIT_DIRECTION, want[LAYER_TOGGLE_DIRECTION]);
    cfg->show_slant_range = want[LAYER_TOGGLE_SLANT_RANGE];
    cfg->show_coast_lines = want[LAYER_TOGGLE_COAST_LINES];
    cfg->show_country_borders = want[LAYER_TOGGLE_COUNTRY_BORDERS];
    cfg->show_latlon_grid = want[LAYER_TOGGLE_GRID];
    cfg->show_markers = want[LAYER_TOGGLE_MARKERS];
    cfg->show_earth_texture = want[LAYER_TOGGLE_EARTH_TEXTURE];
    cfg->show_night_lights = want[LAYER_TOGGLE_SUNLIGHT];
    cfg->show_scattering = want[LAYER_TOGGLE_SCATTERING];
    cfg->show_clouds = want[LAYER_TOGGLE_CLOUDS];
    cfg->show_skybox = want[LAYER_TOGGLE_SKYBOX];
    cfg->show_ecliptic_frame = want[LAYER_TOGGLE_ECLIPTIC];
    if (is_ecliptic_frame)
        *is_ecliptic_frame = want[LAYER_TOGGLE_ECLIPTIC];
    ToolSettingSetBool(cfg, LAYERS_KEY_MOON_ORBIT, want[LAYER_TOGGLE_MOON_ORBIT]);
    cfg->show_sun = want[LAYER_TOGGLE_SUN];
    cfg->show_sun_flare = want[LAYER_TOGGLE_SUN_FLARE];

    /* secondary/sub toggles (scope combos + sub-switches) */
    ToolSettingSetInt(cfg, LABELS_KEY_MODE, want_sub[LAYER_SUB_LABELS_MODE]);
    ToolSettingSetInt(cfg, LAYERS_KEY_GC_MODE, want_sub[LAYER_SUB_GC_MODE]);
    ToolSettingSetInt(cfg, LAYERS_KEY_ORBITS_SUNLIT_SCOPE, want_sub[LAYER_SUB_SUNLIT_SCOPE]);
    ToolSettingSetBool(cfg, LAYERS_KEY_ORBIT_DIRECTION_ANIM, want_sub[LAYER_SUB_DIRECTION_ANIM] != 0);
    ToolSettingSetBool(cfg, LAYERS_KEY_FAV_ORBITS_3D, want_sub[LAYER_SUB_FAV_ORBITS_COLOR] != 0);

    /* remember the last explicitly applied preset (-1 == Custom) */
    ToolSettingSetInt(cfg, LAYERS_KEY_PRESET, preset);
}

void DrawPanelLayers(UIContext *ctx, AppConfig *cfg)
{
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

    /* right-aligned Sel/All/Fav scope combo, disabled while the master toggle is off */
    auto DrawScopeCombo = [&](const char *combo_id, const char *key, int default_mode,
                              const char *tooltip, bool enabled) {
        const float combo_w = 74.0f;
        ImGui::BeginDisabled(!enabled);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - combo_w);
        ImGui::SetNextItemWidth(combo_w);
        int mode = ToolSettingGetInt(cfg, key, default_mode);
        const char *modes[] = { "Sel", "All", "Fav" };
        if (ImGui::Combo(combo_id, &mode, modes, 3))
            ToolSettingSetInt(cfg, key, mode);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);
        ImGui::EndDisabled();
    };

    /* -- Preset -------------------------------------------------------------- */
    {
        bool cur[LAYER_TOGGLE_COUNT];
        int sub[LAYER_SUB_COUNT];
        ReadLayerState(ctx, cfg, cur, sub);

        int matched = LAYERS_PRESET_CUSTOM;
        for (int i = 0; i < LAYER_PRESET_COUNT; i++)
        {
            if (LayerStateMatchesPreset(i, cur, sub))
            {
                matched = i;
                break;
            }
        }

        const char *items[LAYER_PRESET_COUNT + 1];
        for (int i = 0; i < LAYER_PRESET_COUNT; i++)
            items[i] = LAYER_PRESET_LABELS[i];
        items[LAYER_PRESET_COUNT] = "Custom";

        int display = (matched >= 0) ? matched : LAYER_PRESET_COUNT;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Preset:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::Combo("##layers_preset", &display, items, LAYER_PRESET_COUNT + 1))
        {
            /* "Custom" is a status, not an action: leave the layers untouched */
            if (display >= 0 && display < LAYER_PRESET_COUNT)
                ApplyLayerPreset(cfg, display, ctx->is_ecliptic_frame);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Apply a predefined layer visibility profile.\nShows \"Custom\" once any layer is changed manually.");
    }

    /* -- Satellites ---------------------------------------------------------- */
    if (ImGui::CollapsingHeader("Satellites", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();

        /* Orbits: master + dimmed + fav-color */
        {
            bool orbits_enabled = ToolSettingGetBool(cfg, LAYERS_KEY_ORBITS, true);
            bool orbits_prev = orbits_enabled;
            float orbits_row_avail = ImGui::GetContentRegionAvail().x;
            DrawLayerCheckbox("Orbits", &orbits_enabled, ICON_FA_SATELLITE,
                              "Master toggle for all orbit lines (2D and 3D). Off kills every orbit, even favorites.");
            if (orbits_enabled != orbits_prev)
                ToolSettingSetBool(cfg, LAYERS_KEY_ORBITS, orbits_enabled);

            /* right-aligned trailing controls (disabled while master off): dimmed, fav-color */
            ImGui::BeginDisabled(!orbits_enabled);
            const float box_w = 26.0f;
            const float trail_w = box_w + box_w;
            ImGui::SameLine(orbits_row_avail - trail_w);
            bool dimmed_on = ToolSettingGetBool(cfg, LAYERS_KEY_ORBITS_DIMMED, true);
            if (ImGui::Checkbox("##orbits_dimmed", &dimmed_on))
                ToolSettingSetBool(cfg, LAYERS_KEY_ORBITS_DIMMED, dimmed_on);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Draw unselected satellite orbit lines faded (dimmed) behind the focused satellite (applies to both 2D and 3D)");
            ImGui::SameLine();

            bool fav_on = ToolSettingGetBool(cfg, LAYERS_KEY_FAV_ORBITS_3D, false);
            if (ImGui::Checkbox("##orbits_fav", &fav_on))
                ToolSettingSetBool(cfg, LAYERS_KEY_FAV_ORBITS_3D, fav_on);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Color favourite (starred) satellites' orbits");
            ImGui::EndDisabled();
        }

        /* Labels: master + Sel/All/Fav scope */
        {
            bool labels_enabled = ToolSettingGetBool(cfg, LABELS_KEY_ENABLED, true);
            bool labels_prev = labels_enabled;
            DrawLayerCheckbox("Labels", &labels_enabled, ICON_FA_TAG,
                              "Show name labels for satellites and markers (2D and 3D)");
            if (labels_enabled != labels_prev)
                ToolSettingSetBool(cfg, LABELS_KEY_ENABLED, labels_enabled);
            DrawScopeCombo("##label_mode", LABELS_KEY_MODE, LABELS_MODE_SELECTED_ONLY,
                           "Sel: only the selected satellite's label; All: labels for all active satellites; Fav: labels for favorite satellites",
                           labels_enabled);
        }

        /* LOS (line-of-sight ground coverage footprint) */
        {
            bool los_enabled = cfg->show_ground_coverage;
            bool los_prev = los_enabled;
            DrawLayerCheckbox("LOS", &los_enabled, ICON_FA_ROUTE,
                              "Show the line-of-sight ground coverage footprint under each satellite (2D and 3D)");
            if (los_enabled != los_prev)
                cfg->show_ground_coverage = los_enabled;
            DrawScopeCombo("##gc_mode", LAYERS_KEY_GC_MODE, LAYERS_GC_MODE_SELECTED,
                           "Sel: only the active satellite's footprint; All: footprints for all active satellites; Fav: footprints for favorite satellites",
                           los_enabled);
        }

        /* Apsides */
        {
            bool val = cfg->show_apsides;
            bool prev = val;
            DrawLayerCheckbox("Apsides", &val, ICON_FA_CIRCLE_DOT,
                              "Show perigee/apogee markers and altitude labels (2D and 3D)");
            if (val != prev)
                cfg->show_apsides = val;
        }

        /* Sunlit (highlight sunlit portions of orbits) */
        {
            bool val = cfg->highlight_sunlit;
            bool prev = val;
            DrawLayerCheckbox("Sunlit", &val, ICON_FA_BOLT,
                              "Highlight the sunlit portions of orbit lines (2D and 3D)");
            if (val != prev)
                cfg->highlight_sunlit = val;
            DrawScopeCombo("##orbits_sunlit", LAYERS_KEY_ORBITS_SUNLIT_SCOPE, LAYERS_ORBITS_SUNLIT_SELECTED,
                           "Sunlit highlight scope: Sel = active satellite only; All = every active satellite; Fav = favorite satellites only",
                           val);
        }

        /* Direction (trail/future ramp + optional animated pulse) */
        {
            float row_avail = ImGui::GetContentRegionAvail().x;
            bool val = ToolSettingGetBool(cfg, LAYERS_KEY_ORBIT_DIRECTION, true);
            bool prev = val;
            DrawLayerCheckbox("Direction", &val, ICON_FA_ARROW_RIGHT_LONG,
                              "Fade orbit lines along the direction of motion: the trail behind each satellite/moon is brighter and thicker than the path ahead (2D and 3D)");
            if (val != prev)
                ToolSettingSetBool(cfg, LAYERS_KEY_ORBIT_DIRECTION, val);

            /* right-aligned animated-pulse toggle, disabled while the master is off */
            ImGui::BeginDisabled(!val);
            const float box_w = 26.0f;
            ImGui::SameLine(row_avail - box_w);
            bool anim = ToolSettingGetBool(cfg, LAYERS_KEY_ORBIT_DIRECTION_ANIM, false);
            if (ImGui::Checkbox("##orbit_dir_anim", &anim))
                ToolSettingSetBool(cfg, LAYERS_KEY_ORBIT_DIRECTION_ANIM, anim);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Animate a bright pulse sweeping each orbit in the direction of motion");
            ImGui::EndDisabled();
        }

        /* Slant range */
        {
            bool val = cfg->show_slant_range;
            bool prev = val;
            DrawLayerCheckbox("Slant Range", &val, ICON_FA_RULER,
                              "Show a slant range line from the home location to the active satellite (2D and 3D)");
            if (val != prev)
                cfg->show_slant_range = val;
        }

        ImGui::Unindent();
    }

    /* -- Map ---------------------------------------------------------------- */
    if (ImGui::CollapsingHeader("Map", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();

        /* Coast Lines */
        {
            bool val = cfg->show_coast_lines;
            bool prev = val;
            DrawLayerCheckbox("Coast Lines", &val, ICON_FA_WATER,
                              "Show coastline outlines on the map and globe (2D and 3D)");
            if (val != prev)
                cfg->show_coast_lines = val;
        }

        /* Country Borders */
        {
            bool val = cfg->show_country_borders;
            bool prev = val;
            DrawLayerCheckbox("Country Borders", &val, ICON_FA_DRAW_POLYGON,
                              "Show country borders on the map and globe (2D and 3D)");
            if (val != prev)
                cfg->show_country_borders = val;
        }

        /* Grid (2D and 3D) + spacing combo */
        {
            const float combo_w = 60.0f;
            float row_avail = ImGui::GetContentRegionAvail().x;
            bool val = cfg->show_latlon_grid;
            bool prev = val;
            DrawLayerCheckbox("Grid", &val, ICON_FA_GRIP_LINES,
                              "Show a latitude/longitude grid on the map and globe.\n2D and 3D.");
            if (val != prev)
                cfg->show_latlon_grid = val;

            ImGui::BeginDisabled(!val);
            ImGui::SameLine(row_avail - combo_w);
            ImGui::SetNextItemWidth(combo_w);
            int spacing = GridSpacingIndex(cfg);
            if (ImGui::Combo("##grid_spacing", &spacing, GRID_SPACING_LABELS, (int)GRID_SPACING_COUNT))
                ToolSettingSetInt(cfg, GRID_KEY_SPACING, GRID_SPACINGS[spacing]);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Spacing between grid lines, in degrees");
            ImGui::EndDisabled();
        }

        /* Markers */
        {
            bool val = cfg->show_markers;
            bool prev = val;
            DrawLayerCheckbox("Markers", &val, ICON_FA_MAP_PIN,
                              "Show ground markers (home location and saved places).\n2D and 3D.");
            if (val != prev)
                cfg->show_markers = val;
        }

        ImGui::Unindent();
    }

    /* -- Earth & Scene ------------------------------------------------------- */
    if (ImGui::CollapsingHeader("Earth & Scene", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();

        /* Earth Texture (master), off = theme-colored solid body */
        bool earth_texture_on = cfg->show_earth_texture;
        {
            bool val = earth_texture_on;
            bool prev = val;
            DrawLayerCheckbox("Earth Texture", &val, ICON_FA_GLOBE,
                              "Show the Earth surface texture (off = a shaded theme-colored globe).\n2D and 3D.");
            if (val != prev)
            {
                earth_texture_on = val;
                cfg->show_earth_texture = val;
            }
        }

        /* earth-texture sub-controls (disabled while the master is off) */
        ImGui::BeginDisabled(!earth_texture_on);

        /* Sunlight: toggles night-side lights + the visible sun disc */
        {
            bool val = cfg->show_night_lights;
            if (!earth_texture_on)
            {
                cfg->show_night_lights = false;
                val = false;
            }
            bool prev = val;
            DrawLayerCheckbox("Sunlight", &val, ICON_FA_SUN,
                              "Toggles the sun and night-side city lights (N). Night lights render in both views; the visible sun disc is 3D only.");
            if (val != prev)
                cfg->show_night_lights = val;
        }

        /* Scattering (3D only) */
        {
            bool val = cfg->show_scattering;
            if (!earth_texture_on)
            {
                cfg->show_scattering = false;
                val = false;
            }
            bool prev = val;
            DrawLayerCheckbox("Scattering", &val, ICON_FA_CLOUD_SUN,
                              "Atmospheric scattering glow around the Earth.\n3D only.");
            if (val != prev)
                cfg->show_scattering = val;
        }

        /* Clouds (3D only) */
        {
            bool val = cfg->show_clouds;
            if (!earth_texture_on)
            {
                cfg->show_clouds = false;
                val = false;
            }
            bool prev = val;
            DrawLayerCheckbox("Clouds", &val, ICON_FA_CLOUD,
                              "Show the cloud layer over the Earth. (C)\n3D only.");
            if (val != prev)
                cfg->show_clouds = val;
        }

        ImGui::EndDisabled();

        /* Skybox (3D only) */
        {
            bool val = cfg->show_skybox;
            bool prev = val;
            DrawLayerCheckbox("Skybox", &val, ICON_FA_STAR,
                              "Show the starfield skybox.\n3D only.");
            if (val != prev)
                cfg->show_skybox = val;
        }

        /* Sun (3D only) + inline sun-flare sub-toggle */
        {
            float row_avail = ImGui::GetContentRegionAvail().x;
            bool val = cfg->show_sun;
            bool prev = val;
            DrawLayerCheckbox("Sun", &val, ICON_FA_CIRCLE_RADIATION,
                              "Show the sun disc and its glow over the scene.\nIndependent of the Skybox.\n3D only.");
            if (val != prev)
                cfg->show_sun = val;

            /* right-aligned sun-flare toggle, disabled while the master is off */
            ImGui::BeginDisabled(!val);
            const float box_w = 26.0f;
            ImGui::SameLine(row_avail - box_w);
            bool flare = cfg->show_sun_flare;
            if (ImGui::Checkbox("##sun_flare", &flare))
                cfg->show_sun_flare = flare;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Draw the lens-flare chain around the sun.\n3D only.");
            ImGui::EndDisabled();
        }

        /* Ecliptic Frame (3D only) */
        {
            bool val = *ctx->is_ecliptic_frame;
            bool prev = val;
            DrawLayerCheckbox("Ecliptic Frame", &val, ICON_FA_ROTATE,
                              "Tilt the scene to the ecliptic plane.\n3D only.");
            if (val != prev)
            {
                *ctx->is_ecliptic_frame = val;
                cfg->show_ecliptic_frame = val;
            }
        }

        /* Moon Orbit */
        {
            bool val = ToolSettingGetBool(cfg, LAYERS_KEY_MOON_ORBIT, true);
            bool prev = val;
            DrawLayerCheckbox("Moon Orbit", &val, ICON_FA_MOON,
                              "Show the Moon's geocentric orbital path (3D only).");
            if (val != prev)
                ToolSettingSetBool(cfg, LAYERS_KEY_MOON_ORBIT, val);
        }

        ImGui::Unindent();
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

    int first_copy = 0, last_copy = 0;
    MapVisibleCopyRange(*sctx->camera2d, sctx->map_w, &first_copy, &last_copy);
    for (int k = first_copy; k <= last_copy; k++)
    {
        const float x_off = k * sctx->map_w;
        for (int i = 0; i < line_count; i++)
        {
            const int end = lines[i].start + lines[i].count;
            for (int p = lines[i].start; p + 1 < end; p++)
            {
                Vector2 a = MapDetailToWorld(points[p], sctx->map_w, sctx->map_h);
                Vector2 b = MapDetailToWorld(points[p + 1], sctx->map_w, sctx->map_h);
                if (fabsf(a.x - b.x) <= sctx->map_w * 0.45f)
                    DrawLineEx({a.x + x_off, a.y}, {b.x + x_off, b.y}, line_width, color);
            }
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
    const bool show_coast = cfg->show_coast_lines;
    const bool show_border = cfg->show_country_borders;
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
static void DrawMapGrid3D(const SceneContext *sctx, AppConfig *cfg)
{
    const int spacing = GRID_SPACINGS[GridSpacingIndex(cfg)];

    const float rot_rad = (float)((sctx->gmst_deg + sctx->earth_rotation_offset) * DEG2RAD);
    const float cr = cosf(rot_rad);
    const float sr = sinf(rot_rad);

    /* lift the grid just off the surface to avoid z-fighting the Earth mesh */
    const float r = (EARTH_RADIUS_KM + MAP_DETAIL_LIFT_KM) / DRAW_SCALE;

    Color thin_color = g_theme.ui.text;
    thin_color.a = (unsigned char)(thin_color.a * 0.15f);
    Color strong_color = g_theme.ui.text;
    strong_color.a = (unsigned char)(strong_color.a * 0.28f);

    /* segments per curve so the great-circle arcs render smoothly */
    const int segs = 90;

    auto submit_seg = [&](float lat0, float lon0, float lat1, float lon1, Color c) {
        const float la0 = lat0 * DEG2RAD, lo0 = lon0 * DEG2RAD;
        const float la1 = lat1 * DEG2RAD, lo1 = lon1 * DEG2RAD;
        const float cl0 = cosf(la0);
        const Vector3 a = { cl0 * cosf(lo0) * r, sinf(la0) * r, -cl0 * sinf(lo0) * r };
        const float cl1 = cosf(la1);
        const Vector3 b = { cl1 * cosf(lo1) * r, sinf(la1) * r, -cl1 * sinf(lo1) * r };
        rlColor4ub(c.r, c.g, c.b, c.a);
        rlVertex3f(a.x * cr + a.z * sr, a.y, -a.x * sr + a.z * cr);
        rlVertex3f(b.x * cr + b.z * sr, b.y, -b.x * sr + b.z * cr);
    };

    rlDrawRenderBatchActive();
    rlBegin(RL_LINES);

    /* longitude lines (meridians): constant lon, lat from -90..90 */
    const int lon_max = (179 / spacing) * spacing;
    for (int lon = -lon_max; lon <= lon_max; lon += spacing)
    {
        const Color c = (lon == 0) ? strong_color : thin_color;
        for (int i = 0; i < segs; i++)
        {
            const float lat_a = -90.0f + (180.0f * (float)i) / (float)segs;
            const float lat_b = -90.0f + (180.0f * (float)(i + 1)) / (float)segs;
            submit_seg(lat_a, (float)lon, lat_b, (float)lon, c);
        }
    }

    for (int i = 0; i < segs; i++)
    {
        const float lat_a = -90.0f + (180.0f * (float)i) / (float)segs;
        const float lat_b = -90.0f + (180.0f * (float)(i + 1)) / (float)segs;
        submit_seg(lat_a, 180.0f, lat_b, 180.0f, thin_color);
    }

    /* latitude lines (parallels): constant lat, lon from -180..180 */
    const int lat_max = (89 / spacing) * spacing;
    for (int lat = -lat_max; lat <= lat_max; lat += spacing)
    {
        const Color c = (lat == 0) ? strong_color : thin_color;
        for (int i = 0; i < segs; i++)
        {
            const float lon_a = -180.0f + (360.0f * (float)i) / (float)segs;
            const float lon_b = -180.0f + (360.0f * (float)(i + 1)) / (float)segs;
            submit_seg((float)lat, lon_a, (float)lat, lon_b, c);
        }
    }

    rlEnd();
    rlDrawRenderBatchActive();
}

/* -- Moon orbit ------------------------------------------------------------ */

static void DrawMoonOrbit3D(const SceneContext *sctx, AppConfig *cfg)
{
    Vector3 pts[MOON_ORBIT_SAMPLES];
    calculate_moon_orbit(sctx->current_epoch, pts, MOON_ORBIT_SAMPLES);

    Color col = g_theme.world.orbit;
    col.a = 200;

    const float earth_r = sctx->draw_earth_radius;
    const Vector3 cam_pos = sctx->camera3d ? sctx->camera3d->position : Vector3Zero();

    /* sample 0 is the Moon's current position, so phase advances along the direction of travel */
    const bool dir_on = ToolSettingGetBool(cfg, LAYERS_KEY_ORBIT_DIRECTION, true);
    const bool dir_anim = dir_on && ToolSettingGetBool(cfg, LAYERS_KEY_ORBIT_DIRECTION_ANIM, false);
    const double dir_time = GetTime();

    rlDrawRenderBatchActive();
    rlSetLineWidth(1.5f);
    rlBegin(RL_LINES);
    for (int i = 0; i < MOON_ORBIT_SAMPLES; i++)
    {
        Vector3 a = Vector3Scale(pts[i], 1.0f / DRAW_SCALE);
        Vector3 b = Vector3Scale(pts[(i + 1) % MOON_ORBIT_SAMPLES], 1.0f / DRAW_SCALE);

        if (IsOrbitSegmentOccluded(cam_pos, a, b, earth_r))
            continue;

        unsigned char alpha = col.a;
        if (dir_on)
        {
            const double phase = (double)i / (double)MOON_ORBIT_SAMPLES;
            alpha = (unsigned char)(col.a * OrbitDirectionIntensity(phase, dir_anim, dir_time));
        }
        rlColor4ub(col.r, col.g, col.b, alpha);
        rlVertex3f(a.x, a.y, a.z);
        rlVertex3f(b.x, b.y, b.z);
    }
    rlEnd();
    rlSetLineWidth(1.0f);
    rlDrawRenderBatchActive();
}

void DrawSceneLayers(SceneContext *sctx, AppConfig *cfg)
{
    if (sctx->is_2d_view && sctx->camera2d)
    {
        const float zoom = fmaxf(sctx->camera2d->zoom, 0.10f);

        if (cfg->show_coast_lines)
            DrawMapDetailLines(sctx, MAP_COAST_POINTS, MAP_COAST_LINES, MAP_COAST_LINE_COUNT, 1.0f / zoom, 0.5f);

        if (cfg->show_country_borders)
            DrawMapDetailLines(sctx, MAP_BORDER_POINTS, MAP_BORDER_LINES, MAP_BORDER_LINE_COUNT, 0.8f / zoom, 0.20f);
    }
    else if (!sctx->is_2d_view)
    {
        DrawMapDetailLines3D(sctx, cfg);
        if (cfg->show_latlon_grid)
            DrawMapGrid3D(sctx, cfg);
    }

    if (!sctx->is_2d_view && ToolSettingGetBool(cfg, LAYERS_KEY_MOON_ORBIT, true))
        DrawMoonOrbit3D(sctx, cfg);

    if (sctx->is_2d_view && sctx->camera2d && cfg->show_latlon_grid)
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

        int first_copy = 0, last_copy = 0;
        MapVisibleCopyRange(*sctx->camera2d, sctx->map_w, &first_copy, &last_copy);

        for (int k = first_copy; k <= last_copy; k++)
        {
            for (int lon = -180 + spacing; lon <= 180; lon += spacing)
            {
                const float x = ((float)lon / 360.0f) * sctx->map_w + k * sctx->map_w;
                const bool prime_meridian = lon == 0;
                DrawLineEx(
                    {x, -sctx->map_h * 0.5f},
                    {x, sctx->map_h * 0.5f},
                    prime_meridian ? strong_width : thin_width,
                    prime_meridian ? strong_color : thin_color);
            }
        }

        const float grid_min_x = (first_copy - 0.5f) * sctx->map_w;
        const float grid_max_x = (last_copy + 0.5f) * sctx->map_w;
        const int lat_max = (89 / spacing) * spacing;
        for (int lat = -lat_max; lat <= lat_max; lat += spacing)
        {
            const float y = -((float)lat / 180.0f) * sctx->map_h;
            const bool equator = lat == 0;
            DrawLineEx(
                {grid_min_x, y},
                {grid_max_x, y},
                equator ? strong_width : thin_width,
                equator ? strong_color : thin_color);
        }
    }

    if (!ImGui::GetCurrentContext())
        return;

    if (ImGui::GetIO().WantTextInput || !IsKeyPressed(KEY_H))
        return;

    LayoutToggleCleanView(); /* sidebars, bottom time bar, menu bar and edge tabs */
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
    if (!cfg->show_latlon_grid)
        return;

    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    ImFont *font = ImGui::GetFont();
    if (!dl || !font)
        return;

    const float map_w = ctx->map_w;
    const float map_h = ctx->map_h;

    float vp_x, vp_y, vp_w, vp_h;
    LayoutGetViewportRect(&vp_x, &vp_y, &vp_w, &vp_h);
    const float left_edge = vp_x;
    const float right_edge = vp_x + vp_w;
    const float nav_h = g_layout.clean_view ? 0.0f : ImGui::GetFrameHeight();
    const float top_edge = fmaxf(vp_y, nav_h);
    const float screen_h = vp_y + vp_h;

    Vector2 vis_a = GetScreenToWorld2D((Vector2){vp_x, top_edge}, *ctx->camera2d);
    Vector2 vis_b = GetScreenToWorld2D((Vector2){vp_x + vp_w, vp_y + vp_h}, *ctx->camera2d);
    const float clip_min_x = fminf(vis_a.x, vis_b.x);
    const float clip_max_x = fmaxf(vis_a.x, vis_b.x);
    const float clip_min_y = fmaxf(fminf(vis_a.y, vis_b.y), -map_h * 0.5f);
    const float clip_max_y = fminf(fmaxf(vis_a.y, vis_b.y), map_h * 0.5f);
    if (clip_min_x >= clip_max_x || clip_min_y >= clip_max_y)
        return;

    const int spacing = GRID_SPACINGS[GridSpacingIndex(cfg)];

    const float font_size = ImGui::GetFontSize() * 0.8f;
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
        /* cull when the label would poke into the nav bar band */
        if (pos.y < nav_h || pos.y > screen_h)
            continue;

        dl->AddText(font, font_size, ImVec2(pos.x + 1.0f, pos.y + 1.0f), shadow_col, buf);
        dl->AddText(font, font_size, pos, col, buf);
    }

    /* longitude labels along the top edge of each visible wrap copy */
    int first_copy = 0, last_copy = 0;
    MapVisibleCopyRange(*ctx->camera2d, map_w, &first_copy, &last_copy);
    for (int k = first_copy; k <= last_copy; k++)
    for (int lon = -180 + spacing; lon <= 180; lon += spacing)
    {
        const float x = ((float)lon / 360.0f) * map_w + k * map_w;
        if (x < clip_min_x || x > clip_max_x)
            continue;

        if (lon == 180)
            snprintf(buf, sizeof(buf), "180°");
        else if (lon > 0)
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
