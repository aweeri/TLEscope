/*
 * notifications.cpp - KSP-style toast notification system (ROADMAP section 8.1)
 *
 * A small ring buffer of {level, icon, message, timestamp} entries rendered as
 * stacked toasts in the top-right corner via ImGui. Each toast auto-dismisses
 * after a few seconds with a timed fade-out. Notifications are theme-aware
 * (colors come from the Theme struct) and respect UI scale.
 *
 * The module is self-contained: call NotifyPush() from any event point, call
 * NotifyUpdate()/DrawNotifications() each frame from the UI render loop, and
 * optionally persist per-category toggles via the generic tool_settings store.
 */

#include "notifications.h"
#include "ui_layout.h"
#include "tools/tools_settings.h"

#include <imgui.h>
#include <raylib.h>
#include <rlImGui.h>
#include "IconsFontAwesome6.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* -- ring buffer ----------------------------------------------------------- */

typedef struct
{
    NotifyLevel level;
    char icon[8];      /* FontAwesome glyph (may be empty) */
    char message[256];
    float age;         /* seconds since push */
    bool active;
} Notification;

static Notification g_notifs[MAX_NOTIFICATIONS];
static int g_notif_count = 0;

/* -- settings toggles ------------------------------------------------------ */

static bool g_enabled = true;
static bool g_cat_enabled[NOTIFY_LEVEL_COUNT] = {true, true, true, true};

/* tool_settings keys (namespaced under "notify.") */
#define KEY_ENABLED  "notify.enabled"
#define KEY_CAT_INFO "notify.cat.info"
#define KEY_CAT_OK   "notify.cat.success"
#define KEY_CAT_WARN "notify.cat.warning"
#define KEY_CAT_ERR  "notify.cat.error"

/* -- public API ------------------------------------------------------------ */

void NotifyLoadSettings(AppConfig *cfg)
{
    if (!cfg) return;
    g_enabled = ToolSettingGetBool(cfg, KEY_ENABLED, true);
    g_cat_enabled[NOTIFY_INFO]    = ToolSettingGetBool(cfg, KEY_CAT_INFO, true);
    g_cat_enabled[NOTIFY_SUCCESS] = ToolSettingGetBool(cfg, KEY_CAT_OK, true);
    g_cat_enabled[NOTIFY_WARNING] = ToolSettingGetBool(cfg, KEY_CAT_WARN, true);
    g_cat_enabled[NOTIFY_ERROR]   = ToolSettingGetBool(cfg, KEY_CAT_ERR, true);
}

void NotifySaveSettings(AppConfig *cfg)
{
    if (!cfg) return;
    ToolSettingSetBool(cfg, KEY_ENABLED, g_enabled);
    ToolSettingSetBool(cfg, KEY_CAT_INFO, g_cat_enabled[NOTIFY_INFO]);
    ToolSettingSetBool(cfg, KEY_CAT_OK, g_cat_enabled[NOTIFY_SUCCESS]);
    ToolSettingSetBool(cfg, KEY_CAT_WARN, g_cat_enabled[NOTIFY_WARNING]);
    ToolSettingSetBool(cfg, KEY_CAT_ERR, g_cat_enabled[NOTIFY_ERROR]);
}

bool NotifyEnabled(void) { return g_enabled; }
bool NotifyCategoryEnabled(NotifyLevel level)
{
    if (level < 0 || level >= NOTIFY_LEVEL_COUNT) return false;
    return g_cat_enabled[level];
}

void NotifySetEnabled(bool enabled) { g_enabled = enabled; }
void NotifySetCategoryEnabled(NotifyLevel level, bool enabled)
{
    if (level < 0 || level >= NOTIFY_LEVEL_COUNT) return;
    g_cat_enabled[level] = enabled;
}

void NotifyPush(NotifyLevel level, const char *icon, const char *fmt, ...)
{
    if (!g_enabled) return;
    if (level < 0 || level >= NOTIFY_LEVEL_COUNT) return;
    if (!g_cat_enabled[level]) return;
    if (!fmt || fmt[0] == '\0') return;

    /* if the buffer is full, drop the oldest entry */
    if (g_notif_count >= MAX_NOTIFICATIONS)
    {
        /* shift everything down by one */
        for (int i = 1; i < MAX_NOTIFICATIONS; i++)
            g_notifs[i - 1] = g_notifs[i];
        g_notif_count = MAX_NOTIFICATIONS - 1;
    }

    Notification *n = &g_notifs[g_notif_count++];
    n->level = level;
    n->age = 0.0f;
    snprintf(n->icon, sizeof(n->icon), "%s", icon ? icon : "");

    /* format the message with printf-style args */
    va_list args;
    va_start(args, fmt);
    vsnprintf(n->message, sizeof(n->message), fmt, args);
    va_end(args);
}

void NotifyUpdate(float dt)
{
    if (dt <= 0.0f) return;

    /* advance ages and drop expired entries */
    int write = 0;
    for (int i = 0; i < g_notif_count; i++)
    {
        g_notifs[i].age += dt;
        if (g_notifs[i].age < NOTIFY_DURATION + NOTIFY_FADE)
        {
            if (write != i)
                g_notifs[write] = g_notifs[i];
            write++;
        }
    }
    g_notif_count = write;
}

/* -- rendering ------------------------------------------------------------- */

static ImVec4 ThemeColor(const Color &c)
{
    return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

/** resolve the accent color for a level from the theme */
static ImVec4 LevelColor(NotifyLevel level)
{
    switch (level)
    {
        case NOTIFY_SUCCESS: return ThemeColor(g_theme.ui.notif_success);
        case NOTIFY_WARNING: return ThemeColor(g_theme.ui.notif_warning);
        case NOTIFY_ERROR:   return ThemeColor(g_theme.ui.notif_error);
        case NOTIFY_INFO:
        default:             return ThemeColor(g_theme.ui.notif_info);
    }
}

void DrawNotifications(void)
{
    if (!g_enabled || g_notif_count == 0) return;

    ImGuiIO &io = ImGui::GetIO();
    float screen_w = io.DisplaySize.x;

    /* position toasts in the corner of the raylib viewport, respecting the
     * top nav bar and the right sidebar (ROADMAP 8.1) */
    float margin = 12.0f;
    float top = ImGui::GetFrameHeight() + margin;   /* below the nav bar */
    float right_edge = screen_w - margin;
    if (g_layout.right_visible && !g_layout.right_hidden)
        right_edge -= g_layout.right_width;          /* left of the right sidebar */
    float y = top;

    /* stack newest at the bottom so the newest toast is most visible */
    for (int i = 0; i < g_notif_count; i++)
    {
        Notification *n = &g_notifs[i];

        /* fade-out near the end of the lifetime */
        float alpha = 1.0f;
        if (n->age > NOTIFY_DURATION)
        {
            float t = (n->age - NOTIFY_DURATION) / NOTIFY_FADE;
            alpha = 1.0f - t;
            if (alpha < 0.0f) alpha = 0.0f;
        }

        /* measure the message so the toast sizes to its content */
        ImVec2 text_sz = ImGui::CalcTextSize(n->message);
        float pad_x = 12.0f, pad_y = 8.0f;
        float icon_w = (n->icon[0] != '\0') ? ImGui::GetFrameHeight() + 6.0f : 0.0f;
        float w = text_sz.x + icon_w + pad_x * 2.0f;
        float h = text_sz.y + pad_y * 2.0f;
        if (w < 120.0f) w = 120.0f;

        float x = right_edge - w;

        /* toast window: no decoration, no focus, no input -> never blocks. */
        char win_id[32];
        snprintf(win_id, sizeof(win_id), "##notif_%d", i);
        ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(alpha);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ThemeColor(g_theme.ui.notif_bg));
        ImGui::PushStyleColor(ImGuiCol_Border, ThemeColor(g_theme.ui.notif_border));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad_x, pad_y));
        /* scale the alpha of ALL content */
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

        ImGui::Begin(win_id, NULL,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);

        /* accent bar on the left edge */
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        ImVec4 accent_col = LevelColor(n->level);
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(pos.x, pos.y + 2.0f),
            ImVec2(pos.x + 4.0f, pos.y + size.y - 2.0f),
            IM_COL32((int)(accent_col.x * 255), (int)(accent_col.y * 255),
                     (int)(accent_col.z * 255), (int)(alpha * 255)));

        /* icon + message */
        if (n->icon[0] != '\0')
        {
            ImGui::TextColored(LevelColor(n->level), "%s", n->icon);
            ImGui::SameLine();
        }
        ImGui::TextWrapped("%s", n->message);

        ImGui::End();

        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor(2);

        y += h + 8.0f;
    }
}