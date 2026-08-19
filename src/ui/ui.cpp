/*
 * ui.cpp - Dear ImGui UI implementation
 *
 * This file implements the UI layer using Dear ImGui + rlImGui.
 * It contains the modal dialogs (first-run, exit, data warning, update
 * check, help, about) and the main DrawGUI entry point. The sidebar /
 * panel workspace lives in ui_layout.cpp / panels.cpp.
 */

#include "ui.h"
#include "ui_layout.h"
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
    .ra_format = 1,   /* default: hours:minutes:seconds */
    .dec_format = 1,  /* default: degrees:arcminutes:arcseconds */
};

/* -- Helpers --------------------------------------------------------------- */

Color ApplyAlpha(Color c, float alpha)
{
    c.a = (unsigned char)(alpha * 255.0f);
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

/* -- Bottom Center Time Bar ------------------------------------------------ */

static void DrawBottomBar(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    if (!LayoutBottomBarVisible()) return;

    /* use ImGui's display size (consistent with sidebar layout in ui_layout.cpp) */
    float screen_w = ImGui::GetIO().DisplaySize.x;
    float screen_h = ImGui::GetIO().DisplaySize.y;
    float btn_sz = 24.0f;
    float spacing = 4.0f;
    float time_text_w = 220.0f;
    float speed_text_w = 56.0f;

    /* collapsed bar: time + 5 buttons + expand arrow + speed label */
    int collapsed_btns = 6; /* backward, play/pause, forward, reset, expand, (speed label inline) */
    float collapsed_w = time_text_w + collapsed_btns * (btn_sz + spacing) + speed_text_w + spacing * 2;

    /* expanded panel slides UP from behind the collapsed bar */
    float expanded_h = 112.0f;  /* height for the time setter area */
    float bar_h = btn_sz + 12.0f;
    float total_h = bar_h + (g_layout.bottom_bar_expanded ? expanded_h : 0.0f);
    float y = screen_h - total_h;

    /* center the window, but ensure it doesn't clip on small screens */
    float win_w = fmaxf(collapsed_w, 560.0f);
    float x0 = (screen_w - win_w) * 0.5f;
    if (x0 < 0.0f) x0 = 0.0f;

    ImGui::SetNextWindowPos(ImVec2(x0, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(win_w, total_h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.08f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border,    ImVec4(0.20f, 0.20f, 0.25f, 0.70f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
    /* sleeker frames: rounded corners + compact padding instead of big clunky buttons */
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    /* track whether we need to re-populate the time setter fields from sim time */
    static bool s_needs_populate = true;

    if (ImGui::Begin("##bottombar", NULL, flags))
    {
        /* ---- Expanded panel (time setter) — slides up from behind the collapsed bar ---- */
        if (g_layout.bottom_bar_expanded)
        {
            /* populate input fields from current simulation time when needed */
            if (s_needs_populate)
            {
                double epoch = *ctx->current_epoch;
                double unix_sec = get_unix_from_epoch(epoch);
                time_t t = (time_t)unix_sec;
                struct tm *gmt = gmtime(&t);
                if (gmt)
                {
                    g_layout.bb_year  = gmt->tm_year + 1900;
                    g_layout.bb_day   = gmt->tm_yday + 1;
                    g_layout.bb_hour  = gmt->tm_hour;
                    g_layout.bb_min   = gmt->tm_min;
                    g_layout.bb_sec   = gmt->tm_sec;
                }
                s_needs_populate = false;
            }
            float avail = ImGui::GetContentRegionAvail().x;

            /* ---- Time setter: labeled inputs in a single aligned row ---- */
            auto DrawTimeField = [&](const char *label, int *value, int min_v, int max_v,
                                     float width)
            {
                ImGui::BeginGroup();
                ImGui::PushID(label);

                /* label centered above the field */
                float label_w = ImGui::CalcTextSize(label).x;
                float arrow_w = ImGui::GetFrameHeight();
                float group_w = width + 2.0f * arrow_w + 2.0f;  /* up + input + down */
                float label_x = (group_w - label_w) * 0.5f;
                if (label_x > 0.0f) ImGui::Dummy(ImVec2(label_x, 0.0f));
                ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary), "%s", label);
                if (label_x > 0.0f) ImGui::SameLine(0.0f, 0.0f);
                ImGui::Dummy(ImVec2(group_w - label_x - label_w, 0.0f));

                /* up arrow */
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 1.0f));
                if (ImGui::ArrowButton("##up", ImGuiDir_Up))
                {
                    (*value)++;
                    if (*value > max_v) *value = min_v;
                }
                ImGui::PopStyleVar();
                ImGui::SameLine(0.0f, 1.0f);

                /* value input */
                ImGui::SetNextItemWidth(width);
                if (ImGui::InputInt("##field", value, 0, 0))
                {
                    if (*value < min_v) *value = min_v;
                    if (*value > max_v) *value = max_v;
                }

                /* down arrow */
                ImGui::SameLine(0.0f, 1.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 1.0f));
                if (ImGui::ArrowButton("##down", ImGuiDir_Down))
                {
                    (*value)--;
                    if (*value < min_v) *value = max_v;
                }
                ImGui::PopStyleVar();

                ImGui::PopID();
                ImGui::EndGroup();
            };

            float field_w = fminf(52.0f, (avail - 280.0f) / 5.0f);
            if (field_w < 36.0f) field_w = 36.0f;

            DrawTimeField("Year", &g_layout.bb_year, 1900, 3000, field_w);
            ImGui::SameLine(0.0f, spacing);
            DrawTimeField("Day", &g_layout.bb_day, 1, 366, field_w);
            ImGui::SameLine(0.0f, spacing);
            DrawTimeField("Hour", &g_layout.bb_hour, 0, 23, field_w);
            ImGui::SameLine(0.0f, spacing);
            DrawTimeField("Min", &g_layout.bb_min, 0, 59, field_w);
            ImGui::SameLine(0.0f, spacing);
            DrawTimeField("Sec", &g_layout.bb_sec, 0, 59, field_w);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            /* ---- Action row: Apply / Reset to Now / Collapse ---- */
            float action_w = (btn_sz + spacing) * 3.0f;
            float action_x = avail - action_w;
            if (action_x < 0.0f) action_x = 0.0f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + action_x);

            /* Apply time button */
            ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.plot_histogram));
            if (ImGui::Button(ICON_FA_CHECK "##settime", ImVec2(btn_sz, btn_sz)))
            {
                double day_fraction = (g_layout.bb_hour +
                                       g_layout.bb_min / 60.0 +
                                       g_layout.bb_sec / 3600.0) / 24.0;
                *ctx->current_epoch = g_layout.bb_year * 1000.0 +
                                      g_layout.bb_day + day_fraction;
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Apply set time");

            ImGui::SameLine(0.0f, spacing);

            /* Reset to Now button */
            ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.plot_histogram));
            if (ImGui::Button(ICON_FA_CLOCK "##resetnow", ImVec2(btn_sz, btn_sz)))
            {
                *ctx->current_epoch = get_current_real_time_epoch();
                /* repopulate fields from current time */
                double epoch = *ctx->current_epoch;
                double unix_sec = get_unix_from_epoch(epoch);
                time_t t = (time_t)unix_sec;
                struct tm *gmt = gmtime(&t);
                if (gmt)
                {
                    g_layout.bb_year  = gmt->tm_year + 1900;
                    g_layout.bb_day   = gmt->tm_yday + 1;
                    g_layout.bb_hour  = gmt->tm_hour;
                    g_layout.bb_min   = gmt->tm_min;
                    g_layout.bb_sec   = gmt->tm_sec;
                }
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset to current real time");

            ImGui::SameLine(0.0f, spacing);

            /* collapse button INSIDE the expanded panel so the setter can always be exited */
            ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.text_secondary));
            if (ImGui::Button(ICON_FA_CHEVRON_DOWN "##collapse", ImVec2(btn_sz, btn_sz)))
            {
                g_layout.bottom_bar_expanded = false;
                s_needs_populate = true;
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Collapse time controls");

            ImGui::Separator();
        }

        /* ---- Collapsed bar row (always visible) ---- */
        /* simulation time display (uses simulation epoch, not wall clock) */
        char time_str[64];
        epoch_to_datetime_str(*ctx->current_epoch, time_str);

        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary), "%s", time_str);
        ImGui::SameLine(0.0f, spacing * 2);

        /* slow down / reverse */
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.text_secondary));
        if (ImGui::Button(ICON_FA_BACKWARD "##backward", ImVec2(btn_sz, btn_sz)))
        {
            *ctx->time_multiplier = StepTimeMultiplier(*ctx->time_multiplier, false);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Slow down / reverse time");
        ImGui::SameLine(0.0f, spacing);

        /* play/pause */
        bool is_paused = (*ctx->time_multiplier == 0.0);
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.ui_accent));
        if (ImGui::Button(is_paused ? (ICON_FA_PLAY "##playpause") : (ICON_FA_PAUSE "##playpause"), ImVec2(btn_sz, btn_sz)))
        {
            if (is_paused)
                *ctx->time_multiplier = (*ctx->saved_multiplier != 0.0) ? *ctx->saved_multiplier : 1.0;
            else
                { *ctx->saved_multiplier = *ctx->time_multiplier; *ctx->time_multiplier = 0.0; }
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(is_paused ? "Resume" : "Pause");
        ImGui::SameLine(0.0f, spacing);

        /* accelerate */
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.text_secondary));
        if (ImGui::Button(ICON_FA_FORWARD "##forward", ImVec2(btn_sz, btn_sz)))
        {
            *ctx->time_multiplier = StepTimeMultiplier(*ctx->time_multiplier, true);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Speed up time");
        ImGui::SameLine(0.0f, spacing);

        /* reset to now */
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.plot_histogram));
        if (ImGui::Button(ICON_FA_ARROW_ROTATE_LEFT "##reset", ImVec2(btn_sz, btn_sz)))
        {
            *ctx->current_epoch = get_current_real_time_epoch();
            *ctx->time_multiplier = 1.0;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset to current time");
        ImGui::SameLine(0.0f, spacing);

        /* expand/collapse arrow — clicking makes the time setter slide UP from behind */
        bool is_expanded = g_layout.bottom_bar_expanded;
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.text_secondary));
        if (ImGui::Button(is_expanded ? ICON_FA_CHEVRON_DOWN "##expand2" : ICON_FA_CHEVRON_UP "##expand", ImVec2(btn_sz, btn_sz)))
        {
            g_layout.bottom_bar_expanded = !g_layout.bottom_bar_expanded;
            /* when collapsing, mark for re-population on next expand */
            if (!g_layout.bottom_bar_expanded)
                s_needs_populate = true;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(is_expanded ? "Collapse time controls" : "Expand time controls");

        /* speed indicator */
        ImGui::SameLine(0.0f, spacing * 2);
        char speed_str[32];
        double mult = *ctx->time_multiplier;
        if (fabs(mult) < 1e-9)
            snprintf(speed_str, sizeof(speed_str), "0.0x");
        else
            snprintf(speed_str, sizeof(speed_str), "%.1fx", mult);
        ImGui::TextColored(ThemeColor(g_theme.ui.ui_accent), "%s", speed_str);

        /* capture actual notch rect for sidebar layout (ui_layout reads this) */
        ImVec2 bb_pos  = ImGui::GetWindowPos();
        ImVec2 bb_size = ImGui::GetWindowSize();
        g_layout.bottom_bar_x   = bb_pos.x;
        g_layout.bottom_bar_w   = bb_size.x;
        g_layout.bottom_bar_top = bb_pos.y;
    }
    ImGui::End();
    ImGui::PopStyleVar(5);
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
        ImGui::BulletText("1: Satellite Manager");
        ImGui::BulletText("2: Data Sources");
        ImGui::BulletText("4: Scope");
        ImGui::BulletText("5: Satellite Passes");
        ImGui::BulletText("6: Polar Plot");
        ImGui::BulletText("7: Doppler Analysis");
        ImGui::BulletText("8: Rotator Control");
        ImGui::BulletText("9: Log");
        ImGui::BulletText("0: Satellite Info");
        ImGui::BulletText("R: Rotator Control");
        ImGui::BulletText("M: Toggle 2D/3D");
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
        ImGui::TextColored(ThemeColor(g_theme.ui.ui_accent), "%s", title);

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
    if (!imgui_inited) {
        rlImGuiBeginInitImGui();
        rlImGuiEndInitImGui();

        ThemeApplyToImGui(&g_theme, cfg->ui_scale);
        ThemeRebuildImGuiFonts(&g_theme, cfg->ui_scale);

        imgui_inited = true;
        last_ui_scale = cfg->ui_scale;
    }
    else if (cfg->ui_scale != last_ui_scale)
    {
        last_ui_scale = cfg->ui_scale;
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

    /* top navigation bar */
    DrawNavBar(ctx, cfg);

    /* bottom center time notch — drawn BEFORE the sidebars so they can
     * size themselves to its actual rendered height (g_layout.bottom_bar_top) */
    DrawBottomBar(ctx, cfg);

    /* sidebar workspace (left actions / right inspector) + transparent center */
    DrawUILayout(ctx, cfg);

    /* settings modal (centered, dimmed/blurred background) */
    DrawSettingsModal(ctx, cfg);

    /* home-location picking hint: the settings modal is closed while picking,
     * so show a small banner telling the user how to set / cancel the pick */
    if (*ctx->picking_home)
    {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                                       ImGui::GetFrameHeight() + 14.0f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.9f);
        ImGui::Begin("##pick_home_hint", NULL,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav);
        ImGui::TextUnformatted("Click on the map to set your home location    ESC to cancel");
        ImGui::End();
    }

    /* modals */
    DrawFirstRunDialog(ctx, cfg);
    DrawDataWarning(ctx, cfg);
    DrawUpdateCheck(ctx, cfg);
    DrawHelpModal(ctx, cfg);
    DrawAboutModal(ctx, cfg);
    DrawExitDialog(ctx, cfg);

    /* end rlImGui frame */
    rlImGuiEnd();
}

/* -- Required stubs (to match ui.h declarations) ---------------------------- */

void SaveSatSelection(void) {}
void LoadSatSelection(void) {}
bool IsUITyping(void) { return ImGui::GetCurrentContext() ? ImGui::IsAnyItemActive() : false; }
void ToggleTLEWarning(void) { show_tle_warning = !show_tle_warning; }
bool IsMouseOverUI(AppConfig *cfg) { (void)cfg; return ImGui::GetCurrentContext() ? (ImGui::IsWindowHovered(ImGuiFocusedFlags_AnyWindow) || ImGui::IsAnyItemHovered()) : false; }

/** simple earth occlusion test using dot product */
bool IsOccludedByEarth(Vector3 camPos, Vector3 targetPos, float earthRadius)
{
    Vector3 camToTarget = Vector3Subtract(targetPos, camPos);
    float dist = Vector3Length(camToTarget);
    if (dist <= 0.0f) return false;
    float camDist = Vector3Length(camPos);
    if (camDist <= 0.0f) return false;
    float angle = acosf(Vector3DotProduct(camPos, camToTarget) / (camDist * dist));
    float horizonAngle = asinf(earthRadius / camDist);
    return angle < horizonAngle;
}