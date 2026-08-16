#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H

#include "core/config.h"
#include "core/types.h"
#include "ui.h"

/**
 * @file ui_layout.h
 * @brief Sidebar/panel workspace layout for TLEscope
 *
 * Implements the three-section workspace: a left sidebar (actions), a
 * transparent center canvas (simulation), and a right sidebar (inspector).
 * Sidebars are resizable, snap-hide to the screen edge (pull-tab to
 * restore), and contain reorderable accordion panels whose arrangement and
 * open/closed state are persisted to settings.json.
 */

/* -- Panel identity -------------------------------------------------------- */

typedef enum
{
    PANEL_SAT_MGR = 0,      /* Satellite Manager            -> left  */
    PANEL_DATA_SOURCES,     /* Data Sources                 -> left  */
    PANEL_TIME_CTRL,        /* Time Control                 -> left  */
    PANEL_SCOPE,            /* Scope                        -> left  */
    PANEL_ROTATOR,          /* Rotator Control              -> left  */
    PANEL_SAT_INFO,         /* Satellite Info (inspector)   -> right */
    PANEL_PASSES,           /* Satellite Passes             -> right */
    PANEL_POLAR_PLOT,       /* Polar Plot                   -> right */
    PANEL_DOPPLER,          /* Doppler Analysis             -> right */
    PANEL_LOG,              /* Log                          -> right */
    PANEL_COUNT
} PanelId;

/* panel categories used by the Tools dropdown */
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

/* -- Runtime layout state -------------------------------------------------- */

typedef struct
{
    /* sidebar geometry */
    float left_width;
    float right_width;
    bool left_visible;
    bool right_visible;
    bool left_hidden;        /* snap-hidden (pull-tab shown)  */
    bool right_hidden;
    float left_restore_width;  /* width to restore after un-hide */
    float right_restore_width;

    /* panel order arrays (PanelId values, -1 = unused slot) */
    int left_order[MAX_LEFT_PANELS];
    int right_order[MAX_RIGHT_PANELS];

    /* open/closed state indexed by PanelId */
    bool panel_open[PANEL_COUNT];

    /* drag-reorder runtime state */
    int drag_panel;          /* PanelId currently dragged, -1 = none */
    bool drag_is_left;
    int drag_target;         /* insertion index while dragging */

    /* settings modal */
    bool settings_open;

    /* bottom bar visibility (View menu) */
    bool show_bottom_bar;
} UILayoutState;

/* -- Globals --------------------------------------------------------------- */

extern UILayoutState g_layout;
extern const PanelDef g_panel_defs[PANEL_COUNT];

/* -- API ------------------------------------------------------------------- */

/** initialise default layout (first-run state) */
void LayoutInitDefaults(void);

/** apply persisted layout onto runtime state */
void LayoutApplyPersist(const UILayoutPersist *p);

/** copy runtime layout into the persist struct */
void LayoutFillPersist(UILayoutPersist *p);

/** draw the top navigation bar (menus + settings button) */
void DrawNavBar(UIContext *ctx, AppConfig *cfg);

/** draw both sidebars + panel accordions */
void DrawUILayout(UIContext *ctx, AppConfig *cfg);

/** draw the settings modal (centered, dimmed/blurred background) */
void DrawSettingsModal(UIContext *ctx, AppConfig *cfg);

/* panel visibility helpers (used by menus, shortcuts, other dialogs) */
void LayoutTogglePanel(PanelId id);
void LayoutOpenPanel(PanelId id);
void LayoutClosePanel(PanelId id);
bool LayoutIsPanelOpen(PanelId id);

/* settings modal helpers */
bool LayoutSettingsOpen(void);
void LayoutOpenSettings(void);
void LayoutCloseSettings(void);

/* bottom bar visibility */
bool LayoutBottomBarVisible(void);
void LayoutSetBottomBarVisible(bool visible);

/* sidebar helpers used by other modules */
bool LayoutSidebarVisible(SidebarSide side);
void LayoutSetSidebarVisible(SidebarSide side, bool visible);
SidebarSide PanelSide(PanelId id);

#endif /* UI_LAYOUT_H */
