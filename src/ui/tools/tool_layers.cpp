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

void DrawPanelLayers(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;

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

        /* Earth Texture (master), off = plain black body */
        bool earth_texture_on = cfg->show_earth_texture;
        {
            bool val = earth_texture_on;
            bool prev = val;
            DrawLayerCheckbox("Earth Texture", &val, ICON_FA_GLOBE,
                              "Show the Earth surface texture (off = plain black).\n2D and 3D.");
            if (val != prev)
            {
                earth_texture_on = val;
                cfg->show_earth_texture = val;
            }
        }

        /* sub-controls for the earth texture (not indented; disabled and forced off while the master is off) */
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
    if (!cfg->show_latlon_grid)
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
