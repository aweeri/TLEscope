#include "tools_registry.h"
#include "tools_common.h"
#include "tools.h"

#include "IconsFontAwesome6.h"

/* -- Panel registry -------------------------------------------------------- */
const PanelDef g_panel_defs[PANEL_COUNT] = {
    { PANEL_SAT_MGR,      "Satellite Manager",  ICON_FA_SATELLITE,       PANEL_CAT_CORE,  SIDEBAR_LEFT,  true,  true,  DrawPanelSatMgr },
    { PANEL_DATA_SOURCES, "Data Sources",        ICON_FA_DATABASE,        PANEL_CAT_CORE,  SIDEBAR_LEFT,  true,  true,  DrawPanelDataSources },
    { PANEL_LAYERS,       "Layers",              ICON_FA_LAYER_GROUP,     PANEL_CAT_CORE,  SIDEBAR_LEFT,  true,  true,  DrawPanelLayers, DrawSceneLayers },
    // retired panel, row kept for now for enum stability only
    { PANEL_ROTATOR,      "Rotator Control",     ICON_FA_TURN_UP,         PANEL_CAT_EXTRA, SIDEBAR_LEFT,  false, false, NULL, NULL, true },
    { PANEL_SAT_INFO,     "Satellite Info",      ICON_FA_CIRCLE_INFO,     PANEL_CAT_CORE,  SIDEBAR_RIGHT, true,  true,  DrawPanelSatInfo },
    { PANEL_TRANSIT,      "Transit Tracking",    ICON_FA_ROUTE,           PANEL_CAT_CORE,  SIDEBAR_RIGHT, false, true,  DrawPanelTransit, DrawSceneTransit, false },
    /* the polar plot is now folded into the Transit Tracking panel */
    { PANEL_POLAR_PLOT,   "Polar Plot",          ICON_FA_COMPASS,         PANEL_CAT_CORE,  SIDEBAR_RIGHT, false, true,  DrawPanelPolarPlot, NULL, true },
    { PANEL_LOG,          "Log",                 ICON_FA_LIST,            PANEL_CAT_DEBUG, SIDEBAR_RIGHT, false, false, DrawPanelLog },
    { PANEL_TRXDB,        "TRXDB NORAD TEST",    ICON_FA_SATELLITE_DISH,  PANEL_CAT_EXTRA, SIDEBAR_RIGHT, false, false, DrawPanelTrxdb },
    { PANEL_EXAMPLE,      "Example Tool",        ICON_FA_PUZZLE_PIECE,    PANEL_CAT_EXTRA, SIDEBAR_RIGHT, true,  false, DrawPanelExample },
};
