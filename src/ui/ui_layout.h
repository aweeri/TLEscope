#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H

#include "core/config.h"
#include "core/types.h"
#include "ui.h"
#include "tools/tools_registry.h"

/**
 * @file ui_layout.h
 * @brief Sidebar/panel workspace layout for TLEscope
 *
 * Implements the three-section workspace: a left sidebar (actions), a
 * transparent center canvas (simulation), and a right sidebar (inspector).
 * Sidebars are resizable, snap-hide to the screen edge (pull-tab to
 * restore), and contain reorderable accordion panels whose arrangement and
 * open/closed state are persisted to settings.json.
 *
 * Panel identity (PanelId, PanelDef, the g_panel_defs registry) lives in
 * tools/tools_registry.h — see that file to add a new tool.
 */

/* -- Runtime layout state -------------------------------------------------- */

/* a screen-space rectangle captured during the layout pass; valid=false when not drawn this frame */
typedef struct { float x0, y0, x1, y1; bool valid; } UIRect;

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
    int left_order[MAX_PANELS];
    int right_order[MAX_PANELS];

    /* open/closed state indexed by PanelId */
    bool panel_open[PANEL_COUNT];

    /* panel enabled (completely shown/hidden in sidebar) indexed by PanelId */
    bool panel_enabled[PANEL_COUNT];

    /* screen-space rects captured each frame during the layout pass (valid=false when not drawn) */
    UIRect panel_rect[PANEL_COUNT];         /* panel band (header top .. content bottom) */
    UIRect panel_header_rect[PANEL_COUNT];  /* just the clickable title bar */
    UIRect sidebar_rect[2];                 /* [0]=left, [1]=right inner sidebar rect */
    UIRect notch_rect[2];                   /* [0]=left, [1]=right edge notch tab */
    UIRect tools_btn_rect;                  /* top-bar "Tools" menu item */
    UIRect settings_btn_rect;               /* top-bar "Settings" menu item */
    UIRect view_toggle_rect;                /* top-bar 2D/3D view switch    */
    UIRect rotator_nav_rect;                /* nav-bar rotator chip          */

    /* drag-reorder runtime state */
    int drag_panel;          /* PanelId currently dragged, -1 = none */
    bool drag_is_left;
    int drag_target;         /* insertion index while dragging */

    /* settings modal */
    bool settings_open;

    /* tools modal */
    bool tools_open;

    /* most-recently-activated tool panel; -1 = none */
    int last_active_panel;

    /* H key clean view: every chrome element hidden (sidebars, bottom bar, nav bar, edge notches) */
    bool clean_view;

    /* bottom bar visibility (View menu) */
    bool show_bottom_bar;

    /* rendered rect of the bottom-bar notch (ImGui coords), captured each frame by DrawBottomBar() */
    float bottom_bar_x;        /* left edge of the notch  */
    float bottom_bar_w;        /* width of the notch      */
    float bottom_bar_top;      /* top edge of the notch   */
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

/** draw the tools modal (manage tool enable/disable + sidebar placement) */
void DrawToolsModal(UIContext *ctx, AppConfig *cfg);

/* panel visibility helpers (used by menus, shortcuts, other dialogs) */
void LayoutTogglePanel(PanelId id);
void LayoutOpenPanel(PanelId id);
bool LayoutIsPanelOpen(PanelId id);

bool LayoutCloseActivePanel(void);

int LayoutGetActivePanel(void);
void LayoutSetPanelSide(PanelId id, SidebarSide side);
SidebarSide LayoutPanelCurrentSide(PanelId id);

/* settings modal helpers */
bool LayoutSettingsOpen(void);
void LayoutOpenSettings(void);

/* tools modal helpers */
bool LayoutToolsOpen(void);
void LayoutOpenTools(void);
void LayoutCloseTools(void);

/* bottom bar visibility */
bool LayoutBottomBarVisible(void);
void LayoutSetBottomBarVisible(bool visible);

/* H key clean view: hide all chrome, or restore what was visible before */
void LayoutToggleCleanView(void);

/* sidebar helpers used by other modules */
bool LayoutSidebarVisible(SidebarSide side);
void LayoutSetSidebarVisible(SidebarSide side, bool visible);

/* central canvas rect (logical screen coords); any out pointer may be NULL */
void LayoutGetViewportRect(float *x, float *y, float *w, float *h);

/* on-screen element rects captured during the layout pass (for coachmarks/hint system).
 * Each returns out->valid and copies *out = {0} (valid=false) when the element was
 * not drawn this frame. */
bool LayoutGetPanelRect(PanelId id, UIRect *out);
bool LayoutGetPanelHeaderRect(PanelId id, UIRect *out);
bool LayoutGetSidebarRect(SidebarSide side, UIRect *out);
bool LayoutGetNotchRect(SidebarSide side, UIRect *out);
bool LayoutGetToolsButtonRect(UIRect *out);
bool LayoutGetSettingsButtonRect(UIRect *out);
bool LayoutGetViewToggleRect(UIRect *out);
bool LayoutGetRotatorNavRect(UIRect *out);

#endif /* UI_LAYOUT_H */
