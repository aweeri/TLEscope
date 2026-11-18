#ifndef TOOLS_REGISTRY_H
#define TOOLS_REGISTRY_H

#include "core/config.h"
#include "ui/ui.h"
#include "tools_scene.h"

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
    bool hidden;
} PanelDef;

/* the single registry table (defined in tools_registry.cpp) */
extern const PanelDef g_panel_defs[PANEL_COUNT];

#endif /* TOOLS_REGISTRY_H */
