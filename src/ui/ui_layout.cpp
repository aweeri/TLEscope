/*
 * ui_layout.cpp - Sidebar / panel workspace layout engine
 *
 * Implements the three-section workspace: a left sidebar (actions), a
 * transparent center canvas (simulation), and a right sidebar (inspector).
 * Sidebars are resizable, snap-hide to the screen edge (pull-tab to
 * restore), and contain reorderable accordion panels.
 */

#include "ui_layout.h"
#include "tools/tools.h"
#include "tools/tools_common.h"
#include "tools/tools_settings.h"
#include "notifications.h"
#include "core/astro.h"
#include "core/theme.h"
#include "core/config.h"
#include "core/location.h"
#include "data/curl_diagnostics.h"
#include "util/log.h"

#include <raylib.h>
#include <imgui.h>
#include <rlImGui.h>
#include "IconsFontAwesome6.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cfloat>
#include <algorithm>

/* -- Layout constants ------------------------------------------------------ */

static const float HANDLE_WIDTH   = 6.0f;   /* resize strip width          */
static const float NOTCH_W        = 26.0f;  /* show/hide notch width       */
static const float NOTCH_H        = 64.0f;  /* show/hide notch height      */
static const float MIN_SIDEBAR_W  = 280.0f; /* minimum sidebar width       */
static const float MAX_SIDEBAR_W  = 600.0f; /* maximum sidebar width       */
static const float DEF_SIDEBAR_W  = 320.0f; /* default sidebar width       */
static const float HANDLE_W       = 22.0f;  /* panel drag handle width     */

/* -- Height helpers -------------------------------------------------------- */

/** compute the available vertical space between the nav bar and the bottom of
 * the screen for a sidebar occupying [x0, x1).
 *
 * The bottom bar is a centered notch, not a full-width bar, so a sidebar only
 * needs to stop at the notch's top edge when it horizontally overlaps the
 * notch (narrow windows / very wide sidebars). Otherwise it extends all the
 * way to the bottom of the screen. Uses the notch's ACTUAL rendered rect
 * (captured by DrawBottomBar() each frame) rather than a hardcoded height. */
static float GetContentHeight(float x0, float x1)
{
    float nav_h = ImGui::GetFrameHeight();
    float display_h = ImGui::GetIO().DisplaySize.y;
    float bottom = display_h;

    if (g_layout.show_bottom_bar && g_layout.bottom_bar_top > 0.0f)
    {
        /* does this sidebar horizontally overlap the notch? */
        float notch_x0 = g_layout.bottom_bar_x;
        float notch_x1 = g_layout.bottom_bar_x + g_layout.bottom_bar_w;
        if (x1 > notch_x0 && x0 < notch_x1)
            bottom = g_layout.bottom_bar_top;
    }

    float h = bottom - nav_h;
    return (h < 50.0f) ? 50.0f : h;
}

/* -- Globals --------------------------------------------------------------- */

UILayoutState g_layout = {0};

/* -- Internal helpers ------------------------------------------------------ */

static void EnsureSidebar(SidebarSide side)
{
    if (side == SIDEBAR_LEFT)
    {
        if (g_layout.left_hidden) { g_layout.left_hidden = false; g_layout.left_visible = true; }
        else if (!g_layout.left_visible) g_layout.left_visible = true;
    }
    else if (side == SIDEBAR_RIGHT)
    {
        if (g_layout.right_hidden) { g_layout.right_hidden = false; g_layout.right_visible = true; }
        else if (!g_layout.right_visible) g_layout.right_visible = true;
    }
}

static void SnapHide(bool is_left)
{
    if (is_left)
    {
        g_layout.left_restore_width = (g_layout.left_width > 100.0f) ? g_layout.left_width : DEF_SIDEBAR_W;
        g_layout.left_hidden = true;
        g_layout.left_visible = false;
    }
    else
    {
        g_layout.right_restore_width = (g_layout.right_width > 100.0f) ? g_layout.right_width : DEF_SIDEBAR_W;
        g_layout.right_hidden = true;
        g_layout.right_visible = false;
    }
}

/* -- Initialisation / persistence ------------------------------------------ */

void LayoutInitDefaults(void)
{
    g_layout = UILayoutState{};
    g_layout.left_width  = DEF_SIDEBAR_W;
    g_layout.right_width = DEF_SIDEBAR_W;
    g_layout.left_visible = true;
    g_layout.right_visible = true;
    g_layout.left_restore_width  = DEF_SIDEBAR_W;
    g_layout.right_restore_width = DEF_SIDEBAR_W;
    g_layout.show_bottom_bar = true;
    g_layout.drag_panel = -1;
    g_layout.drag_target = -1;
    g_layout.bottom_bar_expanded = false;

    int li = 0, ri = 0;
    for (int i = 0; i < PANEL_COUNT; i++)
    {
        const PanelDef *def = &g_panel_defs[i];
        if (def->default_side == SIDEBAR_LEFT)
            g_layout.left_order[li++] = def->id;
        else
            g_layout.right_order[ri++] = def->id;
        g_layout.panel_open[def->id] = def->default_open;
        g_layout.panel_enabled[def->id] = def->default_enabled;
    }

    /* fill unused slots with -1 so no panel id is duplicated in the arrays */
    for (int i = li; i < MAX_PANELS; i++)
        g_layout.left_order[i] = -1;
    for (int i = ri; i < MAX_PANELS; i++)
        g_layout.right_order[i] = -1;
}

/** Sanitise a panel order array: remove invalid IDs and duplicates,
 *  filling remaining slots with -1. Returns the number of valid entries. */
static int SanitiseOrder(int *order, int max_slots)
{
    bool seen[PANEL_COUNT] = {false};
    int write = 0;
    for (int i = 0; i < max_slots; i++)
    {
        int pid = order[i];
        if (pid >= 0 && pid < PANEL_COUNT && !seen[pid])
        {
            seen[pid] = true;
            order[write++] = pid;
        }
    }
    for (int i = write; i < max_slots; i++)
        order[i] = -1;
    return write;
}

void LayoutApplyPersist(const UILayoutPersist *p)
{
    g_layout.left_width  = p->left_sidebar_width;
    g_layout.right_width = p->right_sidebar_width;
    g_layout.left_visible  = p->left_sidebar_visible;
    g_layout.right_visible = p->right_sidebar_visible;
    g_layout.left_hidden   = p->left_sidebar_hidden;
    g_layout.right_hidden  = p->right_sidebar_hidden;
    g_layout.left_restore_width  = p->left_restore_width;
    g_layout.right_restore_width = p->right_restore_width;

    for (int i = 0; i < MAX_PANELS; i++)
        g_layout.left_order[i] = p->left_panel_order[i];
    for (int i = 0; i < MAX_PANELS; i++)
        g_layout.right_order[i] = p->right_panel_order[i];

    /* sanitise: strip duplicates and invalid entries */
    SanitiseOrder(g_layout.left_order, MAX_PANELS);
    SanitiseOrder(g_layout.right_order, MAX_PANELS);

    for (int i = 0; i < MAX_PANELS; i++)
    {
        int pid = g_layout.left_order[i];
        if (pid >= 0 && pid < PANEL_COUNT)
            g_layout.panel_open[pid] = p->left_panel_open[i];
    }
    for (int i = 0; i < MAX_PANELS; i++)
    {
        int pid = g_layout.right_order[i];
        if (pid >= 0 && pid < PANEL_COUNT)
            g_layout.panel_open[pid] = p->right_panel_open[i];
    }

    /* restore panel enabled/disabled state (Tools dropdown) */
    for (int i = 0; i < PANEL_COUNT; i++)
        g_layout.panel_enabled[i] = p->panel_enabled[i];
}

void LayoutFillPersist(UILayoutPersist *p)
{
    p->left_sidebar_width  = g_layout.left_width;
    p->right_sidebar_width = g_layout.right_width;
    p->left_sidebar_visible  = g_layout.left_visible;
    p->right_sidebar_visible = g_layout.right_visible;
    p->left_sidebar_hidden   = g_layout.left_hidden;
    p->right_sidebar_hidden  = g_layout.right_hidden;
    p->left_restore_width  = g_layout.left_restore_width;
    p->right_restore_width = g_layout.right_restore_width;

    for (int i = 0; i < MAX_PANELS; i++)
        p->left_panel_order[i] = g_layout.left_order[i];
    for (int i = 0; i < MAX_PANELS; i++)
        p->right_panel_order[i] = g_layout.right_order[i];

    for (int i = 0; i < MAX_PANELS; i++)
    {
        int pid = g_layout.left_order[i];
        p->left_panel_open[i] = (pid >= 0 && pid < PANEL_COUNT) ? g_layout.panel_open[pid] : false;
    }
    for (int i = 0; i < MAX_PANELS; i++)
    {
        int pid = g_layout.right_order[i];
        p->right_panel_open[i] = (pid >= 0 && pid < PANEL_COUNT) ? g_layout.panel_open[pid] : false;
    }

    /* persist panel enabled/disabled state (Tools dropdown) */
    for (int i = 0; i < PANEL_COUNT; i++)
        p->panel_enabled[i] = g_layout.panel_enabled[i];
}

/* -- Panel visibility helpers ----------------------------------------------- */

void LayoutTogglePanel(PanelId id)
{
    if (id < 0 || id >= PANEL_COUNT) return;
    g_layout.panel_open[id] = !g_layout.panel_open[id];
    if (g_layout.panel_open[id]) EnsureSidebar(g_panel_defs[id].default_side);
}

void LayoutOpenPanel(PanelId id)
{
    if (id < 0 || id >= PANEL_COUNT) return;
    g_layout.panel_open[id] = true;
    EnsureSidebar(g_panel_defs[id].default_side);
}

/** move a panel to the given sidebar, appending it to that side's order.
 *  Removes it from the source side's order first. Each sidebar can hold up
 *  to MAX_PANELS panels, so a move never evicts another panel. */
void LayoutSetPanelSide(PanelId id, SidebarSide side)
{
    if (id < 0 || id >= PANEL_COUNT) return;
    if (side != SIDEBAR_LEFT && side != SIDEBAR_RIGHT) return;

    /* no-op if the panel is already on the requested side */
    if (LayoutPanelCurrentSide(id) == side) return;

    /* remove from both order arrays */
    for (int i = 0; i < MAX_PANELS; i++)
        if (g_layout.left_order[i] == id) g_layout.left_order[i] = -1;
    for (int i = 0; i < MAX_PANELS; i++)
        if (g_layout.right_order[i] == id) g_layout.right_order[i] = -1;

    /* compact each array (shift -1s to the end) */
    int *orders[2] = { g_layout.left_order, g_layout.right_order };
    for (int s = 0; s < 2; s++)
    {
        int w = 0;
        for (int i = 0; i < MAX_PANELS; i++)
            if (orders[s][i] >= 0) orders[s][w++] = orders[s][i];
        for (int i = w; i < MAX_PANELS; i++)
            orders[s][i] = -1;
    }

    /* append to the target side (there is always a free slot) */
    int *target = (side == SIDEBAR_LEFT) ? g_layout.left_order : g_layout.right_order;
    for (int i = 0; i < MAX_PANELS; i++)
    {
        if (target[i] < 0)
        {
            target[i] = id;
            break;
        }
    }

    EnsureSidebar(side);
}

/** return the sidebar a panel currently lives in (from the order arrays). */
SidebarSide LayoutPanelCurrentSide(PanelId id)
{
    if (id < 0 || id >= PANEL_COUNT) return SIDEBAR_NONE;
    for (int i = 0; i < MAX_PANELS; i++)
        if (g_layout.left_order[i] == id) return SIDEBAR_LEFT;
    for (int i = 0; i < MAX_PANELS; i++)
        if (g_layout.right_order[i] == id) return SIDEBAR_RIGHT;
    return SIDEBAR_NONE;
}

bool LayoutIsPanelOpen(PanelId id)
{
    return (id >= 0 && id < PANEL_COUNT) ? g_layout.panel_open[id] : false;
}

/* -- Sidebar visibility helpers --------------------------------------------- */

bool LayoutSidebarVisible(SidebarSide side)
{
    if (side == SIDEBAR_LEFT) return g_layout.left_visible && !g_layout.left_hidden;
    if (side == SIDEBAR_RIGHT) return g_layout.right_visible && !g_layout.right_hidden;
    return false;
}

void LayoutSetSidebarVisible(SidebarSide side, bool visible)
{
    if (side == SIDEBAR_LEFT) { g_layout.left_visible = visible; if (visible) g_layout.left_hidden = false; }
    if (side == SIDEBAR_RIGHT) { g_layout.right_visible = visible; if (visible) g_layout.right_hidden = false; }
}

/* -- Bottom bar helpers ----------------------------------------------------- */

bool LayoutBottomBarVisible(void) { return g_layout.show_bottom_bar; }
void LayoutSetBottomBarVisible(bool visible) { g_layout.show_bottom_bar = visible; }

/* -- Settings modal helpers ------------------------------------------------- */

bool LayoutSettingsOpen(void) { return g_layout.settings_open; }
void LayoutOpenSettings(void) { g_layout.settings_open = true; }

bool LayoutToolsOpen(void) { return g_layout.tools_open; }
void LayoutOpenTools(void) { g_layout.tools_open = true; }
void LayoutCloseTools(void) { g_layout.tools_open = false; }

/* -- Drag reorder data (per-frame) ----------------------------------------- */

typedef struct { PanelId id; float y0, y1; } HeaderSlot;
static HeaderSlot s_left_headers[MAX_PANELS];
static HeaderSlot s_right_headers[MAX_PANELS];
static int s_left_hc = 0, s_right_hc = 0;

/* ========================================================================== */
/*  Pull-tab                                                                  */
/* ========================================================================== */

/* ========================================================================== */
/*  Notch (show/hide tab)                                                     */
/* ========================================================================== */

/** Draw a small rounded notch at the edge of a hidden sidebar.
 *  Also draws a notch on the visible sidebar edge so the user can hide it. */
static void DrawNotch(bool is_left, float nav_h, float content_h, bool sidebar_visible)
{
    const float screen_w = ImGui::GetIO().DisplaySize.x;
    float notch_w = NOTCH_W, notch_h = NOTCH_H;


    float notch_y = nav_h + content_h * 0.25f - notch_h * 0.5f;
    const float notch_y_max = nav_h + content_h - notch_h; /* may be < nav_h for tiny content_h */
    notch_y = (notch_y < nav_h) ? nav_h : ((notch_y > notch_y_max) ? notch_y_max : notch_y);
    if (notch_y < nav_h) notch_y = nav_h; /* ensure lower bound always wins */

    float notch_x;
    if (sidebar_visible)
    {
        /* notch sits at the inner edge of the visible sidebar, mostly
         * sticking out into the canvas so it never covers panel controls.
         * Left: 4px overlap into the sidebar (the rest sticks out right).
         * Right: 4px overlap into the sidebar (the rest sticks out left). */
        float sidebar_edge = is_left ? g_layout.left_width : (screen_w - g_layout.right_width);
        notch_x = is_left ? (sidebar_edge - 4.0f) : (sidebar_edge - notch_w + 4.0f);
    }
    else
    {
        /* notch sits flush at the screen edge */
        notch_x = is_left ? 0.0f : (screen_w - notch_w);
    }

    /* smooth per-side hover/press animation (0 = idle, 1 = fully engaged) */
    static float s_notch_anim[2] = {0.0f, 0.0f};
    int side = is_left ? 0 : 1;

    /* while engaged the tab stretches away from its attached edge */
    float grow  = 4.0f * s_notch_anim[side];
    float tab_w = notch_w + grow;
    float win_x = is_left ? notch_x : (notch_x - grow);

    ImGui::SetNextWindowPos(ImVec2(win_x, notch_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(tab_w, notch_h), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    if (ImGui::Begin(is_left ? "##notch_left" : "##notch_right", NULL,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground))
    {
        /* absolute origin of the tab within the window (WindowPadding is 0) */
        const ImVec2 o = ImGui::GetCursorScreenPos();

        bool hovered = ImGui::IsWindowHovered();
        bool clicked = ImGui::InvisibleButton("##notch_btn", ImVec2(tab_w, notch_h));
        bool active  = ImGui::IsItemActive();

        /* ~0.1s animation toward the hover/press target */
        float dt = ImGui::GetIO().DeltaTime;
        if (dt > 0.1f) dt = 0.1f;
        float target = (hovered || active) ? 1.0f : 0.0f;
        s_notch_anim[side] += (target - s_notch_anim[side]) * (1.0f - std::exp(-dt / 0.06f));

        if (hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        ImDrawList *dl = ImGui::GetWindowDrawList();

        /* theme-aware colors (12.1): ui_primary fill, window_border border,
         * ui_accent hover blend, text_secondary icon. */
        auto lerp4 = [](const ImVec4 &a, const ImVec4 &b, float u) {
            return ImVec4(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u,
                          a.z + (b.z - a.z) * u, a.w + (b.w - a.w) * u);
        };
        float t = s_notch_anim[side];

        ImVec4 fill = lerp4(ThemeColor(g_theme.ui.surface),
                            ThemeColor(g_theme.ui.accent), 0.45f * t);
        fill.w = 0.85f + 0.15f * t;
        if (active) { fill.x *= 0.8f; fill.y *= 0.8f; fill.z *= 0.8f; fill.w = 0.98f; }

        ImVec4 bcol = ThemeColor(g_theme.ui.border);
        bcol.w = 0.70f + 0.30f * t;

        /* tab-like shape: only the outer (detached) edge is rounded */
        const float       rounding = 6.0f;
        const ImDrawFlags corners  = is_left ? ImDrawFlags_RoundCornersRight
                                             : ImDrawFlags_RoundCornersLeft;
        ImVec2 p0 = o;
        ImVec2 p1 = ImVec2(o.x + tab_w, o.y + notch_h);

        if (sidebar_visible)
        {
            float clip_x = is_left ? g_layout.left_width
                                   : (ImGui::GetIO().DisplaySize.x - g_layout.right_width);
            ImVec2 cmin = is_left ? ImVec2(clip_x, p0.y) : ImVec2(0.0f, p0.y);
            ImVec2 cmax = is_left ? ImVec2(FLT_MAX, p1.y) : ImVec2(clip_x, p1.y);
            dl->PushClipRect(cmin, cmax, true);
        }

        dl->AddRectFilled(p0, p1, ImGui::GetColorU32(fill), rounding, corners);
        /* the 1px stroke is centered on the path, so inset it by 0.5px to stop
         * the window clip rect trimming the outer half of the top/attached side */
        dl->AddRect(ImVec2(p0.x + 0.5f, p0.y + 0.5f), ImVec2(p1.x - 0.5f, p1.y - 0.5f),
                    ImGui::GetColorU32(bcol), rounding, corners, 1.0f);

        /* chevron icon: points inward when the sidebar is visible (collapse),
         * outward when it is hidden (expand). */
        const char *icon;
        if (sidebar_visible)
            icon = is_left ? ICON_FA_CHEVRON_LEFT : ICON_FA_CHEVRON_RIGHT;
        else
            icon = is_left ? ICON_FA_CHEVRON_RIGHT : ICON_FA_CHEVRON_LEFT;

        ImVec2 icon_sz = ImGui::CalcTextSize(icon);
        ImVec4 icon_col = lerp4(ThemeColor(g_theme.ui.text_dim),
                                ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.6f * t);
        icon_col.w = 0.80f + 0.20f * t;
        dl->AddText(ImVec2(o.x + (tab_w - icon_sz.x) * 0.5f,
                           o.y + (notch_h - icon_sz.y) * 0.5f),
                    ImGui::GetColorU32(icon_col), icon);

        if (sidebar_visible) dl->PopClipRect();

        if (clicked)
        {
            if (sidebar_visible)
            {
                /* hide the sidebar */
                if (is_left)
                {
                    g_layout.left_restore_width = (g_layout.left_width > 100.0f) ? g_layout.left_width : 320.0f;
                    g_layout.left_hidden = true;
                    g_layout.left_visible = false;
                }
                else
                {
                    g_layout.right_restore_width = (g_layout.right_width > 100.0f) ? g_layout.right_width : 320.0f;
                    g_layout.right_hidden = true;
                    g_layout.right_visible = false;
                }
            }
            else
            {
                /* show the sidebar */
                if (is_left)
                {
                    g_layout.left_width   = g_layout.left_restore_width;
                    g_layout.left_hidden  = false;
                    g_layout.left_visible = true;
                }
                else
                {
                    g_layout.right_width   = g_layout.right_restore_width;
                    g_layout.right_hidden  = false;
                    g_layout.right_visible = true;
                }
            }
        }

        if (hovered)
        {
            const char *tt = sidebar_visible ? (is_left ? "Hide left sidebar" : "Hide right sidebar")
                                             : (is_left ? "Show left sidebar" : "Show right sidebar");
            ImGui::SetTooltip("%s", tt);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

/* ========================================================================== */
/*  Resize strip                                                              */
/* ========================================================================== */

static void DrawResizeStrip(bool is_left, float nav_h, float content_h)
{
    float screen_w = (float)GetScreenWidth();
    float *width = is_left ? &g_layout.left_width : &g_layout.right_width;
    bool *hidden = is_left ? &g_layout.left_hidden : &g_layout.right_hidden;

    float strip_x = is_left ? *width : (screen_w - *width - HANDLE_WIDTH);

    ImGui::SetNextWindowPos(ImVec2(strip_x, nav_h), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(HANDLE_WIDTH, content_h), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    if (ImGui::Begin(is_left ? "##resize_left" : "##resize_right", NULL,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoBringToFrontOnFocus))
    {
        ImGui::InvisibleButton("##grip", ImVec2(HANDLE_WIDTH, content_h));

        bool hovered = ImGui::IsItemHovered();
        bool dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);

        if (hovered || dragging)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        /* visual: thin line at the boundary + highlighted strip when hovered */
        float boundary_x = is_left ? *width : (screen_w - *width);
        ImDrawList *dl = ImGui::GetWindowDrawList();

        Color line_theme = g_theme.ui.border;
        Color drag_theme = g_theme.ui.accent;

        if (dragging)
        {
            /* bright highlight during drag */
            dl->AddRectFilled(ImVec2(strip_x, nav_h),
                              ImVec2(strip_x + HANDLE_WIDTH, nav_h + content_h),
                              IM_COL32(drag_theme.r, drag_theme.g, drag_theme.b, 40));
        }
        dl->AddLine(ImVec2(boundary_x, nav_h), ImVec2(boundary_x, nav_h + content_h),
                    hovered || dragging ? IM_COL32(line_theme.r, line_theme.g, line_theme.b, 180)
                                        : IM_COL32(line_theme.r, line_theme.g, line_theme.b, 80),
                    hovered ? 2.0f : 1.0f);

        if (dragging)
        {
            float mouse_x = ImGui::GetIO().MousePos.x;

            if (is_left)
            {
                *width = fmaxf(MIN_SIDEBAR_W, fminf(MAX_SIDEBAR_W, mouse_x));
                /* deliberate snap-hide: only when the cursor is dragged fully
                 * past the screen edge (12.1) - not merely near it */
                if (mouse_x <= 0.0f) { SnapHide(is_left); ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(4); return; }
            }
            else
            {
                *width = fmaxf(MIN_SIDEBAR_W, fminf(MAX_SIDEBAR_W, screen_w - mouse_x));
                if (mouse_x >= screen_w) { SnapHide(is_left); ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(4); return; }
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

/* ========================================================================== */
/*  Accordion header + reorder                                                */
/* ========================================================================== */

static void DrawAccordionHeader(const PanelDef *def, bool *open, int order_idx, bool is_left)
{
    ImGui::PushID(def->id);

    float avail_w = ImGui::GetContentRegionAvail().x;
    float frame_h = ImGui::GetFrameHeight() + 2.0f;

    /* ---- clickable toggle area ----------------------------------------- */
    ImGui::InvisibleButton("##header", ImVec2(avail_w - HANDLE_W, frame_h));
    bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    bool hovered = ImGui::IsItemHovered();
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();

    /* background (theme-aware) */
    Color hdr_bg   = g_theme.ui.surface;
    Color hdr_hov  = ThemeHoverOf(g_theme.ui.surface, g_theme.ui.text);
    ImU32 bg = *open ? IM_COL32(hdr_bg.r, hdr_bg.g, hdr_bg.b, 200) : IM_COL32(hdr_bg.r, hdr_bg.g, hdr_bg.b, 160);
    if (hovered) bg = *open ? IM_COL32(hdr_hov.r, hdr_hov.g, hdr_hov.b, 220) : IM_COL32(hdr_hov.r, hdr_hov.g, hdr_hov.b, 180);
    ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, bg, 4.0f);

    /* icons: chevron + panel icon + title */
    const char *chev_down = ICON_FA_CHEVRON_DOWN;
    const char *chev_right = ICON_FA_CHEVRON_RIGHT;
    float chev_slot_w = std::max(ImGui::CalcTextSize(chev_down).x,
                                 ImGui::CalcTextSize(chev_right).x);
    const char *chev = *open ? chev_down : chev_right;
    ImVec2 chev_sz = ImGui::CalcTextSize(chev);
    ImVec2 icon_sz = ImGui::CalcTextSize(def->icon);
    float y = p0.y + (frame_h - chev_sz.y) * 0.5f;
    ImU32 text_col = IM_COL32(g_theme.ui.text.r, g_theme.ui.text.g, g_theme.ui.text.b, 255);
    ImU32 accent_col = IM_COL32(g_theme.ui.accent.r, g_theme.ui.accent.g, g_theme.ui.accent.b, 255);

    float chev_x = p0.x + 6.0f + (chev_slot_w - chev_sz.x) * 0.5f;
    ImGui::GetWindowDrawList()->AddText(ImVec2(chev_x, y), text_col, chev);
    ImGui::GetWindowDrawList()->AddText(ImVec2(p0.x + 6.0f + chev_slot_w + 6.0f, y), accent_col, def->icon);
    ImGui::GetWindowDrawList()->AddText(ImVec2(p0.x + 6.0f + chev_slot_w + 6.0f + icon_sz.x + 8.0f, y), text_col, def->title);

    if (clicked) *open = !*open;

    /* ---- drag handle (reorder) ----------------------------------------- */
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::InvisibleButton("##handle", ImVec2(HANDLE_W, frame_h));
    bool handle_hovered = ImGui::IsItemHovered();
    bool handle_active = ImGui::IsItemActive();

    ImVec2 h0 = ImGui::GetItemRectMin();
    ImVec2 h1 = ImGui::GetItemRectMax();
    ImVec2 grip_sz = ImGui::CalcTextSize(ICON_FA_GRIP_VERTICAL);

    /* cursor feedback: grab hand on hover / while dragging */
    if (handle_hovered || handle_active)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    /* draw grip dots - theme-aware (12.1) */
    Color grip_theme = g_theme.ui.text_dim;
    if (handle_hovered || handle_active)
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(h0.x + 1, h0.y + 2), ImVec2(h1.x - 1, h1.y - 2),
            IM_COL32(grip_theme.r, grip_theme.g, grip_theme.b, 200), 2.0f);

    ImGui::GetWindowDrawList()->AddText(
        ImVec2(h0.x + (HANDLE_W - grip_sz.x) * 0.5f, h0.y + (frame_h - grip_sz.y) * 0.5f),
        IM_COL32(grip_theme.r, grip_theme.g, grip_theme.b, 200), ICON_FA_GRIP_VERTICAL);

    /* ---- reorder drag logic -------------------------------------------- */
    if (handle_active && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        if (g_layout.drag_panel < 0)
        {
            g_layout.drag_panel = def->id;
            g_layout.drag_is_left = is_left;
        }
    }

    /* ---- store header rect for drop-target computation ----------------- */
    if (is_left && s_left_hc < MAX_PANELS)
    {
        s_left_headers[s_left_hc].id = def->id;
        s_left_headers[s_left_hc].y0 = p0.y;
        s_left_headers[s_left_hc].y1 = p1.y;
        s_left_hc++;
    }
    else if (!is_left && s_right_hc < MAX_PANELS)
    {
        s_right_headers[s_right_hc].id = def->id;
        s_right_headers[s_right_hc].y0 = p0.y;
        s_right_headers[s_right_hc].y1 = p1.y;
        s_right_hc++;
    }

    ImGui::PopID();
}

/* ========================================================================== */
/*  Reorder finalisation                                                      */
/* ========================================================================== */

/** Map a "visible target" (a count of enabled, non-dragged header slots above
 *  the mouse) to a full-array insertion index. The order[] arrays contain both
 *  enabled and disabled panels (disabled ones are skipped at render time but
 *  still occupy a slot), so the visible count must be translated back into a
 *  real array position. Pass dragged = -1 when the panel is not in this array
 *  (e.g. when moving it into the other sidebar). */
static int FindInsertIndex(int *order, int dragged, int visible_target)
{
    int seen = 0;
    for (int i = 0; i < MAX_PANELS; i++)
    {
        int pid = order[i];
        if (pid < 0) break;              /* used slots are contiguous at the front */
        if (pid == dragged) continue;
        if (seen == visible_target) return i;
        seen++;
    }
    /* insert after the last used slot */
    int end = 0;
    while (end < MAX_PANELS && order[end] >= 0) end++;
    return end;
}

/** Draw the blue insertion line in the given sidebar at the visible target. */
static void DrawInsertionLine(bool is_left, HeaderSlot *slots, int sc, int visible_target)
{
    float line_y = 0.0f;
    int nd = 0;
    for (int i = 0; i < sc; i++)
    {
        if (slots[i].id == g_layout.drag_panel) continue;
        if (nd == visible_target) { line_y = slots[i].y0; break; }
        nd++;
    }
    if (nd == visible_target && line_y == 0.0f && sc > 0)
    {
        /* after the last visible slot */
        for (int i = sc - 1; i >= 0; i--)
            if (slots[i].id != g_layout.drag_panel) { line_y = slots[i].y1; break; }
    }

    if (line_y > 0.0f)
    {
        float sidebar_x = is_left ? 0.0f : (float)GetScreenWidth() - g_layout.right_width;
        float sidebar_w = is_left ? g_layout.left_width : g_layout.right_width;
        Color drop_theme = g_theme.ui.accent;
        ImGui::GetForegroundDrawList()->AddLine(
            ImVec2(sidebar_x, line_y), ImVec2(sidebar_x + sidebar_w, line_y),
            IM_COL32(drop_theme.r, drop_theme.g, drop_theme.b, 220), 2.0f);
    }
}

static void FinishReorder(AppConfig *cfg)
{
    if (g_layout.drag_panel < 0) return;

    bool src_is_left = g_layout.drag_is_left;
    float mouse_x = ImGui::GetIO().MousePos.x;
    float mouse_y = ImGui::GetIO().MousePos.y;
    float display_w = ImGui::GetIO().DisplaySize.x;

    /* which sidebar is the mouse currently over? */
    bool over_left  = (mouse_x >= 0.0f && mouse_x < g_layout.left_width);
    bool over_right = (mouse_x > display_w - g_layout.right_width && mouse_x <= display_w);

    /* target sidebar: follow the mouse into the other sidebar, else stay put */
    bool target_is_left;
    if (over_left && !src_is_left)      target_is_left = true;
    else if (over_right && src_is_left) target_is_left = false;
    else                                target_is_left = src_is_left;

    HeaderSlot *slots = target_is_left ? s_left_headers : s_right_headers;
    int sc = target_is_left ? s_left_hc : s_right_hc;

    /* visible target: count of enabled (non-dragged) slot midpoints above mouse_y */
    int visible_target = 0;
    for (int i = 0; i < sc; i++)
    {
        if (slots[i].id == g_layout.drag_panel) continue;
        float mid = (slots[i].y0 + slots[i].y1) * 0.5f;
        if (mouse_y > mid) visible_target++;
    }
    g_layout.drag_target = visible_target;

    /* draw the insertion line in the target sidebar */
    DrawInsertionLine(target_is_left, slots, sc, visible_target);

    /* commit on mouse release */
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        if (target_is_left != src_is_left)
        {
            /* ---- move the panel to the other sidebar -------------------- */
            int *src_order = src_is_left ? g_layout.left_order : g_layout.right_order;
            int *dst_order = target_is_left ? g_layout.left_order : g_layout.right_order;

            int src = -1;
            for (int i = 0; i < MAX_PANELS; i++)
                if (src_order[i] == g_layout.drag_panel) { src = i; break; }

            if (src >= 0)
            {
                /* remove from source */
                for (int i = src; i < MAX_PANELS - 1; i++) src_order[i] = src_order[i + 1];
                src_order[MAX_PANELS - 1] = -1;
                /* compact source (shift -1s to the end) */
                int w = 0;
                for (int i = 0; i < MAX_PANELS; i++)
                    if (src_order[i] >= 0) src_order[w++] = src_order[i];
                for (int i = w; i < MAX_PANELS; i++) src_order[i] = -1;

                /* insert into destination at the visible target */
                int target = FindInsertIndex(dst_order, -1, visible_target);
                for (int i = MAX_PANELS - 1; i > target; i--) dst_order[i] = dst_order[i - 1];
                dst_order[target] = g_layout.drag_panel;
            }
        }
        else
        {
            /* ---- same-sidebar reorder ---------------------------------- */
            int *order = src_is_left ? g_layout.left_order : g_layout.right_order;

            int src = -1;
            for (int i = 0; i < MAX_PANELS; i++)
                if (order[i] == g_layout.drag_panel) { src = i; break; }

            if (src >= 0)
            {
                int target = FindInsertIndex(order, g_layout.drag_panel, visible_target);
                int dragged = order[src];

                /* remove dragged panel */
                for (int i = src; i < MAX_PANELS - 1; i++) order[i] = order[i + 1];
                order[MAX_PANELS - 1] = -1;

                /* adjust target if the source was before it */
                if (src < target) target--;

                /* insert at target */
                for (int i = MAX_PANELS - 1; i > target; i--) order[i] = order[i - 1];
                order[target] = dragged;
            }
        }

        g_layout.drag_panel = -1;
        g_layout.drag_target = -1;

        /* persist the new arrangement immediately */
        if (cfg)
        {
            LayoutFillPersist(&cfg->ui_layout);
            SaveAppConfig("settings.json", cfg);
        }
    }
}

/* ========================================================================== */
/*  Sidebar                                                                   */
/* ========================================================================== */

static void DrawSidebar(bool is_left, UIContext *ctx, AppConfig *cfg)
{
    float display_w = ImGui::GetIO().DisplaySize.x;
    float nav_h = ImGui::GetFrameHeight();

    if (is_left && g_layout.left_hidden)
    {
        /* notch is drawn in DrawUILayout (on top of everything) */
        return;
    }
    if (!is_left && g_layout.right_hidden)
    {
        /* notch is drawn in DrawUILayout (on top of everything) */
        return;
    }

    if (is_left && !g_layout.left_visible) return;
    if (!is_left && !g_layout.right_visible) return;

    float width = is_left ? g_layout.left_width : g_layout.right_width;
    float x = is_left ? 0.0f : display_w - width;
    float y = nav_h;
    float x0 = x, x1 = x + width;
    float h = GetContentHeight(x0, x1);

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, h), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg,            ThemeColor(g_theme.ui.bg));
    ImGui::PushStyleColor(ImGuiCol_Border,               ThemeColor(g_theme.ui.border));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,          ThemeColor(g_theme.ui.bg));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,        ThemeColor(ThemeMix(g_theme.ui.surface, g_theme.ui.text, 0.25f)));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ThemeColor(ThemeMix(g_theme.ui.surface, g_theme.ui.text, 0.35f)));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,  ThemeColor(ThemeMix(g_theme.ui.surface, g_theme.ui.text, 0.45f)));

    const char *win_name = is_left ? "##sidebar_left" : "##sidebar_right";
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_AlwaysVerticalScrollbar |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin(win_name, NULL, flags))
    {
        int *order = is_left ? g_layout.left_order : g_layout.right_order;
        int max = MAX_PANELS;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));

        for (int i = 0; i < max; i++)
        {
            int pid = order[i];
            if (pid < 0 || pid >= PANEL_COUNT) continue;
            if (!g_layout.panel_enabled[pid]) continue;  /* skip disabled panels entirely */

            const PanelDef *def = &g_panel_defs[pid];
            bool is_open = g_layout.panel_open[pid];

            DrawAccordionHeader(def, &g_layout.panel_open[pid], i, is_left);

            if (is_open)
            {
                ImGui::Separator();

                /* Draw panel content directly — no outer BeginChild wrapper.
                 * The sidebar itself scrolls, so panels take only the height
                 * their content actually needs. Panels that are extremely long
                 * (e.g. satellite list, log) have their own internal BeginChild
                 * with scrollbars. */
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
                ImGui::Indent(6.0f);
                def->draw_content(ctx, cfg);
                ImGui::Unindent(6.0f);
                ImGui::PopStyleVar();
            }

            if (i < max - 1)
                ImGui::Spacing();
        }

        ImGui::PopStyleVar(2);
    }
    ImGui::End();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(3);
}

/* ========================================================================== */
/*  Navigation bar                                                            */
/* ========================================================================== */

void DrawNavBar(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    (void)cfg;

    if (ImGui::BeginMainMenuBar())
    {
        /* ---- File menu --------------------------------------------------- */
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Exit", "Alt+F4")) UIRequestExit();
            ImGui::EndMenu();
        }

        /* ---- View menu --------------------------------------------------- */
        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Left Sidebar", NULL, &g_layout.left_visible);
            ImGui::MenuItem("Right Sidebar", NULL, &g_layout.right_visible);
            ImGui::MenuItem("Bottom Time Bar", NULL, &g_layout.show_bottom_bar);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout"))
            {
                LayoutInitDefaults();
                LayoutFillPersist(&cfg->ui_layout);
                SaveAppConfig("settings.json", cfg);
            }
            ImGui::EndMenu();
        }

        /* ---- Tools menu -------------------------------------------------- */
        if (ImGui::BeginMenu("Tools"))
        {
            if (ImGui::MenuItem("Manage Tools..."))
            {
                LayoutOpenTools();
            }
            ImGui::EndMenu();
        }

        /* ---- Help menu --------------------------------------------------- */
        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Controls")) UIOpenHelp();
            if (ImGui::MenuItem("GitHub Repository"))
                OpenURL("https://github.com/aweeri/TLEscope");
            if (ImGui::MenuItem("About TLEscope")) UIOpenAbout();
            ImGui::EndMenu();
        }

        /* ---- Settings menu item (after Help) ----------------------------- */
        if (ImGui::MenuItem("Settings"))
        {
            LayoutOpenSettings();
        }

        ImGui::EndMainMenuBar();
    }
}

/* ========================================================================== */
/*  Settings modal                                                            */
/* ========================================================================== */

void DrawSettingsModal(UIContext *ctx, AppConfig *cfg)
{
    if (!g_layout.settings_open) return;

    ImGui::OpenPopup("Settings");
    /* fixed width + auto height: expanding/collapsing sections changes the
     * height but never the width, so the modal doesn't "grow" sideways. */
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((float)GetScreenWidth() * 0.5f, (float)GetScreenHeight() * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    /* p_open draws a native close button in the title bar; clicking it sets
     * settings_open to false so the modal closes. */
    bool *p_open = &g_layout.settings_open;
    if (ImGui::BeginPopupModal("Settings", p_open, ImGuiWindowFlags_AlwaysAutoResize))
    {
        /* ---- Display section --------------------------------------------- */
        if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Show Statistics", &cfg->show_statistics);
            ImGui::Checkbox("Night Mode (Red)", &cfg->night_mode);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Render the entire screen (scene + UI) in monochrome red for dark adaptation (F10)");
            ImGui::Checkbox("VSync", &cfg->hint_vsync);
            if (ImGui::Checkbox("Use Local Time", &cfg->use_local_time))
            {
                SetUseLocalTime(cfg->use_local_time);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Show dates/times in your system timezone instead of UTC");

            /* first day of the week used by the time-controls date picker */
            if (cfg->first_day_of_week < 0 || cfg->first_day_of_week > 1)
                cfg->first_day_of_week = 1;
            if (ImGui::Combo("Week Starts On", &cfg->first_day_of_week, "Sunday\0Monday\0"))
                SaveAppConfig("settings.json", cfg);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("First day of the week shown in the date-picker calendar");

            /* number of predicted (future) orbit steps drawn for each ground
             * track in 2D mode. Float slider with a 0.25-orbit sub-step grid
             * (snapped both on-screen and on write), default 2.0. */
            ImGui::Separator();
            float orbits_steps = ToolSettingGetFloat(cfg, LAYERS_KEY_FUTURE_ORBITS_STEPS,
                                                     LAYERS_FUTURE_ORBITS_STEPS_DEFAULT);
            orbits_steps = roundf(orbits_steps * 4.0f) / 4.0f; /* snap to 0.25 grid */
            if (ImGui::SliderFloat("Orbit Steps", &orbits_steps, LAYERS_FUTURE_ORBITS_STEPS_MIN,
                                   LAYERS_FUTURE_ORBITS_STEPS_MAX, "%.2f"))
            {
                orbits_steps = roundf(orbits_steps * 4.0f) / 4.0f;
                orbits_steps = fmaxf(LAYERS_FUTURE_ORBITS_STEPS_MIN, orbits_steps);
                orbits_steps = fminf(LAYERS_FUTURE_ORBITS_STEPS_MAX, orbits_steps);
                ToolSettingSetFloat(cfg, LAYERS_KEY_FUTURE_ORBITS_STEPS, orbits_steps);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("How many predicted orbits are drawn for each future ground track in 2D mode.\nRange: 0.25 - 5.0 orbits, in 0.25-orbit steps. Default: 2.0");
        }

        /* ---- Performance section ----------------------------------------- */
        if (ImGui::CollapsingHeader("Performance"))
        {
            /* dragging the slider to max (240) enables unlimited FPS (0) */
            int fps = (cfg->target_fps == 0) ? 240 : cfg->target_fps;
            if (ImGui::SliderInt("Max FPS", &fps, 15, 240))
                cfg->target_fps = (fps >= 240) ? 0 : fps;
            if (cfg->target_fps == 0)
                ImGui::TextDisabled("Unlimited FPS");
            ImGui::SliderFloat("UI Scale", &cfg->ui_scale, 0.5f, 2.0f);
        }

        /* ---- Theme section ----------------------------------------------- */
        if (ImGui::CollapsingHeader("Theme"))
        {
            if (g_ui.theme_names[0] == '\0')
            {
                ThemeList list;
                if (ThemeDiscover(&list))
                {
                    memcpy(g_ui.theme_names, list.names, sizeof(g_ui.theme_names));
                    g_ui.active_theme_idx = 0;
                    const char *name = g_ui.theme_names;
                    int idx = 0;
                    while (*name)
                    {
                        if (strcmp(name, cfg->theme) == 0)
                        {
                            g_ui.active_theme_idx = idx;
                            break;
                        }
                        name += strlen(name) + 1;
                        idx++;
                    }
                }
            }

            int prev_theme = g_ui.active_theme_idx;
            ImGui::Combo("Theme##dropdown", &g_ui.active_theme_idx, g_ui.theme_names);
            if (g_ui.active_theme_idx != prev_theme)
            {
                const char *name = g_ui.theme_names;
                for (int i = 0; i < g_ui.active_theme_idx; i++)
                    name += strlen(name) + 1;
                strncpy(cfg->theme, name, 63);
                cfg->theme[63] = '\0';
                cfg->reload_theme = true;
            }
        }

        /* ---- Locations section (single source of truth for markers + home) */
        if (ImGui::CollapsingHeader("Locations"))
        {
            static int editing = -1;   /* index of the location being edited, -1 = none */
            if (editing >= location_count) editing = -1;

            if (location_count == 0)
            {
                ImGui::TextDisabled("No locations yet. Add one below.");
            }
            else
            {
                /* aligned columns: name | home | edit | remove */
                float btn_w = ImGui::GetFrameHeight();
                float spacing = ImGui::GetStyle().ItemSpacing.x;
                float row_w = ImGui::GetContentRegionAvail().x;
                float btn_start = row_w - 3.0f * btn_w - 2.0f * spacing;

                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));
                /* pull the FontAwesome glyphs down and left so they sit centered in the square buttons */
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.45f, 0.6f));

                for (int i = 0; i < location_count; i++)
                {
                    Location *loc = &locations[i];
                    char btn_id[48];

                    ImGui::Text("%s%s", loc->name, loc->is_home ? "  (Home)" : "");

                    ImGui::SameLine(btn_start);
                    snprintf(btn_id, sizeof(btn_id), "%s##home_%d", ICON_FA_HOUSE, i);
                    bool was_home = loc->is_home;
                    if (was_home)
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 200, 0, 255));
                    if (ImGui::Button(btn_id, ImVec2(btn_w, btn_w)))
                    {
                        if (!loc->is_home)
                            SetHomeLocation(i);
                    }
                    if (was_home)
                        ImGui::PopStyleColor(1);

                    ImGui::SameLine();
                    snprintf(btn_id, sizeof(btn_id), "%s##edit_%d", ICON_FA_PEN, i);
                    if (ImGui::Button(btn_id, ImVec2(btn_w, btn_w)))
                        editing = i;

                    ImGui::SameLine();
                    snprintf(btn_id, sizeof(btn_id), "%s##remove_%d", ICON_FA_XMARK, i);
                    Color err_theme = g_theme.ui.error;
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        IM_COL32(err_theme.r, err_theme.g, err_theme.b, 255));
                    if (ImGui::Button(btn_id, ImVec2(btn_w, btn_w)))
                    {
                        RemoveLocation(i);
                        if (editing == i) editing = -1;
                        if (editing > i) editing--;
                        i--; /* re-check the shifted row */
                    }
                    ImGui::PopStyleColor(1);
                }

                ImGui::PopStyleVar();
                ImGui::PopStyleVar();
            }

            /* inline edit fields for the row being edited */
            if (editing >= 0 && editing < location_count)
            {
                Location *loc = &locations[editing];
                ImGui::Separator();
                ImGui::Text("Edit: %s", loc->name);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));
                ImGui::InputText("Name", loc->name, sizeof(loc->name));
                ImGui::InputFloat("Latitude", &loc->lat);
                ImGui::InputFloat("Longitude", &loc->lon);
                ImGui::InputFloat("Altitude", &loc->alt);
                ImGui::PopStyleVar();

                ImGui::Spacing();
                if (ImGui::Button("Done"))
                    editing = -1;
                ImGui::SameLine();
                if (ImGui::Button("Pick on Map"))
                {
                    /* picking on the map updates the location being edited */
                    pick_location_index = editing;
                    *ctx->picking_home = true;
                    g_layout.settings_open = false;
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Add Location"))
            {
                const int idx = AddLocation("New Location", 0.0f, 0.0f, 0.0f);
                if (idx >= 0)
                    editing = idx;
            }
        }

        /* ---- Data section ------------------------------------------------ */
        if (ImGui::CollapsingHeader("Data"))
        {
            const char* stale_options[] = {
                "6 hours", "12 hours", "1 day", "2 days (default)",
                "3 days", "5 days", "7 days"
            };
            int stale_values[] = {
                STALE_THRESHOLD_6H, STALE_THRESHOLD_12H, STALE_THRESHOLD_1D,
                STALE_THRESHOLD_2D, STALE_THRESHOLD_3D, STALE_THRESHOLD_5D,
                STALE_THRESHOLD_7D
            };
            int current_stale_idx = 3;
            for (int i = 0; i < 7; i++)
            {
                if (cfg->data_stale_threshold_seconds == stale_values[i])
                {
                    current_stale_idx = i;
                    break;
                }
            }

            ImGui::Text("Consider data outdated after:");
            if (ImGui::Combo("##stale_threshold", &current_stale_idx, stale_options, 7))
            {
                cfg->data_stale_threshold_seconds = stale_values[current_stale_idx];
            }
        }

        /* ---- Network section --------------------------------------------- */
        if (ImGui::CollapsingHeader("Network"))
        {
            if (cfg->network_timeout_seconds < 15) cfg->network_timeout_seconds = 15;
            if (cfg->network_timeout_seconds > 300) cfg->network_timeout_seconds = 300;

            int timeout_seconds = cfg->network_timeout_seconds;
            ImGui::Text("HTTP request timeout:");
            if (ImGui::SliderInt("##network_timeout", &timeout_seconds, 15, 300, "%d s"))
            {
                cfg->network_timeout_seconds = timeout_seconds;
                TLEscopeSetCurlTimeoutSeconds(timeout_seconds);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Maximum time for a network request. Default: 45 seconds. Connection timeout remains 15 seconds.");

            ImGui::TextDisabled("Default: 45 s. Range: 15-300 s.");
        }

        /* ---- Notifications section (ROADMAP section 8.1) ---------------- */
        if (ImGui::CollapsingHeader("Notifications"))
        {
            bool enabled = NotifyEnabled();
            if (ImGui::Checkbox("Enable Notifications", &enabled))
                NotifySetEnabled(enabled);

            ImGui::Separator();
            ImGui::TextDisabled("Show toasts for:");

            bool cat_info = NotifyCategoryEnabled(NOTIFY_INFO);
            if (ImGui::Checkbox("Info (data, sources)", &cat_info))
                NotifySetCategoryEnabled(NOTIFY_INFO, cat_info);

            bool cat_ok = NotifyCategoryEnabled(NOTIFY_SUCCESS);
            if (ImGui::Checkbox("Success (saved, connected)", &cat_ok))
                NotifySetCategoryEnabled(NOTIFY_SUCCESS, cat_ok);

            bool cat_warn = NotifyCategoryEnabled(NOTIFY_WARNING);
            if (ImGui::Checkbox("Warnings (stale data)", &cat_warn))
                NotifySetCategoryEnabled(NOTIFY_WARNING, cat_warn);

            bool cat_err = NotifyCategoryEnabled(NOTIFY_ERROR);
            if (ImGui::Checkbox("Errors", &cat_err))
                NotifySetCategoryEnabled(NOTIFY_ERROR, cat_err);
        }

        /* ---- Buttons ----------------------------------------------------- */
        ImGui::Separator();

        if (ImGui::Button("Save Settings", ImVec2(140, 0)))
        {
            NotifySaveSettings(cfg);
            LayoutFillPersist(&cfg->ui_layout);
            SaveAppConfig("settings.json", cfg);
            NotifyPush(NOTIFY_SUCCESS, ICON_FA_CHECK, "Settings saved");
        }

        ImGui::SameLine();

        if (ImGui::Button("Close", ImVec2(140, 0)))
        {
            g_layout.settings_open = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

/* -- Tools modal row helper ------------------------------------------------ */

/** draw one tool row: an enable/disable checkbox and a small left/right
 *  toggle aligned to the right edge of the row. Changes are saved
 *  immediately so the modal needs no explicit save button. */
static void DrawToolRow(const PanelDef *def, AppConfig *cfg)
{
    bool enabled = g_layout.panel_enabled[def->id];
    if (ImGui::Checkbox(def->title, &enabled))
    {
        g_layout.panel_enabled[def->id] = enabled;
        if (enabled)
        {
            g_layout.panel_open[def->id] = true;
            EnsureSidebar(def->default_side);
        }
        LayoutFillPersist(&cfg->ui_layout);
        SaveAppConfig("settings.json", cfg);
    }

    /* right-align a compact Left/Right toggle (0 = left, 1 = right) */
    const float btn_w = 34.0f;
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SameLine(avail - 2.0f * btn_w - ImGui::GetStyle().ItemSpacing.x);

    SidebarSide cur = LayoutPanelCurrentSide(def->id);
    ImGui::PushID(def->id);

    /* Left button */
    if (ImGui::Button(cur == SIDEBAR_LEFT ? ICON_FA_ARROW_LEFT "##L" : "##L", ImVec2(btn_w, 0)))
    {
        LayoutSetPanelSide(def->id, SIDEBAR_LEFT);
        LayoutFillPersist(&cfg->ui_layout);
        SaveAppConfig("settings.json", cfg);
    }
    ImGui::SameLine();

    /* Right button */
    if (ImGui::Button(cur == SIDEBAR_RIGHT ? ICON_FA_ARROW_RIGHT "##R" : "##R", ImVec2(btn_w, 0)))
    {
        LayoutSetPanelSide(def->id, SIDEBAR_RIGHT);
        LayoutFillPersist(&cfg->ui_layout);
        SaveAppConfig("settings.json", cfg);
    }

    ImGui::PopID();
}

/* ========================================================================== */
/*  Tools modal                                                               */
/* ========================================================================== */

void DrawToolsModal(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    if (!g_layout.tools_open) return;

    ImGui::OpenPopup("Manage Tools");
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((float)GetScreenWidth() * 0.5f, (float)GetScreenHeight() * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    /* p_open draws a native close button in the title bar; clicking it sets
     * tools_open to false so the modal closes. */
    bool *p_open = &g_layout.tools_open;
    if (ImGui::BeginPopupModal("Manage Tools", p_open, ImGuiWindowFlags_AlwaysAutoResize))
    {
        /* ---- Core Functions --------------------------------------------- */
        if (ImGui::CollapsingHeader("Core Functions", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (int i = 0; i < PANEL_COUNT; i++)
            {
                const PanelDef *def = &g_panel_defs[i];
                if (def->category != PANEL_CAT_CORE) continue;
                DrawToolRow(def, cfg);
            }
        }

        /* ---- Extra Tools ------------------------------------------------ */
        if (ImGui::CollapsingHeader("Extra Tools", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (int i = 0; i < PANEL_COUNT; i++)
            {
                const PanelDef *def = &g_panel_defs[i];
                if (def->category != PANEL_CAT_EXTRA) continue;
                DrawToolRow(def, cfg);
            }
        }

        /* ---- Debug ------------------------------------------------------- */
        if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (int i = 0; i < PANEL_COUNT; i++)
            {
                const PanelDef *def = &g_panel_defs[i];
                if (def->category != PANEL_CAT_DEBUG) continue;
                DrawToolRow(def, cfg);
            }
        }

        /* ---- Buttons ----------------------------------------------------- */
        ImGui::Separator();

        if (ImGui::Button("Close", ImVec2(140, 0)))
        {
            g_layout.tools_open = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

/* ========================================================================== */
/*  Main layout entry point                                                   */
/* ========================================================================== */

void DrawUILayout(UIContext *ctx, AppConfig *cfg)
{
    s_left_hc = 0;
    s_right_hc = 0;

    DrawSidebar(true,  ctx, cfg);
    DrawSidebar(false, ctx, cfg);

    /* resize strips (drawn after sidebars so they sit on top) */
    float nav_h = ImGui::GetFrameHeight();
    float display_w = ImGui::GetIO().DisplaySize.x;

    if (g_layout.left_visible && !g_layout.left_hidden)
        DrawResizeStrip(true, nav_h, GetContentHeight(0.0f, g_layout.left_width));
    if (g_layout.right_visible && !g_layout.right_hidden)
        DrawResizeStrip(false, nav_h,
                        GetContentHeight(display_w - g_layout.right_width, display_w));

    /* show/hide notches — drawn last so they sit on top of everything.
     * A notch appears on the visible sidebar edge (to hide it) and at the
     * screen edge when the sidebar is hidden (to restore it). */
    float nav_h2 = ImGui::GetFrameHeight();
    /* content area height (below the nav bar) - used to center the notches */
    float content_h = ImGui::GetIO().DisplaySize.y - nav_h2;
    DrawNotch(true,  nav_h2, content_h, g_layout.left_visible  && !g_layout.left_hidden);
    DrawNotch(false, nav_h2, content_h, g_layout.right_visible && !g_layout.right_hidden);

    /* reorder finalisation */
    FinishReorder(cfg);
}