#ifndef TOOLS_REGISTRY_H
#define TOOLS_REGISTRY_H

#include "core/config.h"
#include "ui/ui.h"
#include "tools_scene.h"

/**
 * @file tools_registry.h
 * @brief Central registry for all tool panels.
 *
 * A "tool" is a sidebar panel. To add a new tool you:
 *   1. Write a draw function:  void DrawPanelMyTool(UIContext *ctx, AppConfig *cfg)
 *   2. Add one row to g_panel_defs in tools_registry.cpp and one entry to the
 *      PanelId enum in core/types.h.
 *
 * The Tools modal, enable/disable persistence, left/right placement, and the
 * sidebar renderer all pick the tool up automatically because they iterate
 * g_panel_defs.
 *
 * A tool may optionally also provide a draw_scene callback (see tools_scene.h)
 * to draw into the 3D world / 2D map. The render loop calls every registered
 * draw_scene hook each frame.
 */

/* panel categories used by the Tools modal */
typedef enum
{
    PANEL_CAT_CORE = 0,     /* core functions                */
    PANEL_CAT_EXTRA,        /* extra tools                   */
    PANEL_CAT_DEBUG         /* debug tools                   */
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
    bool default_enabled;    /* enabled in Tools dropdown on first run */
    void (*draw_content)(UIContext *ctx, AppConfig *cfg); /* body renderer */
    void (*draw_scene)(SceneContext *sctx, AppConfig *cfg); /* optional scene hook */
} PanelDef;

/* the single registry table (defined in tools_registry.cpp) */
extern const PanelDef g_panel_defs[PANEL_COUNT];
extern const int g_panel_count;

#endif /* TOOLS_REGISTRY_H */
