/*
 * tools_registry.cpp - The single registry of all tool panels.
 *
 * To add a new tool:
 *   1. Write a draw function (see the tool_*.cpp files for examples).
 *   2. Add a PanelId entry in tools_registry.h.
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

const PanelDef g_panel_defs[PANEL_COUNT] = {
    { PANEL_SAT_MGR,      "Satellite Manager",  ICON_FA_SATELLITE,       PANEL_CAT_CORE,       SIDEBAR_LEFT,  true,  DrawPanelSatMgr },
    { PANEL_DATA_SOURCES, "Data Sources",        ICON_FA_DATABASE,        PANEL_CAT_CORE,       SIDEBAR_LEFT,  true,  DrawPanelDataSources },
    { PANEL_LAYERS,       "Layers",              ICON_FA_LAYER_GROUP,     PANEL_CAT_CORE,       SIDEBAR_LEFT,  true,  DrawPanelLayers },
    { PANEL_SCOPE,        "Scope",               ICON_FA_CROSSHAIRS,      PANEL_CAT_SCIENTIFIC, SIDEBAR_LEFT,  false, DrawPanelScope },
    { PANEL_ROTATOR,      "Rotator Control",     ICON_FA_TURN_UP,         PANEL_CAT_CORE,       SIDEBAR_LEFT,  false, DrawPanelRotator },
    { PANEL_SAT_INFO,     "Satellite Info",      ICON_FA_CIRCLE_INFO,     PANEL_CAT_INSPECTOR,  SIDEBAR_RIGHT, true,  DrawPanelSatInfo },
    { PANEL_PASSES,       "Satellite Passes",    ICON_FA_ROUTE,           PANEL_CAT_SCIENTIFIC, SIDEBAR_RIGHT, false, DrawPanelPasses },
    { PANEL_POLAR_PLOT,   "Polar Plot",          ICON_FA_COMPASS,         PANEL_CAT_SCIENTIFIC, SIDEBAR_RIGHT, false, DrawPanelPolarPlot },
    { PANEL_DOPPLER,      "Doppler Analysis",    ICON_FA_TOWER_BROADCAST, PANEL_CAT_SCIENTIFIC, SIDEBAR_RIGHT, false, DrawPanelDoppler },
    { PANEL_LOG,          "Log",                 ICON_FA_LIST,            PANEL_CAT_SCIENTIFIC, SIDEBAR_RIGHT, false, DrawPanelLog },
};

const int g_panel_count = PANEL_COUNT;
