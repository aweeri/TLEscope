/*
 * ui.cpp - Dear ImGui UI implementation
 *
 * This file implements the UI layer using Dear ImGui + rlImGui.
 * It contains the modal dialogs (first-run, exit, data warning, update
 * check, help, about) and the main DrawGUI entry point. The sidebar /
 * panel workspace lives in ui_layout.cpp; the tool panels live in
 * ui/tools/.
 */

#include "ui.h"
#include "ui_layout.h"
#include "ui_input.h"
#include "labels.h"
#include "tools/tools.h"
#include "notifications.h"
#include "hints.h"
#include "tutorial.h"
#include "core/astro.h"
#include "core/config.h"
#include "core/theme.h"
#include "imgui_theme.h"
#include "data/storage.h"
#include "data/async_fetch.h"
#include "util/log.h"
#include "tools/tools_common.h"
#include "tools/tools_settings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <ctime>
#include <cmath>
#include <thread>
#include <atomic>

#include <raylib.h>
#include <raymath.h>

#include "imgui.h"
#include "imgui_internal.h"   /* GetTopMostPopupModal() for the Escape router */
#include "rlImGui.h"
#include "IconsFontAwesome6.h"

/* -- UIState instance ------------------------------------------------------ */

UIState g_ui = {
    .ra_format = 0,   /* default: decimal degrees (compact) */
    .dec_format = 0,  /* default: decimal degrees (compact) */
    .selected_pass_idx = -1, /* no pass selected by default */
};

/* -- Helpers --------------------------------------------------------------- */

Color ApplyAlpha(Color c, float alpha)
{
    c.a = (unsigned char)(c.a * alpha);
    return c;
}

void DrawUIText(Font font, const char *text, float x, float y, float size, Color color)
{
    Vector2 pos = {x, y};
    DrawTextEx(font, text, pos, size, 1, color);
}

/* clamp a time multiplier to the symmetric magnitude limit, preserving sign */
double ClampTimeMultiplier(double v)
{
    if (v > MAX_TIME_MULTIPLIER)
        return MAX_TIME_MULTIPLIER;
    if (v < -MAX_TIME_MULTIPLIER)
        return -MAX_TIME_MULTIPLIER;
    return v;
}

double StepTimeMultiplier(double current, bool increase)
{
    /* snap to zero at 0.25 so the step sequence crosses 0 without stalling */
    const double eps = 1e-9;
    const double snap_threshold = 0.25;
    double next;

    if (increase)
    {
        if (fabs(current) < eps)
            next = 0.5;
        else if (current < 0.0)
        {
            next = current / 2.0;
            if (fabs(next) <= snap_threshold)
                next = 0.0;
        }
        else
        {
            next = current * 2.0;
        }
    }
    else
    {
        if (fabs(current) < eps)
            next = -0.5;
        else if (current > 0.0)
        {
            next = current / 2.0;
            if (next <= snap_threshold)
                next = 0.0;
        }
        else
        {
            next = current * 2.0;
        }
    }

    return ClampTimeMultiplier(next);
}

/* -- Modal state (static to this file) ------------------------------------- */

static bool show_help = false;
static bool show_about = false;
static bool show_tle_warning = false;
static bool show_exit_dialog = false;

/* -- Modal visibility accessors -------------------------------------------- */

void UIOpenSettings(void) { LayoutOpenSettings(); }
void UIOpenHelp(void) { show_help = true; }
void UIOpenAbout(void) { show_about = true; }
void UIRequestExit(void) { show_exit_dialog = true; }

bool UIModalAnyOpen(void)
{
    return show_help || show_about || show_tle_warning || show_exit_dialog ||
           g_ui.update_available || g_ui.transit_overview_open ||
           LayoutSettingsOpen() || LayoutToolsOpen();
}

void UICloseTopModal(AppConfig *cfg)
{
    ImGuiWindow *top = ImGui::GetTopMostPopupModal();
    if (!top || !top->Name)
        return;

    /* popup name must match the string passed to BeginPopupModal()/OpenPopup() */
    struct { const char *name; bool *flag; } kModalFlags[] = {
        { "Exit?",                &show_exit_dialog          },
        { "Help",                 &show_help                 },
        { "About TLEscope",       &show_about                },
        { "Re-pull stale data",   &show_tle_warning          },
        { "Update Available",     &g_ui.update_available     },
        { "Transit Overview",     &g_ui.transit_overview_open },
        { "Settings",             &g_layout.settings_open    },
        { "Manage Tools",         &g_layout.tools_open       },
    };

    for (const auto &entry : kModalFlags)
    {
        if (strcmp(top->Name, entry.name) == 0)
        {
            *entry.flag = false;
            return;
        }
    }
}

/* -- Time-control helpers -------------------------------------------------- */

/** number of days in a month (leap-aware). mon is 1-12. */
static int DaysInMonth(int year, int mon)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mon < 1) mon = 1;
    if (mon > 12) mon = 12;
    int n = days[mon - 1];
    if (mon == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
        n = 29;
    return n;
}

/** day-of-year (1-366) -> month (1-12) + day-of-month (1-31). */
static void DoyToMonthDay(int year, int doy, int *mon, int *mday)
{
    if (doy < 1) doy = 1;
    int m = 1;
    while (m < 12 && doy > DaysInMonth(year, m))
    {
        doy -= DaysInMonth(year, m);
        m++;
    }
    *mon = m;
    *mday = doy;
}

/** month (1-12) + day-of-month (1-31) -> day-of-year (1-366). */
static int MonthDayToDoy(int year, int mon, int mday)
{
    int doy = 0;
    for (int m = 1; m < mon; m++)
        doy += DaysInMonth(year, m);
    return doy + mday;
}

/** day of week for a date; 0 = Sunday (Sakamoto's algorithm). */
static int DayOfWeek(int y, int m, int d)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    int w = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
    if (w < 0) w += 7;
    return w;
}

/* calendar month grid for the clock popup; edits year/day-of-year on click, returns true on change */
static bool DrawCalendarBody(int *year, int *doy, int *cal_year, int *cal_mon, int first_day)
{
    int mon, mday;
    DoyToMonthDay(*year, *doy, &mon, &mday);

    bool changed = false;

    /* month navigation */
    if (ImGui::ArrowButton("##prevmonth", ImGuiDir_Left))
    {
        if (--(*cal_mon) < 1) { *cal_mon = 12; --(*cal_year); }
    }
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::Text("%04d-%02d", *cal_year, *cal_mon);
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::ArrowButton("##nextmonth", ImGuiDir_Right))
    {
        if (++(*cal_mon) > 12) { *cal_mon = 1; ++(*cal_year); }
    }

    ImGui::Separator();

    /* weekday header + day grid; all columns share one width so the header aligns */
    static const char *kWeekdays[7] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    float cell_w = ImGui::CalcTextSize("30").x + 2.0f * ImGui::GetStyle().FramePadding.x;
    if (ImGui::BeginTable("##calgrid", 7, ImGuiTableFlags_SizingFixedSame))
    {
        for (int i = 0; i < 7; i++)
        {
            ImGui::TableNextColumn();
            const char *wd = kWeekdays[(i + first_day) % 7];
            float w = ImGui::CalcTextSize(wd).x;
            if (cell_w > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cell_w - w) * 0.5f);
            ImGui::TextDisabled("%s", wd);
        }

        int first_wd = DayOfWeek(*cal_year, *cal_mon, 1);          /* 0 = Sunday */
        int offset   = (first_wd - first_day + 7) % 7;             /* leading blanks */
        int dim      = DaysInMonth(*cal_year, *cal_mon);
        for (int i = 0; i < offset; i++)
            ImGui::TableNextColumn();
        for (int day = 1; day <= dim; day++)
        {
            ImGui::TableNextColumn();
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "%d##d", day);
            bool selected = (day == mday);
            if (selected)
            {
                /* highlight the currently-set day with the theme accent */
                ImGui::PushStyleColor(ImGuiCol_Button,        ThemeColor(g_theme.ui.accent));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ThemeColor(g_theme.ui.accent));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ThemeColor(g_theme.ui.accent));
                ImGui::PushStyleColor(ImGuiCol_Text,          ThemeColor(g_theme.ui.bg));
            }
            if (ImGui::Button(lbl, ImVec2(cell_w, 0.0f)))
            {
                *year = *cal_year;
                *doy  = MonthDayToDoy(*cal_year, *cal_mon, day);
                changed = true;
            }
            if (selected)
                ImGui::PopStyleColor(4);
        }
        ImGui::EndTable();
    }

    return changed;
}

static bool DrawHMSField(int *hour, int *minute, int *second, char *buf, size_t buf_sz, float width)
{
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputText("##hms", buf, buf_sz, ImGuiInputTextFlags_CharsNoBlank))
    {
        int h = 0, m = 0, s = 0;
        if (sscanf(buf, "%d:%d:%d", &h, &m, &s) == 3)
        {
            if (h < 0) h = 0;
            if (h > 23) h = 23;
            if (m < 0) m = 0;
            if (m > 59) m = 59;
            if (s < 0) s = 0;
            if (s > 59) s = 59;
            *hour = h;
            *minute = m;
            *second = s;
            changed = true;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("HH:MM:SS");
    return changed;
}

/* -- Live badge ------------------------------------------------------------ */

// shared badge geometry so the painted indicator matches the reserved width
static const float kBadgeDotR = 3.0f;
static const float kBadgeGap  = 5.0f;

// fixed width (widest label) so the transport cluster never shifts
static float LiveBadgeWidth(void)
{
    return 2.0f * UIPx(kBadgeDotR) + UIPx(kBadgeGap) + ImGui::CalcTextSize("PREDICTION").x;
}

// minimal status indicator: dot + uppercase state label
static void DrawLiveBadge(bool is_live, bool is_paused, float height)
{
    const char *label = is_live ? "LIVE" : (is_paused ? "PAUSED" : "PREDICTION");

    const float dot_r = UIPx(kBadgeDotR);
    const float gap   = UIPx(kBadgeGap);
    float w = LiveBadgeWidth();
    float h = height;

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));

    ImDrawList *dl = ImGui::GetWindowDrawList();

    /* live is the only active state; everything else stays muted */
    Color dot = is_live ? g_theme.ui.success : g_theme.ui.text_dim;

    ImVec2 text_sz = ImGui::CalcTextSize(label);
    float group_w = 2.0f * dot_r + gap + text_sz.x;
    float group_x = p.x + (w - group_w) * 0.5f;

    ImVec2 dot_c = ImVec2(group_x + dot_r, p.y + h * 0.5f);
    dl->AddCircleFilled(dot_c, dot_r, ImGui::GetColorU32(ThemeColor(dot)), 16);

    ImVec2 tp = ImVec2(group_x + 2.0f * dot_r + gap, p.y + (h - text_sz.y) * 0.5f);
    dl->AddText(tp, ImGui::GetColorU32(ThemeColor(g_theme.ui.text_dim)), label);
}

/* -- Bottom Center Time Bar ------------------------------------------------ */

static void DrawBottomBar(UIContext *ctx, AppConfig *cfg)
{
    if (!LayoutBottomBarVisible()) return;

    ImGuiStyle &style = ImGui::GetStyle();

    /* use ImGui's display size (consistent with sidebar layout in ui_layout.cpp) */
    float screen_w = ImGui::GetIO().DisplaySize.x;
    float screen_h = ImGui::GetIO().DisplaySize.y;

    float row_h   = ImGui::GetFrameHeight();   /* standard widget row height */
    float btn_sz  = row_h;                     /* square icon buttons        */
    float spacing = style.ItemSpacing.x;
    float row_gap = fminf(style.ItemSpacing.y, 6.0f);
    float pad_x   = style.WindowPadding.x;
    float pad_y   = fminf(style.WindowPadding.y, 6.0f);

    /* simulation time display (uses simulation epoch, not wall clock) */
    char time_str[64];
    epoch_to_datetime_str(*ctx->current_epoch, time_str);

    double real_now  = get_current_real_time_epoch_hr();
    double drift_sec = fabs(*ctx->current_epoch - real_now) * 86400.0;
    bool is_paused = (*ctx->time_multiplier == 0.0);
    bool is_live = (!*ctx->is_auto_warping) && (*ctx->time_multiplier == 1.0) && (drift_sec < LIVE_WINDOW_SEC);

    char speed_str[32];
    double mult = *ctx->time_multiplier;
    if (fabs(mult) < 1e-9)
        snprintf(speed_str, sizeof(speed_str), "0.0x");
    else
        snprintf(speed_str, sizeof(speed_str), "%.1fx", mult);

    float time_w = ImGui::CalcTextSize("00:00:00").x + 4.0f * style.FramePadding.x;

    float badge_w = LiveBadgeWidth();
    float play_w  = 4.0f * btn_sz + 3.0f * spacing;   /* backward | play/pause | forward | now */

    /* stable reserve so the transport cluster never jumps as the digits change */
    float time_reserve = ImGui::CalcTextSize("0000-00-00 00:00:00 UTC").x;
    float time_w_measured = ImGui::CalcTextSize(time_str).x;
    if (time_w_measured > time_reserve) time_reserve = time_w_measured;

    float speed_w = ImGui::CalcTextSize(speed_str).x;

    /* transport cluster: timestamp | [<<][play][>>][now] | [clock] | speed */
    float transport_w = time_reserve + spacing + play_w + spacing + speed_w +
                        spacing + btn_sz;

    float need = fmaxf(transport_w, badge_w);
    float win_w = need + 2.0f * pad_x;
    if (win_w > screen_w) win_w = screen_w;

    float content_h = 2.0f * row_h + row_gap;   /* badge row + transport row */
    float win_h = content_h + 2.0f * pad_y;
    float y  = screen_h - win_h;
    float x0 = (screen_w - win_w) * 0.5f;
    if (x0 < 0.0f) x0 = 0.0f;

    ImGui::SetNextWindowPos(ImVec2(x0, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(win_w, win_h));

    /* fully transparent: only the widgets themselves draw, no bar background */
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground;

    /* pending date/time edit fields for the clock popup */
    static int  s_year = 2000, s_day = 1, s_hour = 0, s_min = 0, s_sec = 0;
    static char s_hms_buf[16] = "00:00:00";
    static int  s_cal_year = 2000, s_cal_mon = 1;

    /* window-relative row offsets */
    float badge_y     = pad_y;
    float transport_y = pad_y + row_h + row_gap;

    if (ImGui::Begin("##bottombar", NULL, flags))
    {
        /* ---- TRANSPORT ROW: timestamp + [<<] [play/pause] [>>] [now] + [clock] + speed ---- */
        ImGui::SetCursorPos(ImVec2(pad_x, transport_y));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ThemeColor(g_theme.ui.text_dim), "%s", time_str);

        /* transport: slow down / reverse, play-pause, accelerate, jump to now */
        ImGui::SameLine(0.0f, spacing);
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.text_dim));
        if (ImGui::Button(ICON_FA_BACKWARD "##backward", ImVec2(btn_sz, btn_sz)))
        {
            *ctx->time_multiplier = StepTimeMultiplier(*ctx->time_multiplier, false);
            NotifyPush(NOTIFY_INFO, ICON_FA_BACKWARD, "Time slowed");
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Slow down / reverse time");
        ImGui::SameLine(0.0f, spacing);

        /* play/pause */
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.accent));
        if (ImGui::Button(is_paused ? (ICON_FA_PLAY "##playpause") : (ICON_FA_PAUSE "##playpause"), ImVec2(btn_sz, btn_sz)))
        {
            if (is_paused)
            {
                *ctx->time_multiplier = (*ctx->saved_multiplier != 0.0) ? *ctx->saved_multiplier : 1.0;
                NotifyPush(NOTIFY_INFO, ICON_FA_PLAY, "Time resumed");
            }
            else
            {
                *ctx->saved_multiplier = *ctx->time_multiplier; *ctx->time_multiplier = 0.0;
                NotifyPush(NOTIFY_INFO, ICON_FA_PAUSE, "Time paused");
            }
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(is_paused ? "Resume" : "Pause");
        /* play/pause center, used to center the state badge above the button */
        float pause_cx = (ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f - ImGui::GetWindowPos().x;
        ImGui::SameLine(0.0f, spacing);

        /* accelerate */
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.text_dim));
        if (ImGui::Button(ICON_FA_FORWARD "##forward", ImVec2(btn_sz, btn_sz)))
        {
            *ctx->time_multiplier = StepTimeMultiplier(*ctx->time_multiplier, true);
            NotifyPush(NOTIFY_INFO, ICON_FA_FORWARD, "Time sped up");
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Speed up time");
        ImGui::SameLine(0.0f, spacing);

        /* jump to now and run live */
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.accent));
        if (ImGui::Button(ICON_FA_ARROW_ROTATE_LEFT "##reset", ImVec2(btn_sz, btn_sz)))
        {
            *ctx->current_epoch = get_current_real_time_epoch();
            *ctx->time_multiplier = 1.0;
            NotifyPush(NOTIFY_INFO, ICON_FA_CLOCK, "Time reset to now");
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Jump to now and run live");

        /* clock button: opens the date + time editor popup, left of the speed factor */
        ImGui::SameLine(0.0f, spacing);
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.text_dim));
        if (ImGui::Button(ICON_FA_CLOCK "##timeedit", ImVec2(btn_sz, btn_sz)))
        {
            epoch_to_local_fields(*ctx->current_epoch, &s_year, &s_day, &s_hour, &s_min, &s_sec);
            snprintf(s_hms_buf, sizeof(s_hms_buf), "%02d:%02d:%02d", s_hour, s_min, s_sec);
            int mday;
            DoyToMonthDay(s_year, s_day, &s_cal_mon, &mday);
            s_cal_year = s_year;
            ImGui::OpenPopup("##timepop");
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Set simulation date & time");

        /* speed multiplier, sitting right of the clock button */
        ImGui::SameLine(0.0f, spacing);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ThemeColor(g_theme.ui.accent), "%s", speed_str);

        /* ---- BADGE ROW: live state, centered above the play/pause button ---- */
        float badge_x = pause_cx - badge_w * 0.5f;
        if (badge_x < pad_x) badge_x = pad_x;
        ImGui::SetCursorPos(ImVec2(badge_x, badge_y));
        DrawLiveBadge(is_live, is_paused, row_h);

        /* clock popup: calendar + time field + Apply/Reset/Close */
        if (ImGui::BeginPopup("##timepop"))
        {
            /* header row: title left, close X right */
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ThemeColor(g_theme.ui.text_dim), "Date & Time");
            ImGui::SameLine();
            float top_x_off = ImGui::GetContentRegionAvail().x - btn_sz;
            if (top_x_off > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + top_x_off);
            if (ImGui::Button(ICON_FA_XMARK "##timecancel_top", ImVec2(btn_sz, 0.0f)))
            {
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close without applying");
            ImGui::Separator();

            DrawCalendarBody(&s_year, &s_day, &s_cal_year, &s_cal_mon, cfg->first_day_of_week);

            ImGui::Separator();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ThemeColor(g_theme.ui.text_dim), "Time");
            ImGui::SameLine(0.0f, spacing);
            DrawHMSField(&s_hour, &s_min, &s_sec, s_hms_buf, sizeof(s_hms_buf), time_w);

            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.accent));
            if (ImGui::Button("Apply", ImVec2(3.0f * btn_sz, 0.0f)))
            {
                *ctx->current_epoch = local_fields_to_epoch(s_year, s_day, s_hour, s_min, s_sec);
                // leaving the wall clock on purpose: drop the live lock so the main loop doesn't snap back to "now"
                if (ctx->live_follow)
                    *ctx->live_follow = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Apply the selected date/time to the simulation");

            ImGui::SameLine(0.0f, spacing);
            if (ImGui::Button("Reset", ImVec2(3.0f * btn_sz, 0.0f)))
            {
                epoch_to_local_fields(get_current_real_time_epoch(), &s_year, &s_day, &s_hour, &s_min, &s_sec);
                snprintf(s_hms_buf, sizeof(s_hms_buf), "%02d:%02d:%02d", s_hour, s_min, s_sec);
                int mday;
                DoyToMonthDay(s_year, s_day, &s_cal_mon, &mday);
                s_cal_year = s_year;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Load current real time into the fields (does not apply)");

            ImGui::SameLine(0.0f, spacing);
            if (ImGui::Button(ICON_FA_XMARK "##timecancel", ImVec2(btn_sz, 0.0f)))
            {
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close without applying");

            ImGui::EndPopup();
        }

        /* capture actual notch rect for sidebar layout (ui_layout reads this) */
        ImVec2 bb_pos  = ImGui::GetWindowPos();
        ImVec2 bb_size = ImGui::GetWindowSize();
        g_layout.bottom_bar_x   = bb_pos.x;
        g_layout.bottom_bar_w   = bb_size.x;
        g_layout.bottom_bar_top = bb_pos.y;
    }
    ImGui::End();
}

static void DrawHiddenClockOverlay(UIContext *ctx, AppConfig *cfg)
{
    if (!g_layout.clean_view) return;
    if (!ToolSettingGetBool(cfg, "view.time_when_hidden", false)) return;
    if (!ctx || !ctx->current_epoch) return;

    char time_str[32];
    epoch_to_time_str(*ctx->current_epoch, time_str);

    /* date is the first 10 chars ("YYYY-MM-DD") of the full datetime string */
    char datetime_str[64];
    char date_str[16];
    epoch_to_datetime_str(*ctx->current_epoch, datetime_str);
    snprintf(date_str, sizeof(date_str), "%.10s", datetime_str);

    /* lower-left of the viewport, hugging the edge */
    float vp_x, vp_y, vp_w, vp_h;
    LayoutGetViewportRect(&vp_x, &vp_y, &vp_w, &vp_h);
    const float margin = 8.0f;

    /* pivot (0,1) anchors the window's bottom-left corner to the target point */
    ImGui::SetNextWindowPos(ImVec2(vp_x + margin, vp_y + vp_h - margin),
                            ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##hidden_clock", NULL, flags))
    {
        ImVec4 col = ThemeColor(g_theme.ui.text_dim);
        ImGui::TextColored(col, "%s", time_str);
        ImGui::TextColored(col, "%s", date_str);
    }
    ImGui::End();
}

/* -- Help Modal ------------------------------------------------------------ */

static void DrawHelpModal(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    (void)cfg;
    if (show_help)
        ImGui::OpenPopup("Help");

    if (ImGui::BeginPopupModal("Help", &show_help, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("TLEscope v%s", TLESCOPE_VERSION);
        ImGui::Separator();
        ImGui::Text("Controls:");
        ImGui::BulletText("Space: Pause/Resume time");
        ImGui::BulletText("Scroll: Zoom in/out");
        ImGui::BulletText("Middle mouse: Pan");
        ImGui::BulletText("Left click: Select satellite");
        ImGui::BulletText("Right click: Orbit camera");
        ImGui::Separator();
        ImGui::Text("Keyboard Shortcuts:");
        ImGui::BulletText("M: Toggle 2D/3D");
        ImGui::BulletText("H: Toggle clean view (hide/show panels)");
        ImGui::Separator();
        if (ImGui::Button("GitHub Repository"))
        {
            OpenURL("https://github.com/aweeri/TLEscope");
        }
        ImGui::SameLine();
        if (ImGui::Button("Close"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

/* -- About Modal ----------------------------------------------------------- */

static void DrawAboutModal(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    (void)cfg;
    if (show_about)
        ImGui::OpenPopup("About TLEscope");

    if (ImGui::BeginPopupModal("About TLEscope", &show_about, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("TLEscope v%s", TLESCOPE_VERSION);
        ImGui::Separator();
        ImGui::TextWrapped("A real-time satellite tracking and orbit simulation tool.");
        ImGui::TextWrapped("TLE / OMM orbital data, 3D globe, passes, polar plots and more.");
        ImGui::Separator();
        if (ImGui::Button("GitHub Repository"))
        {
            OpenURL("https://github.com/aweeri/TLEscope");
        }
        ImGui::SameLine();
        if (ImGui::Button("Close"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

/* -- TLE Warning / staleness orchestrator ---------------------------------- */

static void DrawDataWarning(UIContext *ctx, AppConfig *cfg)
{
    /* Behavior 2: "Never" disables every staleness check (no prompt, no pull). */
    if (cfg->data_stale_threshold_seconds <= STALE_THRESHOLD_NEVER)
    {
        show_tle_warning = false;
        return;
    }

    // anti-nag for 24/7: prompt at most once per session, honour a "No" cool-down; both re-arm on fresh data
    static bool   s_prompted_this_session = false;
    static bool   s_auto_prompted = false;  /* the open popup was opened by us */
    static time_t s_snooze_until = 0;

    // compute staleness unconditionally so the auto path works without the manual ] trigger; unknown ages count as fresh
    time_t now = time(NULL);
    bool data_is_old = false;
    for (int i = 0; i < sat_count; i++)
    {
        if (satellites[i].data_meta.fetch_time > 0 &&
            (now - satellites[i].data_meta.fetch_time) > cfg->data_stale_threshold_seconds)
        {
            data_is_old = true;
            break;
        }
    }

    // fresh data re-arms the prompt and closes an auto-opened popup; a manual ] popup is left alone
    if (!data_is_old)
    {
        s_prompted_this_session = false;
        s_snooze_until = 0;
        if (s_auto_prompted)
        {
            show_tle_warning = false;
            s_auto_prompted = false;
        }
    }

    int threshold_secs = cfg->data_stale_threshold_seconds;
    const char *threshold_str = "2 days";
    if (threshold_secs <= STALE_THRESHOLD_6H) threshold_str = "6 hours";
    else if (threshold_secs <= STALE_THRESHOLD_12H) threshold_str = "12 hours";
    else if (threshold_secs <= STALE_THRESHOLD_1D) threshold_str = "1 day";
    else if (threshold_secs <= STALE_THRESHOLD_2D) threshold_str = "2 days";
    else if (threshold_secs <= STALE_THRESHOLD_3D) threshold_str = "3 days";
    else if (threshold_secs <= STALE_THRESHOLD_5D) threshold_str = "5 days";
    else threshold_str = "7 days";

    // auto prompt/pull: only when stale, not prompted this session, not snoozed, no fetch running, no other modal
    if (data_is_old && !s_prompted_this_session && now >= s_snooze_until &&
        !AsyncFetchBusy() && !DataSourcesPullInProgress() && !UIModalAnyOpen())
    {
        if (cfg->auto_repull_stale)
        {
            // auto-pull instead of prompting; the completion toast is emitted by DataSourcesPollPullCompletion()
            if (DataSourcesStartPull(cfg, ctx))
                DataSourcesSetAutoPull(true);
            // mark prompted even on failure so a 24/7 instance doesn't retry every frame
            s_prompted_this_session = true;
        }
        else
        {
            /* Behavior 1: prompt instead of silently downloading. */
            s_prompted_this_session = true;
            s_auto_prompted = true;
            show_tle_warning = true;
        }
    }

    /* the `]` key (ToggleTLEWarning) also drives show_tle_warning */
    if (show_tle_warning)
        ImGui::OpenPopup("Re-pull stale data");

    const char *title = ICON_FA_ROTATE "  Re-pull stale data?";
    char line1[192];
    snprintf(line1, sizeof(line1), "Your orbital data is older than %s.", threshold_str);
    const char *line2 = "Would you like to re-pull it now?";

    const float pad = UIPx(16.0f);
    const float btn_w = UIPx(140.0f);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float buttons_w = btn_w * 2.0f + spacing;

    const float title_w = ImGui::CalcTextSize(title).x;
    const float line1_w = ImGui::CalcTextSize(line1).x;
    const float line2_w = ImGui::CalcTextSize(line2).x;

    float content_w = title_w;
    if (line1_w > content_w) content_w = line1_w;
    if (line2_w > content_w) content_w = line2_w;
    if (buttons_w > content_w) content_w = buttons_w;
    const float dialog_w = content_w + pad * 2.0f;

    ImGui::SetNextWindowSize(ImVec2(dialog_w, 0), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((float)GetScreenWidth() * 0.5f, (float)GetScreenHeight() * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));

    if (ImGui::BeginPopupModal("Re-pull stale data", &show_tle_warning, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const float win_w = ImGui::GetWindowWidth();

        ImGui::SetCursorPosX((win_w - title_w) * 0.5f);
        ImGui::TextColored(ThemeColor(g_theme.ui.warning), "%s", title);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::SetCursorPosX((win_w - line1_w) * 0.5f);
        ImGui::TextUnformatted(line1);
        ImGui::SetCursorPosX((win_w - line2_w) * 0.5f);
        ImGui::TextUnformatted(line2);

        ImGui::Spacing();

        ImGui::SetCursorPosX((win_w - buttons_w) * 0.5f);

        if (ImGui::Button("Don't Update", ImVec2(btn_w, 0.0f)))
        {
            /* snooze for one threshold interval before prompting again */
            s_snooze_until = now + cfg->data_stale_threshold_seconds;
            show_tle_warning = false;
            s_auto_prompted = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,        ThemeColor(g_theme.ui.accent));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ThemeColor(ThemeHoverOf(g_theme.ui.accent, g_theme.ui.text)));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ThemeColor(ThemeActiveOf(g_theme.ui.accent, g_theme.ui.text)));
        ImGui::PushStyleColor(ImGuiCol_Text,          ThemeColor(g_theme.ui.bg));
        if (ImGui::Button("Update", ImVec2(btn_w, 0.0f)))
        {
            DataSourcesStartPull(cfg, ctx);
            show_tle_warning = false;
            s_auto_prompted = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(4);
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar();
}

/* -- Update Check Notification ---------------------------------------------- */

static void DrawUpdateCheck(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    (void)cfg;

    static double popup_start = 0.0;
    if (g_ui.update_available && popup_start == 0.0) popup_start = GetTime();

    if (g_ui.update_available && GetTime() - popup_start < 10.0)
        ImGui::OpenPopup("Update Available");

    if (ImGui::BeginPopupModal("Update Available", &g_ui.update_available, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("A new version of TLEscope is available!");
        ImGui::Text("%s", g_ui.latest_version_str);

        if (ImGui::Button("Download", ImVec2(120, 0)))
        {
            OpenURL("https://github.com/aweeri/TLEscope/releases/latest");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Dismiss", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

/* -- Exit Dialog ----------------------------------------------------------- */

static void DrawExitDialog(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    if (show_exit_dialog)
        ImGui::OpenPopup("Exit?");

    const char *title = ICON_FA_DOOR_OPEN "  Exit TLEscope?";
    const char *message = "Are you sure you want to close TLEscope?";

    const float pad = UIPx(16.0f);
    const float btn_w = UIPx(140.0f);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float buttons_w = btn_w * 2.0f + spacing;

    const float title_w = ImGui::CalcTextSize(title).x;
    const float msg_w = ImGui::CalcTextSize(message).x;

    float content_w = title_w;
    if (msg_w > content_w) content_w = msg_w;
    if (buttons_w > content_w) content_w = buttons_w;
    const float dialog_w = content_w + pad * 2.0f;

    ImGui::SetNextWindowSize(ImVec2(dialog_w, 0), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((float)GetScreenWidth() * 0.5f, (float)GetScreenHeight() * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));

    /* p_open wired to show_exit_dialog so the Escape router can close it */
    if (ImGui::BeginPopupModal("Exit?", &show_exit_dialog, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const float win_w = ImGui::GetWindowWidth();

        ImGui::SetCursorPosX((win_w - title_w) * 0.5f);
        ImGui::TextColored(ThemeColor(g_theme.ui.warning), "%s", title);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::SetCursorPosX((win_w - msg_w) * 0.5f);
        ImGui::TextUnformatted(message);

        ImGui::Spacing();

        ImGui::SetCursorPosX((win_w - buttons_w) * 0.5f);

        if (ImGui::Button("Cancel", ImVec2(btn_w, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,        ThemeColor(g_theme.ui.error));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ThemeColor(ThemeHoverOf(g_theme.ui.error, g_theme.ui.text)));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ThemeColor(ThemeActiveOf(g_theme.ui.error, g_theme.ui.text)));
        ImGui::PushStyleColor(ImGuiCol_Text,          ThemeColor(g_theme.ui.bg));
        if (ImGui::Button("Exit", ImVec2(btn_w, 0.0f)))
        {
            *ctx->exit_app = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(4);
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar();
}

/* -- Main DrawGUI ---------------------------------------------------------- */

void DrawGUI(UIContext *ctx, AppConfig *cfg, Font customFont)
{
    /* one-time ImGui init; theme colors/style/fonts are also applied by main.cpp on theme switch */
    static bool imgui_inited = false;
    static float last_ui_scale = 0.0f;
    static float last_dpi_scale = 0.0f;
    static float last_display_w = 0.0f;
    static float last_display_h = 0.0f;
    /* pending ui_scale whose font-atlas rebuild is debounced while +/- is held; 0 = none */
    static float pending_ui_scale = 0.0f;
    static float ui_scale_idle = 0.0f;

    const float dpi_scale = ThemeDevicePixelScale();
    /* logical display size (same space io.DisplaySize is normalised to below) */
    const float display_w = (float)GetScreenWidth();
    const float display_h = (float)GetScreenHeight();

    if (!imgui_inited) {
        rlImGuiBeginInitImGui();
        rlImGuiEndInitImGui();

        ThemeApplyToImGui(&g_theme, cfg->ui_scale);
        ThemeRebuildImGuiFonts(&g_theme, cfg->ui_scale);

        imgui_inited = true;
        last_ui_scale = cfg->ui_scale;
        last_dpi_scale = dpi_scale;
        last_display_w = display_w;
        last_display_h = display_h;
    }
    else
    {
        const bool scale_changed   = (cfg->ui_scale != last_ui_scale);
        const bool dpi_changed     = (dpi_scale != last_dpi_scale);
        const bool display_changed = (display_w != last_display_w || display_h != last_display_h);

        if (scale_changed || dpi_changed || display_changed)
        {
            last_ui_scale = cfg->ui_scale;
            last_dpi_scale = dpi_scale;
            last_display_w = display_w;
            last_display_h = display_h;

            ThemeApplyToImGui(&g_theme, cfg->ui_scale);

            if (dpi_changed)
            {
                ThemeRebuildImGuiFonts(&g_theme, cfg->ui_scale);
                pending_ui_scale = 0.0f;
            }
            else if (scale_changed)
            {
                // debounce
                pending_ui_scale = cfg->ui_scale;
                ui_scale_idle = 0.0f;
            }
        }

        if (pending_ui_scale > 0.0f)
        {
            ui_scale_idle += ImGui::GetIO().DeltaTime;
            if (ui_scale_idle >= 0.15f)
            {
                ThemeRebuildImGuiFonts(&g_theme, pending_ui_scale);
                pending_ui_scale = 0.0f;
            }
        }
    }

    /* apply persisted layout on first frame, after LayoutInitDefaults() fills the uncovered fields */
    static bool layout_applied = false;
    if (!layout_applied)
    {
        LayoutInitDefaults();
        LayoutApplyPersist(&cfg->ui_layout);
        layout_applied = true;
    }

    /* begin rlImGui frame */
    rlImGuiBegin();

    // handle a completed pull (notify + persist) even when the Data Sources panel is closed
    DataSourcesPollPullCompletion(cfg);

    ImGuiIO &io = ImGui::GetIO();
    if (GetScreenWidth() > 0 && GetScreenHeight() > 0)
        io.DisplaySize = ImVec2((float)GetScreenWidth(), (float)GetScreenHeight());
    io.DisplayFramebufferScale = ImVec2(dpi_scale, dpi_scale);

    UIHandleGlobalEscape(ctx, cfg);

    /* map grid value labels, drawn first so satellite/marker labels win */
    DrawMapGridLabels(ctx, cfg);

    /* scene label overlay: background draw list, above the raylib scene but behind ImGui windows */
    DrawSceneLabels(ctx, cfg);

    /* top navigation bar */
    DrawNavBar(ctx, cfg);

    /* bottom time notch: drawn before the sidebars so they can size to its rendered height */
    DrawBottomBar(ctx, cfg);

    /* sidebar workspace (left actions / right inspector) + transparent center */
    DrawUILayout(ctx, cfg);

    /* settings modal (centered, dimmed/blurred background) */
    DrawSettingsModal(ctx, cfg);

    /* tools modal (manage tool enable/disable + sidebar placement) */
    DrawToolsModal(ctx, cfg);

    /* transit overview modal (large, root-level: must not live in the sidebar) */
    DrawTransitOverviewModal(ctx, cfg);

    /* home-location picking hint banner (the settings modal is closed while picking) */
    if (*ctx->picking_home)
    {
        float vp_x, vp_y, vp_w, vp_h;
        LayoutGetViewportRect(&vp_x, &vp_y, &vp_w, &vp_h);
        ImGui::SetNextWindowPos(ImVec2(vp_x + vp_w * 0.5f,
                                       ImGui::GetFrameHeight() + UIPx(14.0f)),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.9f);
        if (ImGui::Begin("##pick_home_hint", NULL,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav))
        {
            ImGui::TextUnformatted("Click on the map to set your home location    ESC to cancel");
            ImGui::End();
        }
    }

    /* modals */
    DrawDataWarning(ctx, cfg);
    DrawUpdateCheck(ctx, cfg);
    DrawHelpModal(ctx, cfg);
    DrawAboutModal(ctx, cfg);
    DrawExitDialog(ctx, cfg);
    DrawTutorialOverlay(ctx, cfg);

    /* notification toasts: top-right, non-blocking */
    NotifyUpdate(ImGui::GetIO().DeltaTime);
    DrawNotifications();

    /* hints: drawn last into the foreground draw list so they sit over everything */
    DrawHints();

    /* when the UI is hidden (H), show just the simulated time in the lower-left */
    DrawHiddenClockOverlay(ctx, cfg);

    /* end rlImGui frame */
    rlImGuiEnd();
}

/* -- Satellite selection persistence (section 11) --------------------------- */

/** persist active satellites (by NORAD id) into AppConfig (written to settings.json) */
void SaveSatSelection(AppConfig *cfg)
{
    if (!cfg) return;

    cfg->active_sat_count = 0;
    for (int i = 0; i < sat_count && cfg->active_sat_count < GetMaxSatellites(); i++)
    {
        if (satellites[i].is_active)
            cfg->active_sat_ids[cfg->active_sat_count++] = satellites[i].norad_id_num;
    }
    cfg->has_saved_selection = true;

    LOG_INFO("Saved %d active satellites to config", cfg->active_sat_count);
}

/** restore active satellites from AppConfig (loaded from settings.json) */
void LoadSatSelection(AppConfig *cfg)
{
    if (!cfg) return;

    /* no persisted selection yet: keep the default active state from data.json */
    if (!cfg->has_saved_selection)
        return;

    /* apply: only satellites whose id is in the saved set stay active */
    for (int i = 0; i < sat_count; i++)
    {
        bool keep = false;
        for (int k = 0; k < cfg->active_sat_count; k++)
        {
            if (satellites[i].norad_id_num == cfg->active_sat_ids[k])
            {
                keep = true;
                break;
            }
        }
        satellites[i].is_active = keep;
    }

    LOG_INFO("Restored %d active satellites from config", cfg->active_sat_count);
}

bool IsUITyping(void) { return ImGui::GetCurrentContext() ? ImGui::IsAnyItemActive() : false; }
void ToggleTLEWarning(void) { show_tle_warning = !show_tle_warning; }
bool IsMouseOverUI(AppConfig *cfg) { (void)cfg; return ImGui::GetCurrentContext() ? (ImGui::IsWindowHovered(ImGuiFocusedFlags_AnyWindow) || ImGui::IsAnyItemHovered()) : false; }

/** earth occlusion test: true if the camera->target segment passes through the earth sphere */
bool IsOccludedByEarth(Vector3 camPos, Vector3 targetPos, float earthRadius)
{
    Vector3 camToTarget = Vector3Subtract(targetPos, camPos);
    float segLenSq = Vector3DotProduct(camToTarget, camToTarget);
    if (segLenSq <= 0.0f) return false;
    /* parameter of the closest point on the segment to the earth center (origin) */
    float t = -Vector3DotProduct(camPos, camToTarget) / segLenSq;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    Vector3 closest = Vector3Add(camPos, Vector3Scale(camToTarget, t));
    float closestDist = Vector3Length(closest);
    return closestDist < earthRadius;
}

/** true if the earth blocks the view of the a-b segment; endpoints and midpoint are tested */
bool IsOrbitSegmentOccluded(Vector3 camPos, Vector3 a, Vector3 b, float earthRadius)
{
    if (IsOccludedByEarth(camPos, a, earthRadius)) return true;
    if (IsOccludedByEarth(camPos, b, earthRadius)) return true;
    Vector3 mid = Vector3Scale(Vector3Add(a, b), 0.5f);
    return IsOccludedByEarth(camPos, mid, earthRadius);
}
