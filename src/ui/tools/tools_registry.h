#ifndef TOOLS_REGISTRY_H
#define TOOLS_REGISTRY_H

#include "core/config.h"
#include "ui/ui.h"

/**
 * @file tools_registry.h
 * @brief Central registry for all tool panels.
 *
 * A "tool" is a sidebar panel. To add a new tool you:
 *   1. Write a draw function:  void DrawPanelMyTool(UIContext *ctx, AppConfig *cfg)
 *   2. Add one row to g_panel_defs in tools_registry.cpp and one entry to the
 *      PanelId enum below.
 *
 * The Tools modal, enable/disable persistence, left/right placement, and the
 * sidebar renderer all pick the tool up automatically because they iterate
 * g_panel_defs.
 */

/* -- Panel identity -------------------------------------------------------- */

typedef enum
{
    PANEL_SAT_MGR = 0,      /* Satellite Manager            -> left  */
    PANEL_DATA_SOURCES,     /* Data Sources                 -> left  */
    PANEL_LAYERS,           /* Layer Controls               -> left  */
    PANEL_SCOPE,            /* Scope                        -> left  */
    PANEL_ROTATOR,          /* Rotator Control              -> left  */
    PANEL_SAT_INFO,         /* Satellite Info (inspector)   -> right */
    PANEL_PASSES,           /* Satellite Passes             -> right */
    PANEL_POLAR_PLOT,       /* Polar Plot                   -> right */
    PANEL_DOPPLER,          /* Doppler Analysis             -> right */
    PANEL_LOG,              /* Log                          -> right */
    PANEL_COUNT
} PanelId;

/* panel categories used by the Tools modal */
typedef enum
{
    PANEL_CAT_CORE = 0,     /* core functions                */
    PANEL_CAT_SCIENTIFIC,   /* scientific tools              */
    PANEL_CAT_INSPECTOR     /* inspector / information       */
} PanelCategory;

/* which sidebar a panel lives in */
typedef enum
{
    SIDEBAR_NONE = 0,
    SIDEBAR_LEFT,
    SIDEBAR_RIGHT
} SidebarSide;

/* -- Panel registry -------------------------------------------------------- */

typedef struct
{
    PanelId id;
    const char *title;       /* display title                 */
    const char *icon;        /* FontAwesome icon              */
    PanelCategory category;
    SidebarSide default_side; /* default sidebar assignment   */
    bool default_open;       /* open on first run             */
    void (*draw_content)(UIContext *ctx, AppConfig *cfg); /* body renderer */
} PanelDef;

/* the single registry table (defined in tools_registry.cpp) */
extern const PanelDef g_panel_defs[PANEL_COUNT];
extern const int g_panel_count;

#endif /* TOOLS_REGISTRY_H */
