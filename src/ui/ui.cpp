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

UIState g_ui = {0};

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
    /* time multiplier steps: 0, 0.1, 0.5, 1, 2, 5, 10, 30, 60, 300, 600, 3600 */
    const double steps[] = {0.0, 0.1, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0, 300.0, 600.0, 3600.0};
    int n = sizeof(steps) / sizeof(steps[0]);

    if (increase)
    {
        for (int i = 0; i < n - 1; i++)
        {
            if (current >= steps[i] && current < steps[i + 1] - 0.001)
                return steps[i + 1];
        }
        return steps[n - 1];
    }
    else
    {
        for (int i = n - 1; i > 0; i--)
        {
            if (current <= steps[i] && current > steps[i + 1] + 0.001)
                return steps[i - 1];
            if (fabs(current - steps[i]) < 0.001)
                return steps[i - 1];
        }
        return steps[0];
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

/* -- Bottom Center Time Notch ---------------------------------------------- */

static void DrawBottomBar(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    if (!LayoutBottomBarVisible()) return;

    float screen_w = (float)GetScreenWidth();
    float screen_h = (float)GetScreenHeight();
    float btn_sz = 30.0f;
    float spacing = 4.0f;
    float time_text_w = 200.0f;
    float total_w = time_text_w + 5 * (btn_sz + spacing) + spacing;
    float x0 = (screen_w - total_w) * 0.5f;
    float bar_h = btn_sz + 10.0f;
    float y = screen_h - bar_h - 12.0f;

    ImGui::SetNextWindowPos(ImVec2(x0, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(total_w, bar_h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.08f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Border,    ImVec4(0.20f, 0.20f, 0.25f, 0.70f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));

    if (ImGui::Begin("##bottombar", NULL,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings))
    {
        /* simulation time in UTC */
        time_t now_raw = time(NULL);
        struct tm *gmt = gmtime(&now_raw);
        char time_str[64];
        if (gmt)
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S UTC", gmt);
        else
            snprintf(time_str, sizeof(time_str), "---");

        ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary), "%s", time_str);
        ImGui::SameLine(0.0f, spacing);

        /* slow down / reverse */
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.text_secondary));
        if (ImGui::Button(ICON_FA_BACKWARD, ImVec2(btn_sz, btn_sz)))
        {
            *ctx->time_multiplier = StepTimeMultiplier(*ctx->time_multiplier, false);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Slow down / reverse time");
        ImGui::SameLine(0.0f, spacing);

        /* play/pause */
        bool is_paused = (*ctx->time_multiplier == 0.0);
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.ui_accent));
        if (ImGui::Button(is_paused ? ICON_FA_PLAY : ICON_FA_PAUSE, ImVec2(btn_sz, btn_sz)))
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
        if (ImGui::Button(ICON_FA_FORWARD, ImVec2(btn_sz, btn_sz)))
        {
            *ctx->time_multiplier = StepTimeMultiplier(*ctx->time_multiplier, true);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Speed up time");
        ImGui::SameLine(0.0f, spacing);

        /* reset to now */
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.plot_histogram));
        if (ImGui::Button(ICON_FA_ARROW_ROTATE_LEFT, ImVec2(btn_sz, btn_sz)))
        {
            *ctx->current_epoch = get_current_real_time_epoch();
            *ctx->time_multiplier = 1.0;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset to current time");
        ImGui::SameLine(0.0f, spacing);

        /* open time control panel */
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.text_secondary));
        if (ImGui::Button(ICON_FA_STOPWATCH, ImVec2(btn_sz, btn_sz)))
        {
            LayoutTogglePanel(PANEL_TIME_CTRL);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open time control panel");
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
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
        ImGui::BulletText("3: Time Control");
        ImGui::BulletText("4: Scope");
        ImGui::BulletText("5: Satellite Passes");
        ImGui::BulletText("6: Polar Plot");
        ImGui::BulletText("7: Doppler Analysis");
        ImGui::BulletText("8: Rotator Control");
        ImGui::BulletText("9: Log");
        ImGui::BulletText("0: Satellite Info");
        ImGui::BulletText("R: Rotator Control");
        ImGui::BulletText("Grave: Time Control");
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
    if (ImGui::BeginPopupModal("Welcome to TLEscope", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Please select a graphics profile for your first run:");

        if (ImGui::Button("Performance", ImVec2(150, 60)))
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
        if (ImGui::Button("Aesthetic", ImVec2(150, 60)))
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

        ImGui::Text("Settings can be tweaked later in the settings menu.");
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

    /* sidebar workspace (left actions / right inspector) + transparent center */
    DrawUILayout(ctx, cfg);

    /* bottom center time notch */
    DrawBottomBar(ctx, cfg);

    /* settings modal (centered, dimmed/blurred background) */
    DrawSettingsModal(ctx, cfg);

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