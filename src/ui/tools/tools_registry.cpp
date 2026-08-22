/*
 * tools_registry.cpp - The single registry of all tool panels.
 *
 * To add a new tool:
 *   1. Write a draw function (see the tool_*.cpp files for examples).
 *   2. Add a PanelId entry in core/types.h.
 *   3. Add one row to g_panel_defs below.
 *
 * The Tools modal, enable/disable persistence, left/right placement, and the
 * sidebar renderer all pick the tool up automatically.
 */

#include "tools_registry.h"
#include "tools_common.h"
#include "tools.h"

#include "IconsFontAwesome6.h"

/* -- Panel registry -------------------------------------------------------- */
/*
 * Each row is a PanelDef with the following fields, in order:
 *
 *   id             PanelId enum value (must be unique, matches core/types.h).
 *   title          Display name shown in the sidebar header and Tools modal.
 *   icon           FontAwesome glyph shown next to the title (see
 *                  IconsFontAwesome6.h). Pick one that matches the tool.
 *   category       PANEL_CAT_CORE / PANEL_CAT_EXTRA / PANEL_CAT_DEBUG.
 *                  Controls which section the tool appears under in the
 *                  Tools modal (Core Functions / Extra Tools / Debug).
 *   default_side   SIDEBAR_LEFT or SIDEBAR_RIGHT - where the panel lives on
 *                  first run. The user can move it later (drag or Tools modal).
 *   default_open   true  = panel is expanded (content visible) on first run.
 *                  false = panel is collapsed to just its header.
 *   default_enabled true  = panel is shown in the sidebar on first run.
 *                  false = hidden until the user enables it in the Tools modal.
 *                  New tools should ship disabled (false) so they don't crowd
 *                  the sidebar until the user opts in.
 *   draw_content   The panel body renderer: void f(UIContext*, AppConfig*).
 *   draw_scene     Optional scene hook: void f(SceneContext*, AppConfig*).
 *                  NULL if the tool draws nothing into the 3D/2D scene.
 */
const PanelDef g_panel_defs[PANEL_COUNT] = {
    { PANEL_SAT_MGR,      "Satellite Manager",  ICON_FA_SATELLITE,       PANEL_CAT_CORE,  SIDEBAR_LEFT,  true,  true,  DrawPanelSatMgr },
    { PANEL_DATA_SOURCES, "Data Sources",        ICON_FA_DATABASE,        PANEL_CAT_CORE,  SIDEBAR_LEFT,  true,  true,  DrawPanelDataSources },
    { PANEL_LAYERS,       "Layers",              ICON_FA_LAYER_GROUP,     PANEL_CAT_CORE,  SIDEBAR_LEFT,  true,  true,  DrawPanelLayers },
    { PANEL_SCOPE,        "Scope",               ICON_FA_CROSSHAIRS,      PANEL_CAT_EXTRA, SIDEBAR_LEFT,  false, false, DrawPanelScope },
    { PANEL_ROTATOR,      "Rotator Control",     ICON_FA_TURN_UP,         PANEL_CAT_EXTRA, SIDEBAR_LEFT,  false, false, DrawPanelRotator },
    { PANEL_SAT_INFO,     "Satellite Info",      ICON_FA_CIRCLE_INFO,     PANEL_CAT_CORE,  SIDEBAR_RIGHT, true,  true,  DrawPanelSatInfo },
    { PANEL_PASSES,       "Satellite Passes",    ICON_FA_ROUTE,           PANEL_CAT_CORE,  SIDEBAR_RIGHT, false, true,  DrawPanelPasses },
    { PANEL_POLAR_PLOT,   "Polar Plot",          ICON_FA_COMPASS,         PANEL_CAT_CORE,  SIDEBAR_RIGHT, false, true,  DrawPanelPolarPlot },
    { PANEL_DOPPLER,      "Doppler Analysis",    ICON_FA_TOWER_BROADCAST, PANEL_CAT_EXTRA, SIDEBAR_RIGHT, false, false, DrawPanelDoppler },
    { PANEL_LOG,          "Log",                 ICON_FA_LIST,            PANEL_CAT_DEBUG, SIDEBAR_RIGHT, false, false, DrawPanelLog },
    { PANEL_TRXDB,        "TRXDB NORAD TEST",    ICON_FA_SATELLITE_DISH,  PANEL_CAT_EXTRA, SIDEBAR_RIGHT, false, false, DrawPanelTrxdb },
};

const int g_panel_count = PANEL_COUNT;
