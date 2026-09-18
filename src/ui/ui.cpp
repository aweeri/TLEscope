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
#include "labels.h"
#include "tools/tools.h"
#include "notifications.h"
#include "core/astro.h"
#include "io/rotator.h"
#include "core/config.h"
#include "core/theme.h"
#include "imgui_theme.h"
#include "data/provider.h"
#include "data/cache.h"
#include "data/storage.h"
#include "data/omm_parser.h"
#include "util/log.h"

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
#include "rlImGui.h"
#include "IconsFontAwesome6.h"

/* -- Helpers --------------------------------------------------------------- */

static ImVec4 ThemeColor(const Color &c)
{
    return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

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

double StepTimeMultiplier(double current, bool increase)
{
    /* Doubling/halving time multiplier with zero-crossing:
     *
     * Forward  (increase=true):  double the speed
     *   ... -4 -> -2 -> -1 -> -0.5 -> 0 -> 0.5 -> 1 -> 2 -> 4 -> ...
     *
     * Backward (increase=false): halve the speed
     *   ... 4 -> 2 -> 1 -> 0.5 -> 0.25 -> 0 -> -0.5 -> -1 -> -2 -> ...
     *
     * When halving 0.5 -> 0.25, snap to 0 instead.
     * Next backward from 0 -> -0.5.
     * Same transition when coming back from negative to positive.
     */
    const double eps = 1e-9;
    const double snap_threshold = 0.25;

    if (increase)
    {
        /* forward: speed up */
        if (fabs(current) < eps)
            return 0.5;               /* 0 -> 0.5 */

        if (current < 0.0)
        {
            /* negative side, moving toward zero: halve magnitude */
            double next = current / 2.0;
            if (fabs(next) <= snap_threshold)  /* -0.5/2=-0.25, snap to 0 */
                return 0.0;
            return next;
        }
        else
        {
            /* positive side: double */
            return current * 2.0;
        }
    }
    else
    {
        /* backward: slow down */
        if (fabs(current) < eps)
            return -0.5;              /* 0 -> -0.5 */

        if (current > 0.0)
        {
            /* positive side, moving toward zero: halve */
            double next = current / 2.0;
            if (next <= snap_threshold)        /* 0.5/2=0.25, snap to 0 */
                return 0.0;
            return next;
        }
        else
        {
            /* negative side: double magnitude in reverse */
            return current * 2.0;
        }
    }
}

double unix_to_epoch(double target_unix)
{
    struct tm *gmt = gmtime((time_t *)&target_unix);
    if (!gmt) return get_current_real_time_epoch();
    int year = gmt->tm_year + 1900;
    double day_of_year = gmt->tm_yday + 1.0;
    double fraction = (gmt->tm_hour + gmt->tm_min / 60.0 + gmt->tm_sec / 3600.0) / 24.0;
    return (year * 1000.0) + day_of_year + fraction;
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

// combined date picker
static bool DrawDatePicker(int *year, int *doy, float width, int *cal_year, int *cal_mon,
                           int first_day)
{
    int mon, mday;
    DoyToMonthDay(*year, *doy, &mon, &mday);

    char date_str[16];
    snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d", *year, mon, mday);

    bool changed = false;
    ImGui::PushID("datebtn");
    bool open = ImGui::Button(date_str, ImVec2(width, 0.0f));
    ImGui::PopID();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick a date");

    if (open)
    {
        *cal_year = *year;
        *cal_mon = mon;
        ImGui::OpenPopup("##datepop");
    }

    if (ImGui::BeginPopup("##datepop"))
    {
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

        /* weekday header + day grid, ordered by the configured first day of
         * the week (0 = Sunday, 1 = Monday). Every column is the same width
         * (all data cells use the same button size) so the header aligns. */
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
                    ImGui::CloseCurrentPopup();
                }
                if (selected)
                    ImGui::PopStyleColor(4);
            }
            ImGui::EndTable();
        }

        ImGui::EndPopup();
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

/* Badge geometry shared by the width helper and the draw routine so the painted
 * chip always matches the reserved width. */
static const float kBadgeDotR = 3.0f;
static const float kBadgeGap  = 5.0f;
static const float kBadgePadX = 6.0f;

/* Fixed badge width (based on the widest state label) so the transport cluster
 * never shifts when the live state changes. */
static float LiveBadgeWidth(void)
{
    return 2.0f * kBadgePadX + 2.0f * kBadgeDotR + kBadgeGap + ImGui::CalcTextSize("PAUSED").x;
}

/**
 * Compact pill badge showing whether the simulation is tracking real time.
 *   LIVE   - accent green, sim runs at 1x and matches the wall clock
 *   PAUSED - muted, time is stopped
 *   FIXED  - muted, a custom/fixed time is active
 */
static void DrawLiveBadge(bool is_live, bool is_paused, float height)
{
    const char *label = is_live ? "LIVE" : (is_paused ? "PAUSED" : "FIXED");

    const float dot_r = kBadgeDotR;
    const float gap   = kBadgeGap;
    float w = LiveBadgeWidth();
    float h = height;

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));   /* reserve layout space and advance the cursor */

    ImDrawList *dl = ImGui::GetWindowDrawList();
    /* modest corner radius keeps the badge reading as a squarish chip rather
     * than a fully-rounded pill */
    float rounding = fminf(h * 0.5f, ImGui::GetStyle().FrameRounding);

    Color pill_bg, pill_border, pill_text, dot;
    if (is_live)
    {
        pill_bg     = ThemeAlpha(g_theme.ui.success, 0.20f);
        pill_border = ThemeAlpha(g_theme.ui.success, 0.70f);
        pill_text   = g_theme.ui.success;
        dot         = g_theme.ui.success;
    }
    else
    {
        pill_bg     = ThemeAlpha(g_theme.ui.surface, 0.65f);
        pill_border = ThemeAlpha(g_theme.ui.border, 0.90f);
        pill_text   = g_theme.ui.text_dim;
        dot         = g_theme.ui.text_dim;
    }

    ImVec2 p1 = ImVec2(p.x + w, p.y + h);
    dl->AddRectFilled(p, p1, ImGui::GetColorU32(ThemeColor(pill_bg)), rounding);
    dl->AddRect(p, p1, ImGui::GetColorU32(ThemeColor(pill_border)), rounding, 0, 1.0f);

    /* status dot (with a soft halo when live), centred with the label */
    ImVec2 text_sz = ImGui::CalcTextSize(label);
    float group_w = 2.0f * dot_r + gap + text_sz.x;
    float group_x = p.x + (w - group_w) * 0.5f;
    ImVec2 dot_c = ImVec2(group_x + dot_r, p.y + h * 0.5f);
    if (is_live)
        dl->AddCircleFilled(dot_c, dot_r + 2.0f, ImGui::GetColorU32(ThemeColor(ThemeAlpha(dot, 0.25f))), 16);
    dl->AddCircleFilled(dot_c, dot_r, ImGui::GetColorU32(ThemeColor(dot)), 16);

    /* label */
    ImVec2 tp = ImVec2(group_x + 2.0f * dot_r + gap, p.y + (h - text_sz.y) * 0.5f);
    dl->AddText(tp, ImGui::GetColorU32(ThemeColor(pill_text)), label);
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
    float row_gap = style.ItemSpacing.y;
    float pad_x   = style.WindowPadding.x;
    float pad_y   = style.WindowPadding.y;

    float time_reserve = ImGui::CalcTextSize("0000-00-00 00:00:00 UTC+0000").x;
    float date_w       = ImGui::CalcTextSize("0000-00-00").x + 4.0f * style.FramePadding.x;
    float time_w       = ImGui::CalcTextSize("00:00:00").x   + 4.0f * style.FramePadding.x;

    char speed_str[32];
    double mult = *ctx->time_multiplier;
    if (fabs(mult) < 1e-9)
        snprintf(speed_str, sizeof(speed_str), "0.0x");
    else
        snprintf(speed_str, sizeof(speed_str), "%.1fx", mult);
    float speed_w = ImGui::CalcTextSize(speed_str).x;

    const char *apply_label = ICON_FA_CHECK " Apply";
    const char *reset_label = ICON_FA_CLOCK " Reset to Now";
    float action_w = fmaxf(ImGui::CalcTextSize(apply_label).x,
                           ImGui::CalcTextSize(reset_label).x) + 2.0f * style.FramePadding.x;

    float badge_w = LiveBadgeWidth();
    float play_w  = 4.0f * btn_sz + 3.0f * spacing;   /* backward | play/pause | forward | now */
    float right_w = btn_sz + spacing + speed_w;       /* chevron + speed text */

    float bottom_need = time_reserve + spacing + badge_w + 2.0f * spacing + play_w + spacing + right_w;

    float top_need = ImGui::CalcTextSize("Date").x + spacing + date_w + spacing +
                     ImGui::CalcTextSize("Time").x + spacing + time_w + 2.0f * spacing +
                     action_w + spacing + action_w;

    float need  = fmaxf(bottom_need, top_need);
    float win_w = need + 2.0f * pad_x;
    if (win_w > screen_w) win_w = screen_w;

    float content_h = row_h + (g_layout.bottom_bar_expanded ? (row_gap + row_h) : 0.0f);
    float win_h = content_h + 2.0f * pad_y;
    float y  = screen_h - win_h;
    float x0 = (screen_w - win_w) * 0.5f;
    if (x0 < 0.0f) x0 = 0.0f;

    ImGui::SetNextWindowPos(ImVec2(x0, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(win_w, win_h));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ThemeColor(g_theme.ui.bg));
    ImGui::PushStyleColor(ImGuiCol_Border,   ThemeColor(g_theme.ui.border));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    /* track whether we need to re-populate the time setter fields from sim time */
    static bool s_needs_populate = true;
    /* HH:MM:SS edit buffer */
    static char s_hms_buf[16] = "00:00:00";
    /* calendar popup navigation state (month currently browsed) */
    static int s_cal_year = 2000;
    static int s_cal_mon = 1;

    if (ImGui::Begin("##bottombar", NULL, flags))
    {
        /* ---- TOP ROW (expanded only): Date [picker]  Time [HH:MM:SS]  |  [Apply] [Reset to Now] ---- */
        if (g_layout.bottom_bar_expanded)
        {
            /* populate input fields from current simulation time when needed */
            if (s_needs_populate)
            {
                epoch_to_local_fields(*ctx->current_epoch,
                                      &g_layout.bb_year, &g_layout.bb_day,
                                      &g_layout.bb_hour, &g_layout.bb_min, &g_layout.bb_sec);
                snprintf(s_hms_buf, sizeof(s_hms_buf), "%02d:%02d:%02d",
                         g_layout.bb_hour, g_layout.bb_min, g_layout.bb_sec);
                s_needs_populate = false;
            }

            ImGui::SetCursorPos(ImVec2(pad_x, pad_y));

            /* date + time inputs (left group) */
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ThemeColor(g_theme.ui.text_dim), "Date");
            ImGui::SameLine(0.0f, spacing);
            if (DrawDatePicker(&g_layout.bb_year, &g_layout.bb_day, date_w,
                               &s_cal_year, &s_cal_mon, cfg->first_day_of_week))
            {
                snprintf(s_hms_buf, sizeof(s_hms_buf), "%02d:%02d:%02d",
                         g_layout.bb_hour, g_layout.bb_min, g_layout.bb_sec);
            }

            ImGui::SameLine(0.0f, spacing);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ThemeColor(g_theme.ui.text_dim), "Time");
            ImGui::SameLine(0.0f, spacing);
            DrawHMSField(&g_layout.bb_hour, &g_layout.bb_min, &g_layout.bb_sec,
                         s_hms_buf, sizeof(s_hms_buf), time_w);

            ImGui::SameLine(0.0f, 2.0f * spacing);
            ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.accent));
            if (ImGui::Button(apply_label, ImVec2(action_w, btn_sz)))
            {
                *ctx->current_epoch = local_fields_to_epoch(
                    g_layout.bb_year, g_layout.bb_day,
                    g_layout.bb_hour, g_layout.bb_min, g_layout.bb_sec);
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Apply the entered date/time to the simulation");

            ImGui::SameLine(0.0f, spacing);
            if (ImGui::Button(reset_label, ImVec2(action_w, btn_sz)))
            {
                epoch_to_local_fields(get_current_real_time_epoch(),
                                      &g_layout.bb_year, &g_layout.bb_day,
                                      &g_layout.bb_hour, &g_layout.bb_min, &g_layout.bb_sec);
                snprintf(s_hms_buf, sizeof(s_hms_buf), "%02d:%02d:%02d",
                         g_layout.bb_hour, g_layout.bb_min, g_layout.bb_sec);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Load current real time into the fields (does not apply)");
        }

        /* ---- BOTTOM ROW: timestamp + live badge + transport (left) | chevron + speed (right) ---- */
        /* simulation time display (uses simulation epoch, not wall clock) */
        char time_str[64];
        epoch_to_datetime_str(*ctx->current_epoch, time_str);

        double real_now  = get_current_real_time_epoch();
        double drift_sec = fabs(*ctx->current_epoch - real_now) * 86400.0;
        bool is_paused = (*ctx->time_multiplier == 0.0);
        bool is_live = (!*ctx->is_auto_warping) && (*ctx->time_multiplier == 1.0) && (drift_sec < 5.0);

        float content_w = win_w - 2.0f * pad_x;
        float bottom_y  = pad_y + (g_layout.bottom_bar_expanded ? (row_h + row_gap) : 0.0f);

        ImGui::SetCursorPos(ImVec2(pad_x, bottom_y));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ThemeColor(g_theme.ui.text_dim), "%s", time_str);

        ImGui::SameLine(0.0f, spacing);
        DrawLiveBadge(is_live, is_paused, row_h);

        /* transport: slow down / reverse, play-pause, accelerate, jump to now */
        ImGui::SameLine(0.0f, 2.0f * spacing);
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
            s_needs_populate = true;   /* refresh the setter fields on next expand */
            NotifyPush(NOTIFY_INFO, ICON_FA_CLOCK, "Time reset to now");
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Jump to now and run live");

        bool is_expanded = g_layout.bottom_bar_expanded;
        float left_end = pad_x + time_reserve + spacing + badge_w + 2.0f * spacing + play_w;
        float right_x = pad_x + content_w - right_w;
        if (right_x < left_end + spacing) right_x = left_end + spacing;
        ImGui::SetCursorPos(ImVec2(right_x, bottom_y));

        /* the single expand/collapse chevron (bottom row only) */
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.text_dim));
        if (ImGui::Button(is_expanded ? ICON_FA_CHEVRON_DOWN "##expand" : ICON_FA_CHEVRON_UP "##expand", ImVec2(btn_sz, btn_sz)))
        {
            g_layout.bottom_bar_expanded = !g_layout.bottom_bar_expanded;
            /* when collapsing, mark for re-population on next expand */
            if (!g_layout.bottom_bar_expanded)
                s_needs_populate = true;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(is_expanded ? "Collapse time controls" : "Expand time controls");

        ImGui::SameLine(0.0f, spacing);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ThemeColor(g_theme.ui.accent), "%s", speed_str);

        /* capture actual notch rect for sidebar layout (ui_layout reads this) */
        ImVec2 bb_pos  = ImGui::GetWindowPos();
        ImVec2 bb_size = ImGui::GetWindowSize();
        g_layout.bottom_bar_x   = bb_pos.x;
        g_layout.bottom_bar_w   = bb_size.x;
        g_layout.bottom_bar_top = bb_pos.y;
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

/* -- Help Modal ------------------------------------------------------------ */

static void DrawHelpModal(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    (void)cfg;
    if (!show_help) return;

    ImGui::OpenPopup("Help");
    if (ImGui::BeginPopupModal("Help", NULL, ImGuiWindowFlags_AlwaysAutoResize))
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
        ImGui::BulletText("R: Rotator Control");
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
            show_help = false;
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
    if (!show_about) return;

    ImGui::OpenPopup("About TLEscope");
    if (ImGui::BeginPopupModal("About TLEscope", NULL, ImGuiWindowFlags_AlwaysAutoResize))
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
            show_about = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

/* -- TLE Warning Dialog ---------------------------------------------------- */

static void DrawDataWarning(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    if (!show_tle_warning) return;

    /* check if any satellite data is older than the configured threshold */
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

    if (!data_is_old)
    {
        show_tle_warning = false;
        return;
    }

    /* surface the staleness as a toast as well (ROADMAP section 8.1) */
    NotifyPush(NOTIFY_WARNING, ICON_FA_TRIANGLE_EXCLAMATION,
               "Orbital data is older than the configured threshold");

    int threshold_secs = cfg->data_stale_threshold_seconds;
    const char *threshold_str = "2 days";
    if (threshold_secs <= STALE_THRESHOLD_6H) threshold_str = "6 hours";
    else if (threshold_secs <= STALE_THRESHOLD_12H) threshold_str = "12 hours";
    else if (threshold_secs <= STALE_THRESHOLD_1D) threshold_str = "1 day";
    else if (threshold_secs <= STALE_THRESHOLD_2D) threshold_str = "2 days";
    else if (threshold_secs <= STALE_THRESHOLD_3D) threshold_str = "3 days";
    else if (threshold_secs <= STALE_THRESHOLD_5D) threshold_str = "5 days";
    else threshold_str = "7 days";

    ImGui::OpenPopup("Orbital Data Warning");
    if (ImGui::BeginPopupModal("Orbital Data Warning", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Your orbital data is older than %s.", threshold_str);
        ImGui::Text("Would you like to update it now?");

        if (ImGui::Button("Update", ImVec2(120, 0)))
        {
            show_tle_warning = false;
            LayoutOpenPanel(PANEL_DATA_SOURCES);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Update", ImVec2(120, 0)))
        {
            show_tle_warning = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

/* -- First Run Dialog ------------------------------------------------------ */

static void DrawFirstRunDialog(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    if (!cfg->show_first_run_dialog) return;

    ImGui::OpenPopup("Welcome to TLEscope");
    /* give the modal a minimum content width so the button pair has
     * symmetric breathing room instead of hugging the window edge */
    ImGui::SetNextWindowContentSize(ImVec2(360, 0));
    if (ImGui::BeginPopupModal("Welcome to TLEscope", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        /* centered heading */
        const char *title = ICON_FA_SATELLITE "  Welcome to TLEscope";
        float title_w = ImGui::CalcTextSize(title).x;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - title_w) * 0.5f);
        ImGui::TextColored(ThemeColor(g_theme.ui.accent), "%s", title);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextWrapped("Pick a graphics profile to get started. You can change these settings later from the Settings menu.");
        ImGui::Spacing();

        /* two equally-sized, aligned buttons */
        float btn_w = 150.0f;
        float btn_h = 60.0f;
        float avail = ImGui::GetContentRegionAvail().x;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float total = btn_w * 2.0f + spacing;
        ImGui::SetCursorPosX((avail - total) * 0.5f);

        if (ImGui::Button("Performance", ImVec2(btn_w, btn_h)))
        {
            cfg->show_clouds = false;
            cfg->show_night_lights = false;
            cfg->show_scattering = false;
            cfg->show_skybox = false;
            cfg->night_mode = false;
            cfg->show_first_run_dialog = false;
            LayoutFillPersist(&cfg->ui_layout);
            SaveAppConfig("settings.json", cfg);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Aesthetic", ImVec2(btn_w, btn_h)))
        {
            cfg->show_clouds = true;
            cfg->show_night_lights = true;
            cfg->show_scattering = true;
            cfg->show_skybox = true;
            cfg->night_mode = false;
            cfg->show_first_run_dialog = false;
            LayoutFillPersist(&cfg->ui_layout);
            SaveAppConfig("settings.json", cfg);
            ImGui::CloseCurrentPopup();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextWrapped("Performance disables clouds, night lights, atmospheric scattering and the skybox. Aesthetic enables all of them.");
        ImGui::EndPopup();
    }
}

/* -- Update Check Notification ---------------------------------------------- */

static void DrawUpdateCheck(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    (void)cfg;
    if (!g_ui.update_available) return;

    static double popup_start = 0.0;
    if (popup_start == 0.0) popup_start = GetTime();

    if (GetTime() - popup_start < 10.0)
    {
        ImGui::OpenPopup("Update Available");
        if (ImGui::BeginPopupModal("Update Available", NULL, ImGuiWindowFlags_AlwaysAutoResize))
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
                g_ui.update_available = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
}

/* -- Exit Dialog ----------------------------------------------------------- */

static void DrawExitDialog(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    if (!show_exit_dialog) return;

    ImGui::OpenPopup("Exit?");
    if (ImGui::BeginPopupModal("Exit?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Are you sure you want to exit?");
        ImGui::Separator();

        if (ImGui::Button("Yes", ImVec2(120, 0)))
        {
            *ctx->exit_app = true;
            show_exit_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(120, 0)))
        {
            show_exit_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

/* -- Main DrawGUI ---------------------------------------------------------- */

void DrawGUI(UIContext *ctx, AppConfig *cfg, Font customFont)
{
    /* one-time ImGui initialization.
     * Theme colors/style/fonts are applied here and again by main.cpp
     * whenever cfg->reload_theme triggers a theme switch. */
    static bool imgui_inited = false;
    static float last_ui_scale = 0.0f;
    static float last_dpi_scale = 0.0f;
    const float dpi_scale = ThemeDevicePixelScale();
    if (!imgui_inited) {
        rlImGuiBeginInitImGui();
        rlImGuiEndInitImGui();

        ThemeApplyToImGui(&g_theme, cfg->ui_scale);
        ThemeRebuildImGuiFonts(&g_theme, cfg->ui_scale);

        imgui_inited = true;
        last_ui_scale = cfg->ui_scale;
        last_dpi_scale = dpi_scale;
    }
    else if (cfg->ui_scale != last_ui_scale || dpi_scale != last_dpi_scale)
    {
        last_ui_scale = cfg->ui_scale;
        last_dpi_scale = dpi_scale;
        /* the UI scale is baked into the font atlas, so a scale change
         * requires rebuilding the fonts (not just re-applying the style) */
        ThemeRebuildImGuiFonts(&g_theme, cfg->ui_scale);
        ThemeApplyToImGui(&g_theme, cfg->ui_scale);
    }

    /* apply persisted layout on first frame.
     * Init defaults first so fields not covered by the persist struct
     * (e.g. show_bottom_bar) have sane values, then layer the saved
     * arrangement on top. */
    static bool layout_applied = false;
    if (!layout_applied)
    {
        LayoutInitDefaults();
        LayoutApplyPersist(&cfg->ui_layout);
        layout_applied = true;
    }

    /* begin rlImGui frame */
    rlImGuiBegin();

    ImGui::GetIO().DisplayFramebufferScale = ImVec2(dpi_scale, dpi_scale);

    /* map grid value labels, drawn first so satellite/marker labels win */
    DrawMapGridLabels(ctx, cfg);

    /* scene label overlay: drawn into the background draw list so labels
     * render on top of the raylib scene but behind all ImGui windows */
    DrawSceneLabels(ctx, cfg);

    /* top navigation bar */
    DrawNavBar(ctx, cfg);

    /* bottom center time notch — drawn BEFORE the sidebars so they can
     * size themselves to its actual rendered height (g_layout.bottom_bar_top) */
    DrawBottomBar(ctx, cfg);

    /* sidebar workspace (left actions / right inspector) + transparent center */
    DrawUILayout(ctx, cfg);

    /* settings modal (centered, dimmed/blurred background) */
    DrawSettingsModal(ctx, cfg);

    /* tools modal (manage tool enable/disable + sidebar placement) */
    DrawToolsModal(ctx, cfg);

    /* home-location picking hint: the settings modal is closed while picking,
     * so show a small banner telling the user how to set / cancel the pick */
    if (*ctx->picking_home)
    {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                                       ImGui::GetFrameHeight() + 14.0f),
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
    DrawFirstRunDialog(ctx, cfg);
    DrawDataWarning(ctx, cfg);
    DrawUpdateCheck(ctx, cfg);
    DrawHelpModal(ctx, cfg);
    DrawAboutModal(ctx, cfg);
    DrawExitDialog(ctx, cfg);

    /* notification toasts (ROADMAP section 8.1) - drawn last so they stack
     * on top of everything, top-right, without blocking interaction */
    NotifyUpdate(ImGui::GetIO().DeltaTime);
    DrawNotifications();

    /* end rlImGui frame */
    rlImGuiEnd();
}

/* -- Satellite selection persistence (section 11) --------------------------- */

/** persist which satellites are active (by NORAD id) into the AppConfig.
 *  The list is written to settings.json by SaveAppConfig(), so it always
 *  lives in the app's directory (no separate file / CWD mismatch). */
void SaveSatSelection(AppConfig *cfg)
{
    if (!cfg) return;

    cfg->active_sat_count = 0;
    for (int i = 0; i < sat_count && cfg->active_sat_count < MAX_SATELLITES; i++)
    {
        if (satellites[i].is_active)
            cfg->active_sat_ids[cfg->active_sat_count++] = satellites[i].norad_id_num;
    }
    cfg->has_saved_selection = true;

    LOG_INFO("Saved %d active satellites to config", cfg->active_sat_count);
}

/** restore which satellites are active from the AppConfig (loaded from
 *  settings.json by LoadAppConfig()). */
void LoadSatSelection(AppConfig *cfg)
{
    if (!cfg) return;

    /* If no selection has ever been persisted (first run / no settings key),
     * keep the default active state from data.json instead of blanking all. */
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

/** earth occlusion test: true if the line segment from the camera to the target
 * passes through the earth sphere (i.e. the globe blocks the view of the target) */
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
