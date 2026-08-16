/*
 * ui_layout.cpp - Sidebar / panel workspace layout engine
 *
 * Implements the three-section workspace: a left sidebar (actions), a
 * transparent center canvas (simulation), and a right sidebar (inspector).
 * Sidebars are resizable, snap-hide to the screen edge (pull-tab to
 * restore), and contain reorderable accordion panels.
 */

#include "ui_layout.h"
#include "panels.h"
#include "core/theme.h"
#include "core/config.h"
#include "util/log.h"

#include <raylib.h>
#include <imgui.h>
#include <rlImGui.h>
#include "IconsFontAwesome6.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

/* -- Layout constants ------------------------------------------------------ */

static const float HANDLE_WIDTH   = 6.0f;   /* resize strip width          */
static const float TAB_WIDTH      = 18.0f;  /* pull-tab width              */
static const float MIN_SIDEBAR_W  = 280.0f; /* minimum sidebar width       */
static const float MAX_SIDEBAR_W  = 600.0f; /* maximum sidebar width       */
static const float DEF_SIDEBAR_W  = 320.0f; /* default sidebar width       */
static const float BOTTOM_BAR_H   = 52.0f;  /* height reserved for bottom bar (bar_h=40 + 12px margin) */
static const float HANDLE_W       = 22.0f;  /* panel drag handle width     */

/* -- Helper ---------------------------------------------------------------- */

static ImVec4 ThemeColor(const Color &c)
{
    return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

/* -- Panel registry -------------------------------------------------------- */

const PanelDef g_panel_defs[PANEL_COUNT] = {
    { PANEL_SAT_MGR,      "Satellite Manager",  ICON_FA_SATELLITE,    PANEL_CAT_CORE,       SIDEBAR_LEFT,  true,  DrawPanelSatMgr },
    { PANEL_DATA_SOURCES, "Data Sources",        ICON_FA_DATABASE,     PANEL_CAT_CORE,       SIDEBAR_LEFT,  true,  DrawPanelDataSources },
    { PANEL_TIME_CTRL,    "Time Control",        ICON_FA_CLOCK,        PANEL_CAT_CORE,       SIDEBAR_RIGHT, false, DrawPanelTimeCtrl },
    { PANEL_SCOPE,        "Scope",               ICON_FA_CROSSHAIRS,   PANEL_CAT_SCIENTIFIC, SIDEBAR_LEFT,  false, DrawPanelScope },
    { PANEL_ROTATOR,      "Rotator Control",     ICON_FA_TURN_UP,      PANEL_CAT_CORE,       SIDEBAR_LEFT,  false, DrawPanelRotator },
    { PANEL_SAT_INFO,     "Satellite Info",      ICON_FA_CIRCLE_INFO,  PANEL_CAT_INSPECTOR,  SIDEBAR_RIGHT, true,  DrawPanelSatInfo },
    { PANEL_PASSES,       "Satellite Passes",    ICON_FA_ROUTE,        PANEL_CAT_SCIENTIFIC, SIDEBAR_RIGHT, false, DrawPanelPasses },
    { PANEL_POLAR_PLOT,   "Polar Plot",          ICON_FA_COMPASS,      PANEL_CAT_SCIENTIFIC, SIDEBAR_RIGHT, false, DrawPanelPolarPlot },
    { PANEL_DOPPLER,      "Doppler Analysis",    ICON_FA_TOWER_BROADCAST, PANEL_CAT_SCIENTIFIC, SIDEBAR_RIGHT, false, DrawPanelDoppler },
    { PANEL_LOG,          "Log",                 ICON_FA_LIST,         PANEL_CAT_SCIENTIFIC, SIDEBAR_RIGHT, false, DrawPanelLog },
};

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

    int li = 0, ri = 0;
    for (int i = 0; i < PANEL_COUNT; i++)
    {
        const PanelDef *def = &g_panel_defs[i];
        if (def->default_side == SIDEBAR_LEFT)
            g_layout.left_order[li++] = def->id;
        else
            g_layout.right_order[ri++] = def->id;
        g_layout.panel_open[def->id] = def->default_open;
    }
}

void LayoutApplyPersist(const UILayoutPersist *p)
{
    g_layout.left_width  = p->left_sidebar_width;
    g_layout.right_width = p->right_sidebar_width;
    g_layout.left_visible  = p->left_sidebar_visible;
    g_layout.right_visible = p->right_sidebar_visible;
    g_layout.left_hidden   = p->left_sidebar_hidden;
    g_layout.right_hidden  = p->right_sidebar_hidden;

    for (int i = 0; i < MAX_LEFT_PANELS; i++)
        g_layout.left_order[i] = p->left_panel_order[i];
    for (int i = 0; i < MAX_RIGHT_PANELS; i++)
        g_layout.right_order[i] = p->right_panel_order[i];

    for (int i = 0; i < MAX_LEFT_PANELS; i++)
    {
        int pid = p->left_panel_order[i];
        if (pid >= 0 && pid < PANEL_COUNT)
            g_layout.panel_open[pid] = p->left_panel_open[i];
    }
    for (int i = 0; i < MAX_RIGHT_PANELS; i++)
    {
        int pid = p->right_panel_order[i];
        if (pid >= 0 && pid < PANEL_COUNT)
            g_layout.panel_open[pid] = p->right_panel_open[i];
    }
}

void LayoutFillPersist(UILayoutPersist *p)
{
    p->left_sidebar_width  = g_layout.left_width;
    p->right_sidebar_width = g_layout.right_width;
    p->left_sidebar_visible  = g_layout.left_visible;
    p->right_sidebar_visible = g_layout.right_visible;
    p->left_sidebar_hidden   = g_layout.left_hidden;
    p->right_sidebar_hidden  = g_layout.right_hidden;

    for (int i = 0; i < MAX_LEFT_PANELS; i++)
        p->left_panel_order[i] = g_layout.left_order[i];
    for (int i = 0; i < MAX_RIGHT_PANELS; i++)
        p->right_panel_order[i] = g_layout.right_order[i];

    for (int i = 0; i < MAX_LEFT_PANELS; i++)
    {
        int pid = g_layout.left_order[i];
        p->left_panel_open[i] = (pid >= 0 && pid < PANEL_COUNT) ? g_layout.panel_open[pid] : false;
    }
    for (int i = 0; i < MAX_RIGHT_PANELS; i++)
    {
        int pid = g_layout.right_order[i];
        p->right_panel_open[i] = (pid >= 0 && pid < PANEL_COUNT) ? g_layout.panel_open[pid] : false;
    }
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

void LayoutClosePanel(PanelId id)
{
    if (id < 0 || id >= PANEL_COUNT) return;
    g_layout.panel_open[id] = false;
}

bool LayoutIsPanelOpen(PanelId id)
{
    return (id >= 0 && id < PANEL_COUNT) ? g_layout.panel_open[id] : false;
}

/* -- Sidebar visibility helpers --------------------------------------------- */

SidebarSide PanelSide(PanelId id)
{
    if (id < 0 || id >= PANEL_COUNT) return SIDEBAR_NONE;
    return g_panel_defs[id].default_side;
}

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
void LayoutCloseSettings(void) { g_layout.settings_open = false; }

/* -- Drag reorder data (per-frame) ----------------------------------------- */

typedef struct { PanelId id; float y0, y1; } HeaderSlot;
static HeaderSlot s_left_headers[MAX_LEFT_PANELS];
static HeaderSlot s_right_headers[MAX_RIGHT_PANELS];
static int s_left_hc = 0, s_right_hc = 0;

/* ========================================================================== */
/*  Pull-tab                                                                  */
/* ========================================================================== */

static void DrawPullTab(bool is_left, float nav_h, float content_h)
{
    float screen_w = (float)GetScreenWidth();
    float x = is_left ? 0.0f : screen_w - TAB_WIDTH;

    ImGui::SetNextWindowPos(ImVec2(x, nav_h), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(TAB_WIDTH, content_h), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ThemeColor(g_theme.ui.ui_primary));

    if (ImGui::Begin(is_left ? "##pull_left" : "##pull_right", NULL,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings))
    {
        bool hovered = ImGui::IsWindowHovered();
        bool clicked = ImGui::InvisibleButton("##tab", ImVec2(TAB_WIDTH, content_h));

        if (hovered || clicked)
        {
            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(ImVec2(0, 0), ImVec2(TAB_WIDTH, content_h),
                              IM_COL32(255, 255, 255, 30));
        }

        const char *icon = is_left ? ICON_FA_CHEVRON_RIGHT : ICON_FA_CHEVRON_LEFT;
        ImVec2 icon_sz = ImGui::CalcTextSize(icon);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2((TAB_WIDTH - icon_sz.x) * 0.5f, (content_h - icon_sz.y) * 0.5f),
            IM_COL32(200, 200, 200, 220), icon);

        if (clicked)
        {
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
        if (hovered)
            ImGui::SetTooltip(is_left ? "Show Left Sidebar" : "Show Right Sidebar");
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
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
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    if (ImGui::Begin(is_left ? "##resize_left" : "##resize_right", NULL,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground))
    {
        ImGui::InvisibleButton("##grip", ImVec2(HANDLE_WIDTH, content_h));

        bool hovered = ImGui::IsItemHovered();
        bool dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);

        if (hovered || dragging)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        /* visual: thin line at the boundary + highlighted strip when hovered */
        float boundary_x = is_left ? *width : (screen_w - *width);
        ImDrawList *dl = ImGui::GetWindowDrawList();

        if (dragging)
        {
            /* bright highlight during drag */
            dl->AddRectFilled(ImVec2(strip_x, nav_h),
                              ImVec2(strip_x + HANDLE_WIDTH, nav_h + content_h),
                              IM_COL32(255, 255, 255, 40));
        }
        dl->AddLine(ImVec2(boundary_x, nav_h), ImVec2(boundary_x, nav_h + content_h),
                    hovered || dragging ? IM_COL32(200, 200, 200, 180) : IM_COL32(120, 120, 120, 80),
                    hovered ? 2.0f : 1.0f);

        if (dragging)
        {
            float mouse_x = ImGui::GetIO().MousePos.x;

            if (is_left)
            {
                *width = fmaxf(MIN_SIDEBAR_W, fminf(MAX_SIDEBAR_W, mouse_x));
                if (mouse_x <= 8.0f) { SnapHide(is_left); ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(3); return; }
            }
            else
            {
                *width = fmaxf(MIN_SIDEBAR_W, fminf(MAX_SIDEBAR_W, screen_w - mouse_x));
                if (mouse_x >= screen_w - 8.0f) { SnapHide(is_left); ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(3); return; }
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
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
    Color hdr_bg   = *open ? g_theme.ui.header       : g_theme.ui.frame_bg;
    Color hdr_hov  = *open ? g_theme.ui.header_hovered : g_theme.ui.frame_bg_hovered;
    ImU32 bg = *open ? IM_COL32(hdr_bg.r, hdr_bg.g, hdr_bg.b, 200) : IM_COL32(hdr_bg.r, hdr_bg.g, hdr_bg.b, 160);
    if (hovered) bg = *open ? IM_COL32(hdr_hov.r, hdr_hov.g, hdr_hov.b, 220) : IM_COL32(hdr_hov.r, hdr_hov.g, hdr_hov.b, 180);
    ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, bg, 4.0f);

    /* icons: chevron + panel icon + title */
    const char *chev = *open ? ICON_FA_CHEVRON_DOWN : ICON_FA_CHEVRON_RIGHT;
    ImVec2 chev_sz = ImGui::CalcTextSize(chev);
    ImVec2 icon_sz = ImGui::CalcTextSize(def->icon);
    float y = p0.y + (frame_h - chev_sz.y) * 0.5f;
    ImU32 text_col = IM_COL32(g_theme.ui.text_main.r, g_theme.ui.text_main.g, g_theme.ui.text_main.b, 255);
    ImU32 accent_col = IM_COL32(g_theme.ui.ui_accent.r, g_theme.ui.ui_accent.g, g_theme.ui.ui_accent.b, 255);

    ImGui::GetWindowDrawList()->AddText(ImVec2(p0.x + 6.0f, y), text_col, chev);
    ImGui::GetWindowDrawList()->AddText(ImVec2(p0.x + 6.0f + chev_sz.x + 6.0f, y), accent_col, def->icon);
    ImGui::GetWindowDrawList()->AddText(ImVec2(p0.x + 6.0f + chev_sz.x + 6.0f + icon_sz.x + 8.0f, y), text_col, def->title);

    if (clicked) *open = !*open;

    /* ---- drag handle (reorder) ----------------------------------------- */
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::InvisibleButton("##handle", ImVec2(HANDLE_W, frame_h));
    bool handle_hovered = ImGui::IsItemHovered();
    bool handle_active = ImGui::IsItemActive();

    ImVec2 h0 = ImGui::GetItemRectMin();
    ImVec2 h1 = ImGui::GetItemRectMax();
    ImVec2 grip_sz = ImGui::CalcTextSize(ICON_FA_GRIP_VERTICAL);

    /* draw grip dots */
    if (handle_hovered || handle_active)
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(h0.x + 1, h0.y + 2), ImVec2(h1.x - 1, h1.y - 2),
            IM_COL32(80, 80, 90, 200), 2.0f);

    ImGui::GetWindowDrawList()->AddText(
        ImVec2(h0.x + (HANDLE_W - grip_sz.x) * 0.5f, h0.y + (frame_h - grip_sz.y) * 0.5f),
        IM_COL32(160, 160, 160, 200), ICON_FA_GRIP_VERTICAL);

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
    if (is_left && s_left_hc < MAX_LEFT_PANELS)
    {
        s_left_headers[s_left_hc].id = def->id;
        s_left_headers[s_left_hc].y0 = p0.y;
        s_left_headers[s_left_hc].y1 = p1.y;
        s_left_hc++;
    }
    else if (!is_left && s_right_hc < MAX_RIGHT_PANELS)
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

static void FinishReorder()
{
    if (g_layout.drag_panel < 0) return;

    bool is_left = g_layout.drag_is_left;
    int *order = is_left ? g_layout.left_order : g_layout.right_order;
    int count = is_left ? MAX_LEFT_PANELS : MAX_RIGHT_PANELS;
    HeaderSlot *slots = is_left ? s_left_headers : s_right_headers;
    int sc = is_left ? s_left_hc : s_right_hc;

    float mouse_y = ImGui::GetIO().MousePos.y;

    /* compute insertion target: count how many non-dragged slot midpoints are above mouse_y */
    int target = 0;
    for (int i = 0; i < sc; i++)
    {
        if (slots[i].id == g_layout.drag_panel) continue;
        float mid = (slots[i].y0 + slots[i].y1) * 0.5f;
        if (mouse_y > mid) target++;
    }
    g_layout.drag_target = target;

    /* draw insertion line */
    {
        float line_y = 0.0f;
        int nd = 0;
        for (int i = 0; i < sc; i++)
        {
            if (slots[i].id == g_layout.drag_panel) continue;
            if (nd == target) { line_y = slots[i].y0; break; }
            nd++;
        }
        if (nd == target && line_y == 0.0f && sc > 0)
        {
            /* after the last */
            for (int i = sc - 1; i >= 0; i--)
                if (slots[i].id != g_layout.drag_panel) { line_y = slots[i].y1; break; }
        }

        if (line_y > 0.0f)
        {
            float sidebar_x = is_left ? 0.0f : (float)GetScreenWidth() - g_layout.right_width;
            float sidebar_w = is_left ? g_layout.left_width : g_layout.right_width;
            ImGui::GetForegroundDrawList()->AddLine(
                ImVec2(sidebar_x, line_y), ImVec2(sidebar_x + sidebar_w, line_y),
                IM_COL32(100, 180, 255, 220), 2.0f);
        }
    }

    /* commit on mouse release */
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        int src = -1;
        for (int i = 0; i < count; i++)
            if (order[i] == g_layout.drag_panel) { src = i; break; }

        if (src >= 0)
        {
            /* remove dragged panel */
            int dragged = order[src];
            for (int i = src; i < count - 1; i++) order[i] = order[i + 1];
            order[count - 1] = -1;

            /* adjust target */
            if (src < target) target--;
            if (target < 0) target = 0;
            if (target >= count) target = count - 1;

            /* insert at target */
            for (int i = count - 1; i > target; i--) order[i] = order[i - 1];
            order[target] = dragged;
        }

        g_layout.drag_panel = -1;
        g_layout.drag_target = -1;
    }
}

/* ========================================================================== */
/*  Sidebar                                                                   */
/* ========================================================================== */

static void DrawSidebar(bool is_left, UIContext *ctx, AppConfig *cfg)
{
    if (is_left && g_layout.left_hidden)  { DrawPullTab(true,  ImGui::GetFrameHeight(), (float)GetScreenHeight() - ImGui::GetFrameHeight() - (g_layout.show_bottom_bar ? BOTTOM_BAR_H : 0.0f)); return; }
    if (!is_left && g_layout.right_hidden) { DrawPullTab(false, ImGui::GetFrameHeight(), (float)GetScreenHeight() - ImGui::GetFrameHeight() - (g_layout.show_bottom_bar ? BOTTOM_BAR_H : 0.0f)); return; }

    if (is_left && !g_layout.left_visible) return;
    if (!is_left && !g_layout.right_visible) return;

    float screen_w = (float)GetScreenWidth();
    float screen_h = (float)GetScreenHeight();
    float nav_h = ImGui::GetFrameHeight();
    float bottom_h = g_layout.show_bottom_bar ? BOTTOM_BAR_H : 0.0f;
    float width = is_left ? g_layout.left_width : g_layout.right_width;
    float x = is_left ? 0.0f : screen_w - width;
    float y = nav_h;
    float h = screen_h - nav_h - bottom_h;
    if (h < 50.0f) h = 50.0f;

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, h), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg,            ThemeColor(ColorAlpha(g_theme.ui.window_bg, 0.94f)));
    ImGui::PushStyleColor(ImGuiCol_Border,               ThemeColor(g_theme.ui.window_border));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,          ThemeColor(g_theme.ui.scrollbar_bg));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,        ThemeColor(g_theme.ui.scrollbar_grab));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ThemeColor(g_theme.ui.scrollbar_grab_hovered));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,  ThemeColor(g_theme.ui.scrollbar_grab_active));

    const char *win_name = is_left ? "##sidebar_left" : "##sidebar_right";
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin(win_name, NULL, flags))
    {
        int *order = is_left ? g_layout.left_order : g_layout.right_order;
        int max = is_left ? MAX_LEFT_PANELS : MAX_RIGHT_PANELS;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));

        /* Count open panels for fair height distribution */
        int open_count = 0;
        for (int i = 0; i < max; i++)
        {
            int pid = order[i];
            if (pid >= 0 && pid < PANEL_COUNT && g_layout.panel_open[pid])
                open_count++;
        }

        for (int i = 0; i < max; i++)
        {
            int pid = order[i];
            if (pid < 0 || pid >= PANEL_COUNT) continue;

            const PanelDef *def = &g_panel_defs[pid];
            bool is_open = g_layout.panel_open[pid];

            DrawAccordionHeader(def, &g_layout.panel_open[pid], i, is_left);

            if (is_open)
            {
                ImGui::Separator();

                /* Each open panel gets a fair share of the available vertical
                 * space, distributed evenly among all open panels. This prevents
                 * one tall panel from pushing others out of view while ensuring
                 * every panel gets adequate room. */
                float avail_h = ImGui::GetContentRegionAvail().y;
                float spacing_total = (open_count > 1) ? (open_count - 1) * 4.0f : 0.0f;
                float body_h = (avail_h - spacing_total) / (float)open_count;
                body_h = fmaxf(body_h, 100.0f);
                body_h = fminf(body_h, 500.0f);

                char child_id[32];
                snprintf(child_id, sizeof(child_id), "##panel_body_%d", def->id);

                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
                if (ImGui::BeginChild(child_id, ImVec2(0, body_h), false,
                                      ImGuiWindowFlags_AlwaysUseWindowPadding))
                {
                    ImGui::Indent(6.0f);
                    def->draw_content(ctx, cfg);
                    ImGui::Unindent(6.0f);
                }
                ImGui::EndChild();
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
            }
            ImGui::EndMenu();
        }

        /* ---- Tools menu -------------------------------------------------- */
        if (ImGui::BeginMenu("Tools"))
        {
            if (ImGui::BeginMenu("Core Functions"))
            {
                for (int i = 0; i < PANEL_COUNT; i++)
                {
                    const PanelDef *def = &g_panel_defs[i];
                    if (def->category != PANEL_CAT_CORE) continue;
                    if (ImGui::MenuItem(def->title, NULL, &g_layout.panel_open[def->id]))
                        EnsureSidebar(def->default_side);
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Scientific Tools"))
            {
                for (int i = 0; i < PANEL_COUNT; i++)
                {
                    const PanelDef *def = &g_panel_defs[i];
                    if (def->category != PANEL_CAT_SCIENTIFIC) continue;
                    if (ImGui::MenuItem(def->title, NULL, &g_layout.panel_open[def->id]))
                        EnsureSidebar(def->default_side);
                }
                ImGui::Separator();
                /* inspector panels also go in Scientific Tools */
                for (int i = 0; i < PANEL_COUNT; i++)
                {
                    const PanelDef *def = &g_panel_defs[i];
                    if (def->category != PANEL_CAT_INSPECTOR) continue;
                    if (ImGui::MenuItem(def->title, NULL, &g_layout.panel_open[def->id]))
                        EnsureSidebar(def->default_side);
                }
                ImGui::EndMenu();
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

        /* ---- Settings button (right-aligned) ----------------------------- */
        float gear_w = ImGui::CalcTextSize(ICON_FA_GEAR "##settings").x
                       + ImGui::GetStyle().FramePadding.x * 2.0f + 4.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - gear_w - 8.0f);
        if (ImGui::Button(ICON_FA_GEAR "##settings"))
        {
            LayoutOpenSettings();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Settings");

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
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2((float)GetScreenWidth() * 0.5f, (float)GetScreenHeight() * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Settings", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        /* ---- Display section --------------------------------------------- */
        if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Show Statistics", &cfg->show_statistics);
            ImGui::Checkbox("Show Clouds", &cfg->show_clouds);
            ImGui::Checkbox("Night Lights", &cfg->show_night_lights);
            ImGui::Checkbox("Show Markers", &cfg->show_markers);
            ImGui::Checkbox("Scattering", &cfg->show_scattering);
            ImGui::Checkbox("Skybox", &cfg->show_skybox);
            ImGui::Checkbox("VSync", &cfg->hint_vsync);
            ImGui::Checkbox("Highlight Sunlit", &cfg->highlight_sunlit);
            ImGui::Checkbox("Show Slant Range", &cfg->show_slant_range);
        }

        /* ---- Performance section ----------------------------------------- */
        if (ImGui::CollapsingHeader("Performance"))
        {
            int fps = cfg->target_fps;
            if (ImGui::SliderInt("Max FPS", &fps, 15, 240))
                cfg->target_fps = fps;
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

        /* ---- Home Location section --------------------------------------- */
        if (ImGui::CollapsingHeader("Home Location"))
        {
            ImGui::InputText("Name", home_location.name, sizeof(home_location.name));
            ImGui::InputFloat("Latitude", &home_location.lat);
            ImGui::InputFloat("Longitude", &home_location.lon);
            ImGui::InputFloat("Altitude", &home_location.alt);
            if (ImGui::Button("Pick on Map"))
            {
                *ctx->picking_home = !*ctx->picking_home;
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

        /* ---- Buttons ----------------------------------------------------- */
        ImGui::Separator();

        if (ImGui::Button("Save Settings", ImVec2(140, 0)))
        {
            LayoutFillPersist(&cfg->ui_layout);
            SaveAppConfig("settings.json", cfg);
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
    float screen_h = (float)GetScreenHeight();
    float bottom_h = g_layout.show_bottom_bar ? BOTTOM_BAR_H : 0.0f;
    float content_h = screen_h - nav_h - bottom_h;
    if (content_h < 50.0f) content_h = 50.0f;

    if (g_layout.left_visible && !g_layout.left_hidden)
        DrawResizeStrip(true, nav_h, content_h);
    if (g_layout.right_visible && !g_layout.right_hidden)
        DrawResizeStrip(false, nav_h, content_h);

    /* reorder finalisation */
    FinishReorder();
}