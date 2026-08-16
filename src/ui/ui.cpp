 /*
 * ui.cpp - Dear ImGui UI implementation
 *
 * This file implements the UI layer using Dear ImGui + rlImGui.
 */

#include "ui.h"
#include "core/astro.h"
#include "io/rotator.h"
#include "core/config.h"
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
#include "FA6FreeSolidFontData.h"

/* -- UIState instance ------------------------------------------------------ */

static UIState g_ui = {0};

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

/* -- UI State (previously static vars in ui.c) ------------------------------ */

static bool show_help = false;
static bool show_settings = false;
static bool show_passes_dialog = false;
static bool show_polar_dialog = false;
static bool show_doppler_dialog = false;
static bool show_tle_warning = false;
static bool show_exit_dialog = false;
static bool show_sat_mgr_dialog = false;
static bool show_tle_mgr_dialog = false;
static bool show_time_dialog = false;
static bool show_scope_dialog = false;
static bool show_sat_info_dialog = false;

static bool rot_show_window = false;
static bool show_log_window = false;

// celestrak selection state (shared across data sources dialog)
static bool celestrak_sel[25] = {false};

/** case-insensitive substring search; returns true if substr is found in str */
static bool str_contains_ic(const char *str, const char *substr)
{
    if (!str || !substr) return false;
    if (*substr == '\0') return true;
    while (*str)
    {
        const char *a = str;
        const char *b = substr;
        while (*a && *b && (tolower((unsigned char)*a) == tolower((unsigned char)*b)))
        { a++; b++; }
        if (*b == '\0') return true;
        str++;
    }
    return false;
}
static bool log_auto_scroll = true;

/* -- Top Icon Bar ---------------------------------------------------------- */

static void DrawTopBar(UIContext *ctx, AppConfig *cfg)
{
    // floating window at top center - no title bar, no background, no scroll
    float screen_w = (float)GetScreenWidth();
    float btn_size = 32.0f;
    float spacing = 4.0f;
    int num_btns = 10;
    float total_w = num_btns * (btn_size + spacing) + spacing;
    float x0 = (screen_w - total_w) * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(x0, 4.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(total_w, btn_size + 8.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    if (ImGui::Begin("##topbar", NULL,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings))
    {
        struct { bool *open; const char *icon; const char *tooltip; } btns[] = {
            {&show_sat_mgr_dialog,  ICON_FA_SATELLITE,      "Satellite Manager"},
            {&show_tle_mgr_dialog,  ICON_FA_DATABASE,       "Data Sources"},
            {&show_passes_dialog,   ICON_FA_ROUTE,          "Satellite Passes"},
            {&show_polar_dialog,    ICON_FA_COMPASS,        "Polar Plot"},
            {&show_scope_dialog,    ICON_FA_CROSSHAIRS,     "Scope"},
            {&rot_show_window,      ICON_FA_TURN_UP,        "Rotator Control"},
            {&show_time_dialog,     ICON_FA_CLOCK,          "Time Control"},
            {&show_settings,        ICON_FA_GEAR,           "Settings"},
            {&show_log_window,      ICON_FA_LIST,           "Log"},
            {&show_help,            ICON_FA_CIRCLE_QUESTION, "Help"},
        };

        for (int i = 0; i < 10; i++)
        {
            bool is_open = *btns[i].open;
            // highlight if open: use accent color, otherwise dim
            ImVec4 btn_color = is_open ? ImVec4(0.4f, 0.7f, 1.0f, 0.9f)
                                       : ImVec4(0.7f, 0.7f, 0.7f, 0.7f);
            ImGui::PushStyleColor(ImGuiCol_Text, btn_color);

            ImGui::PushID(i);
            if (ImGui::Button(btns[i].icon, ImVec2(btn_size, btn_size)))
            {
                *btns[i].open = !*btns[i].open;
            }
            ImGui::PopID();

            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", btns[i].tooltip);

            if (i < 9)
                ImGui::SameLine(0.0f, spacing);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

/* -- Bottom Time Bar ------------------------------------------------------- */

static void DrawBottomBar(UIContext *ctx, AppConfig *cfg)
{
    float screen_w = (float)GetScreenWidth();
    float btn_sz = 30.0f;
    float spacing = 4.0f;
    // 6 elements: time text + 5 buttons
    float time_text_w = 200.0f;
    float total_w = time_text_w + 5 * (btn_sz + spacing) + spacing;
    float x0 = (screen_w - total_w) * 0.5f;

    float screen_h = (float)GetScreenHeight();
    float bar_h = btn_sz + 8.0f;

    ImGui::SetNextWindowPos(ImVec2(x0, screen_h - bar_h - 4.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(total_w, bar_h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    if (ImGui::Begin("##bottombar", NULL,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings))
    {
        // simulation time in UTC
        time_t now_raw = time(NULL);
        struct tm *gmt = gmtime(&now_raw);
        char time_str[64];
        if (gmt)
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S UTC", gmt);
        else
            snprintf(time_str, sizeof(time_str), "---");

        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 0.9f), "%s", time_str);
        ImGui::SameLine(0.0f, spacing);

        // slow down / reverse
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 0.8f));
        if (ImGui::Button(ICON_FA_BACKWARD, ImVec2(btn_sz, btn_sz)))
        {
            *ctx->time_multiplier = StepTimeMultiplier(*ctx->time_multiplier, false);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Slow down / reverse time");
        ImGui::SameLine(0.0f, spacing);

        // play/pause
        bool is_paused = (*ctx->time_multiplier == 0.0);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 0.9f));
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

        // accelerate
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 0.8f));
        if (ImGui::Button(ICON_FA_FORWARD, ImVec2(btn_sz, btn_sz)))
        {
            *ctx->time_multiplier = StepTimeMultiplier(*ctx->time_multiplier, true);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Speed up time");
        ImGui::SameLine(0.0f, spacing);

        // reset to now
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.3f, 0.9f));
        if (ImGui::Button(ICON_FA_ARROW_ROTATE_LEFT, ImVec2(btn_sz, btn_sz)))
        {
            *ctx->current_epoch = get_current_real_time_epoch();
            *ctx->time_multiplier = 1.0;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset to current time");
        ImGui::SameLine(0.0f, spacing);

        // time setter window
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 0.8f));
        if (ImGui::Button(ICON_FA_STOPWATCH, ImVec2(btn_sz, btn_sz)))
        {
            show_time_dialog = !show_time_dialog;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open time control window");
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

/* -- Satellite Info Panel -------------------------------------------------- */

static void DrawSatelliteInfo(UIContext *ctx, AppConfig *cfg)
{
    if (!*ctx->selected_sat) return;

    Satellite *sat = *ctx->selected_sat;
    char title[128];
    snprintf(title, sizeof(title), "Satellite: %s", sat->name);

    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(title, &show_sat_info_dialog))
    {
        ImGui::Text("NORAD: %s", sat->norad_id);
        ImGui::Text("Active: %s", sat->is_active ? "Yes" : "No");

        ImGui::Separator();
        ImGui::Text("Position:");
        ImGui::Text("  X: %.2f km", sat->current_pos.x);
        ImGui::Text("  Y: %.2f km", sat->current_pos.y);
        ImGui::Text("  Z: %.2f km", sat->current_pos.z);

        // velocity display needs a velocity field in the Satellite struct (not available yet)

        ImGui::Separator();
        ImGui::Text("Orbital Elements:");
        ImGui::Text("  Inclination: %.4f deg", sat->inclination);
        ImGui::Text("  RAAN: %.4f deg", sat->raan);
        ImGui::Text("  Eccentricity: %.6f", sat->eccentricity);
        ImGui::Text("  Arg of Perigee: %.4f deg", sat->arg_perigee);
        ImGui::Text("  Mean Anomaly: %.4f deg", sat->mean_anomaly);
        ImGui::Text("  Mean Motion: %.6f rev/day", sat->mean_motion);

        if (sat->is_active && ImGui::Button("Deactivate"))
            sat->is_active = false;
        else if (!sat->is_active && ImGui::Button("Activate"))
            sat->is_active = true;
    }
    ImGui::End();
}

/* -- Satellite Manager ----------------------------------------------------- */

static void DrawSatelliteManager(UIContext *ctx, AppConfig *cfg)
{
    if (!show_sat_mgr_dialog) return;

    ImGui::SetNextWindowSize(ImVec2(420, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Satellite Manager", &show_sat_mgr_dialog))
    {
        static char search_buf[64] = "";
        bool search_active = (search_buf[0] != '\0');

        // search box + Enable All / Disable All buttons on the same line
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 170);
        ImGui::InputText("##search", search_buf, sizeof(search_buf));
        ImGui::SameLine();

        if (ImGui::SmallButton("Enable All"))
        {
            for (int i = 0; i < sat_count; i++)
            {
                if (!search_active || str_contains_ic(satellites[i].name, search_buf))
                    satellites[i].is_active = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Disable All"))
        {
            for (int i = 0; i < sat_count; i++)
            {
                if (!search_active || str_contains_ic(satellites[i].name, search_buf))
                    satellites[i].is_active = false;
            }
        }

        // show count of displayed satellites
        int displayed = 0;
        for (int i = 0; i < sat_count; i++)
        {
            if (!search_active || str_contains_ic(satellites[i].name, search_buf))
                displayed++;
        }
        if (search_active)
        {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                               "%d / %d satellites", displayed, sat_count);
        }

        ImGui::Separator();
        ImGui::BeginChild("SatList");

        for (int i = 0; i < sat_count; i++)
        {
            // case-insensitive search matching
            if (search_active && !str_contains_ic(satellites[i].name, search_buf))
                continue;

            bool active = satellites[i].is_active;
            ImGui::PushID(i);

            // color code: active = normal, inactive = dimmed
            if (!active)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 0.7f));
            }

            char label[128];
            snprintf(label, sizeof(label), "%s##%d", satellites[i].name, i);

            ImVec2 selectable_size = ImVec2(ImGui::GetContentRegionAvail().x - 35, 20);
            if (ImGui::Selectable(label, *ctx->selected_sat == &satellites[i],
                                  ImGuiSelectableFlags_None, selectable_size))
            {
                *ctx->selected_sat = &satellites[i];
                show_sat_info_dialog = true;
            }

            if (!active)
            {
                ImGui::PopStyleColor();
            }

            // show/hide button on the right
            ImGui::SameLine(ImGui::GetWindowWidth() - 30);
            if (active)
            {
                if (ImGui::SmallButton("H"))
                    satellites[i].is_active = false;
            }
            else
            {
                if (ImGui::SmallButton("S"))
                    satellites[i].is_active = true;
            }

            ImGui::PopID();
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

/* -- Settings Window ------------------------------------------------------- */

static void DrawSettingsWindow(UIContext *ctx, AppConfig *cfg)
{
    if (!show_settings) return;

    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Settings", &show_settings))
    {
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

        if (ImGui::CollapsingHeader("Performance"))
        {
            int fps = cfg->target_fps;
            if (ImGui::SliderInt("Max FPS", &fps, 15, 240))
                cfg->target_fps = fps;
            ImGui::SliderFloat("UI Scale", &cfg->ui_scale, 0.5f, 2.0f);
        }

        if (ImGui::CollapsingHeader("Theme"))
        {
            /* scan themes directory */
            static char theme_names[1024] = "";
            static int active_theme = 0;
            if (theme_names[0] == '\0')
            {
                /* simple theme list */
                const char *themes[] = {"default", "girlypop", "trans-test"};
                theme_names[0] = '\0';
                for (int i = 0; i < 3; i++)
                {
                    if (i > 0) strcat(theme_names, ";");
                    strcat(theme_names, themes[i]);
                    if (strcmp(themes[i], cfg->theme) == 0) active_theme = i;
                }
            }

            int prev_theme = active_theme;
            ImGui::Combo("Theme", &active_theme, theme_names);
            if (active_theme != prev_theme)
            {
                /* extract theme name from semicolon-separated list */
                char temp[64];
                const char *start = theme_names;
                for (int i = 0; i < active_theme; i++)
                {
                    start = strchr(start, ';');
                    if (!start) break;
                    start++;
                }
                if (start)
                {
                    const char *end = strchr(start, ';');
                    if (end)
                    {
                        size_t len = end - start;
                        if (len > 63) len = 63;
                        strncpy(temp, start, len);
                        temp[len] = '\0';
                    }
                    else
                    {
                        strncpy(temp, start, 63);
                        temp[63] = '\0';
                    }
                    strncpy(cfg->theme, temp, 63);
                    cfg->reload_theme = true;
                }
            }
        }

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

        if (ImGui::CollapsingHeader("Data"))
        {
            // staleness threshold dropdown
            const char* stale_options[] = {
                "6 hours", "12 hours", "1 day", "2 days (default)",
                "3 days", "5 days", "7 days"
            };
            int stale_values[] = {
                STALE_THRESHOLD_6H, STALE_THRESHOLD_12H, STALE_THRESHOLD_1D,
                STALE_THRESHOLD_2D, STALE_THRESHOLD_3D, STALE_THRESHOLD_5D,
                STALE_THRESHOLD_7D
            };
            int current_stale_idx = 3; // default: 2 days
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

        if (ImGui::Button("Save Settings"))
        {
            SaveAppConfig("settings.json", cfg);
        }
    }
    ImGui::End();
}

/* -- Time Dialog ----------------------------------------------------------- */

static void DrawTimeDialog(UIContext *ctx, AppConfig *cfg)
{
    if (!show_time_dialog) return;

    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Time Control", &show_time_dialog))
    {
        ImGui::Text("Current: %s", ctx->datetime_str);
        { double v_min = 0.0, v_max = 3600.0; ImGui::SliderScalar("Speed", ImGuiDataType_Double, ctx->time_multiplier, &v_min, &v_max, "%.1fx"); }

        if (ImGui::Button("Pause/Resume"))
        {
            if (*ctx->time_multiplier != 0.0)
            {
                *ctx->saved_multiplier = *ctx->time_multiplier;
                *ctx->time_multiplier = 0.0;
            }
            else
            {
                *ctx->time_multiplier = (*ctx->saved_multiplier != 0.0) ? *ctx->saved_multiplier : 1.0;
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Reset to Now"))
        {
            *ctx->current_epoch = get_current_real_time_epoch();
            *ctx->time_multiplier = 1.0;
        }
    }
    ImGui::End();
}

/* -- Help Window ----------------------------------------------------------- */

static void DrawHelpWindow(UIContext *ctx, AppConfig *cfg)
{
    if (!show_help) return;

    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Help", &show_help))
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
        ImGui::BulletText("1: Settings");
        ImGui::BulletText("2: TLE Manager");
        ImGui::BulletText("3: Satellite Manager");
        ImGui::BulletText("4: Passes");
        ImGui::BulletText("5: Polar Plot");
        ImGui::BulletText("6: Scope");
        ImGui::BulletText("7: Help");
        ImGui::BulletText("8: Toggle 2D/3D");
        ImGui::BulletText("9: Hide unselected");
        ImGui::BulletText("0: Highlight sunlit");
        ImGui::BulletText("R: Rotator");
        ImGui::BulletText("Grave: Time dialog");

        if (ImGui::Button("GitHub Repository"))
        {
            OpenURL("https://github.com/aweeri/TLEscope");
        }
    }
    ImGui::End();
}

/* -- Scope Dialog ---------------------------------------------------------- */

static void DrawScopeDialog(UIContext *ctx, AppConfig *cfg)
{
    if (!show_scope_dialog) return;

    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Scope", &show_scope_dialog))
    {
        ImGui::SliderFloat("Azimuth", ctx->scope_az, 0.0f, 360.0f, "%.1f");
        ImGui::SliderFloat("Elevation", ctx->scope_el, -90.0f, 90.0f, "%.1f");
        ImGui::SliderFloat("Beam Width", ctx->scope_beam, 1.0f, 120.0f, "%.1f");

        ImGui::Separator();
        ImGui::Checkbox("Show LEO", &g_ui.scope_show_leo);
        ImGui::SameLine();
        ImGui::Checkbox("Show HEO/MEO", &g_ui.scope_show_heo);
        ImGui::SameLine();
        ImGui::Checkbox("Show GEO", &g_ui.scope_show_geo);
        ImGui::SameLine();
        ImGui::Checkbox("Show Trails", &g_ui.scope_show_trails);
    }
    ImGui::End();
}

/* -- Data Sources Dialog --------------------------------------------------- */

static void DrawDataSourcesDialog(UIContext *ctx, AppConfig *cfg)
{
    if (!show_tle_mgr_dialog) return;

    ImGui::SetNextWindowSize(ImVec2(600, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Data Sources", &show_tle_mgr_dialog))
    {
        // -- Retlector Section (baseline) -------------------------------------
        // note: no DefaultOpen flag, stays closed until user clicks it.
        // fetch happens in a background thread so the UI never blocks.
        {
            static std::atomic<bool> s_fetch_running{false};
            static std::atomic<bool> s_fetch_done{false};
            static RetlectorGroup s_pending[MAX_RETLECTOR_GROUPS];
            static int s_pending_count = 0;
            static std::thread s_fetch_thread;

            if (ImGui::CollapsingHeader("Retlector (baseline)"))
            {
                // start async fetch on first expand (and on retry)
                if (!cfg->retlector_groups_fetched && !s_fetch_running.load())
                {
                    s_fetch_running = true;
                    s_fetch_done = false;
                    s_fetch_thread = std::thread([]() {
                        s_pending_count = FetchRetlectorGroups(s_pending, MAX_RETLECTOR_GROUPS);
                        s_fetch_done = true;
                    });
                    s_fetch_thread.detach();
                }

                // if the background fetch finished, copy results into config
                if (s_fetch_done.load())
                {
                    if (s_pending_count > 0)
                    {
                        for (int i = 0; i < s_pending_count && i < MAX_RETLECTOR_GROUPS; i++)
                            cfg->retlector_groups[i] = s_pending[i];
                        cfg->retlector_group_count = s_pending_count;
                        cfg->retlector_groups_fetched = true;
                        LOG_INFO("Loaded %d retlector groups", s_pending_count);
                    }
                    else
                    {
                        LOG_ERROR("Failed to fetch retlector groups");
                    }
                    s_fetch_done = false;
                    s_fetch_running = false;
                }

                if (s_fetch_running.load())
                {
                    ImGui::Text("Discovering available data sources...");
                    ImGui::SameLine();
                    static float spinner_angle = 0.0f;
                    spinner_angle += ImGui::GetIO().DeltaTime * 180.0f;
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "%c",
                                       "|/-\\"[(int)(spinner_angle / 45.0f) % 4]);
                }
                else if (cfg->retlector_group_count > 0)
                {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%d sources available (CSV format)",
                                       cfg->retlector_group_count);
                    ImGui::Separator();
                    for (int i = 0; i < cfg->retlector_group_count; i++)
                    {
                        RetlectorGroup *g = &cfg->retlector_groups[i];
                        ImGui::PushID(i);
                        ImGui::Checkbox(g->name, &g->selected);
                        ImGui::PopID();
                    }
                }
                else
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Failed to reach retlector.eu");
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Use Celestrak fallback below.");
                    if (ImGui::SmallButton("Retry"))
                    {
                        cfg->retlector_groups_fetched = false;
                    }
                }
            }
            else if (!cfg->retlector_groups_fetched && s_fetch_running.load())
            {
                // header collapsed mid-fetch: keep fetching in background,
                // results will be picked up when the header reopens.
            }
        }

        // -- Celestrak Section (fallback) -------------------------------------
        if (ImGui::CollapsingHeader("Celestrak (fallback)"))
        {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "CSV format (default)");
            ImGui::Separator();
            for (int i = 0; i < NUM_CELESTRAK_SOURCES && i < 25; i++)
            {
                ImGui::PushID(i + 1000);
                ImGui::Checkbox(CELESTRAK_SOURCES[i].name, &celestrak_sel[i]);
                ImGui::PopID();
            }
        }

        // -- Custom URL Section ------------------------------------------------
        if (ImGui::CollapsingHeader("Custom URL"))
        {
            ImGui::InputText("##custom_url", g_ui.custom_url_buf, sizeof(g_ui.custom_url_buf));
            ImGui::SameLine();
            if (ImGui::Button("Fetch") && g_ui.custom_url_buf[0])
            {
                FetchResult result = FetchFromCustomURL(g_ui.custom_url_buf);
                if (result.success)
                {
                    const char *fmt_name = FormatToString(result.format);
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Detected: %s", fmt_name);
                    LOG_INFO("Custom URL fetched: %s, format: %s", g_ui.custom_url_buf, fmt_name);

                    // extract a name from the URL (before the if block so it's in scope)
                    const char *name_start = strrchr(g_ui.custom_url_buf, '/');
                    if (name_start) name_start++; else name_start = g_ui.custom_url_buf;

                    // add as a custom data source
                    if (cfg->custom_data_source_count < MAX_CUSTOM_DATA_SOURCES)
                    {
                        CustomDataSource *ds = &cfg->custom_data_sources[cfg->custom_data_source_count];
                        snprintf(ds->name, sizeof(ds->name), "%.63s", name_start);
                        snprintf(ds->url, sizeof(ds->url), "%s", g_ui.custom_url_buf);
                        ds->preferred_format = result.format;
                        ds->selected = true;
                        cfg->custom_data_source_count++;
                    }

                    // parse the fetched data immediately
                    int before = sat_count;
                    if (result.format == FORMAT_TLE)
                    {
                        char *ptr = result.data;
                        char l0[256], l1[256], l2[256];
                        while (*ptr && sat_count < MAX_SATELLITES)
                        {
                            while (*ptr == '\r' || *ptr == '\n') ptr++;
                            if (!*ptr) break;
                            if (*ptr == '#') { while (*ptr && *ptr != '\n') ptr++; continue; }
                            int j = 0;
                            while (*ptr && *ptr != '\n' && j < 255) l0[j++] = *ptr++;
                            l0[j] = '\0'; if (*ptr == '\n') ptr++;
                            j = 0;
                            while (*ptr && *ptr != '\n' && j < 255) l1[j++] = *ptr++;
                            l1[j] = '\0'; if (*ptr == '\n') ptr++;
                            j = 0;
                            while (*ptr && *ptr != '\n' && j < 255) l2[j++] = *ptr++;
                            l2[j] = '\0'; if (*ptr == '\n') ptr++;
                            OrbitalDataMeta meta = {0};
                            snprintf(meta.source_name, sizeof(meta.source_name), "custom:%.31s", name_start);
                            meta.format = result.format;
                            meta.fetch_time = time(NULL);
                            add_satellite_from_tle(l0, l1, l2, &meta);
                        }
                    }
                    else if (result.format == FORMAT_OMM_JSON)
                    {
                        ParseOMMJson(result.data, result.size, satellites, &sat_count, MAX_SATELLITES,
                                     "custom_url", result.format);
                    }
                    else if (result.format == FORMAT_OMM_CSV)
                    {
                        ParseOMMCsv(result.data, result.size, satellites, &sat_count, MAX_SATELLITES,
                                    "custom_url", result.format);
                    }
                    if (sat_count > before)
                        SaveOrbitalData("data.json", satellites, sat_count);

                    FreeFetchResult(&result);
                    g_ui.custom_url_buf[0] = '\0';
                }
                else
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Fetch failed (HTTP %ld)", result.http_code);
                    FreeFetchResult(&result);
                }
            }

            // show detected format for current URL input
            if (g_ui.custom_url_buf[0])
            {
                // we can't detect format from URL alone, just show a hint
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Enter URL and press Fetch to auto-detect format");
            }

            // list custom data sources with checkboxes and remove buttons
            if (cfg->custom_data_source_count > 0)
            {
                ImGui::Separator();
                for (int i = 0; i < cfg->custom_data_source_count; i++)
                {
                    CustomDataSource *ds = &cfg->custom_data_sources[i];
                    ImGui::PushID(i + 3000);

                    // show name with format badge
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[%s]",
                                       FormatToString(ds->preferred_format));
                    ImGui::SameLine();
                    ImGui::Checkbox(ds->name, &ds->selected);

                    // remove button
                    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 30);
                    if (ImGui::SmallButton("X"))
                    {
                        for (int j = i; j < cfg->custom_data_source_count - 1; j++)
                            cfg->custom_data_sources[j] = cfg->custom_data_sources[j + 1];
                        cfg->custom_data_source_count--;
                        ImGui::PopID();
                        break;
                    }

                    ImGui::PopID();
                }
            }
        }

        // -- Paste Entry Section ----------------------------------------------
        if (ImGui::CollapsingHeader("Paste Entry"))
        {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "Accepted formats: TLE, JSON OMM, CSV OMM, KVN OMM, XML OMM");
            ImGui::InputTextMultiline("##paste", g_ui.custom_paste_buf, sizeof(g_ui.custom_paste_buf),
                                      ImVec2(0, 100));

            // detect format of current paste buffer
            OrbitalDataFormat paste_fmt = FORMAT_UNKNOWN;
            bool has_content = (g_ui.custom_paste_buf[0] != '\0');
            if (has_content)
            {
                paste_fmt = DetectDataFormat(g_ui.custom_paste_buf,
                                              strlen(g_ui.custom_paste_buf));
            }

            bool can_add = has_content && (paste_fmt != FORMAT_UNKNOWN) &&
                           (cfg->custom_entry_count < MAX_CUSTOM_ENTRIES);

            // grey out button if format is unknown or no content
            if (!can_add)
                ImGui::BeginDisabled();

            if (ImGui::Button("Add Entry"))
            {
                const char *fmt_name = FormatToString(paste_fmt);
                LOG_INFO("Pasted entry detected format: %s", fmt_name);

                CustomEntry *e = &cfg->custom_entries[cfg->custom_entry_count];
                strncpy(e->data, g_ui.custom_paste_buf, sizeof(e->data) - 1);
                e->detected_format = paste_fmt;
                e->selected = true;
                cfg->custom_entry_count++;
                g_ui.custom_paste_buf[0] = '\0';
            }

            if (!can_add)
                ImGui::EndDisabled();

            // show detected format for current paste buffer
            if (has_content)
            {
                ImGui::SameLine();
                if (paste_fmt != FORMAT_UNKNOWN)
                {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Detected: %s", FormatToString(paste_fmt));
                }
                else
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Format unknown - not a valid orbital data format");
                }
            }

            // list custom entries
            if (cfg->custom_entry_count > 0)
            {
                ImGui::Separator();
                for (int i = 0; i < cfg->custom_entry_count; i++)
                {
                    CustomEntry *e = &cfg->custom_entries[i];
                    ImGui::PushID(i + 2000);

                    // show first line as preview
                    char preview[64];
                    const char *nl = strchr(e->data, '\n');
                    if (nl)
                    {
                        int len = (int)(nl - e->data);
                        if (len > 60) len = 60;
                        strncpy(preview, e->data, len);
                        preview[len] = '\0';
                    }
                    else
                    {
                        strncpy(preview, e->data, 60);
                        preview[60] = '\0';
                    }

                    bool was_selected = e->selected;
                    ImGui::Checkbox(preview, &e->selected);

                    // if unchecked, remove the entry
                    if (was_selected && !e->selected)
                    {
                        for (int j = i; j < cfg->custom_entry_count - 1; j++)
                            cfg->custom_entries[j] = cfg->custom_entries[j + 1];
                        cfg->custom_entry_count--;
                        ImGui::PopID();
                        break;
                    }

                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[%s]", FormatToString(e->detected_format));
                    ImGui::PopID();
                }
            }
        }

        // -- Pull Button -------------------------------------------------------
        ImGui::Separator();
        if (ImGui::Button("Pull Selected Sources", ImVec2(ImGui::GetContentRegionAvail().x, 30)))
        {
            // step 1: clear satellites from sources that are no longer selected
            // we identify satellites by their data_meta.source_name prefix
            int new_count = 0;
            for (int i = 0; i < sat_count; i++)
            {
                bool keep = false;

                // check if this sat belongs to a selected retlector group
                for (int j = 0; j < cfg->retlector_group_count; j++)
                {
                    if (cfg->retlector_groups[j].selected)
                    {
                        char expected[80];
                        snprintf(expected, sizeof(expected), "retlector:%s", cfg->retlector_groups[j].name);
                        if (strcmp(satellites[i].data_meta.source_name, expected) == 0)
                        {
                            keep = true;
                            break;
                        }
                    }
                }

                // check if this sat belongs to a selected celestrak source
                if (!keep)
                {
                    for (int j = 0; j < NUM_CELESTRAK_SOURCES && j < 25; j++)
                    {
                        if (celestrak_sel[j])
                        {
                            char expected[80];
                            snprintf(expected, sizeof(expected), "celestrak:%s", CELESTRAK_SOURCES[j].name);
                            if (strcmp(satellites[i].data_meta.source_name, expected) == 0)
                            {
                                keep = true;
                                break;
                            }
                        }
                    }
                }

                // check if this sat belongs to a selected custom source
                if (!keep)
                {
                    for (int j = 0; j < cfg->custom_data_source_count; j++)
                    {
                        if (cfg->custom_data_sources[j].selected)
                        {
                            char expected[80];
                            snprintf(expected, sizeof(expected), "custom:%.31s", cfg->custom_data_sources[j].name);
                            if (strcmp(satellites[i].data_meta.source_name, expected) == 0)
                            {
                                keep = true;
                                break;
                            }
                        }
                    }
                }

                // check if this sat belongs to a selected custom entry
                if (!keep)
                {
                    for (int j = 0; j < cfg->custom_entry_count; j++)
                    {
                        if (cfg->custom_entries[j].selected)
                        {
                            char expected[80];
                            snprintf(expected, sizeof(expected), "paste:%d", j);
                            if (strcmp(satellites[i].data_meta.source_name, expected) == 0)
                            {
                                keep = true;
                                break;
                            }
                        }
                    }
                }

                if (keep)
                {
                    if (new_count != i)
                        satellites[new_count] = satellites[i];
                    new_count++;
                }
            }
            sat_count = new_count;

            // step 2: fetch selected retlector groups (CSV format)
            for (int i = 0; i < cfg->retlector_group_count; i++)
            {
                if (!cfg->retlector_groups[i].selected) continue;

                char url[512];
                snprintf(url, sizeof(url), "https://retlector.eu/%s/csv", cfg->retlector_groups[i].name);
                FetchResult result = FetchFromCustomURL(url);
                if (result.success)
                {
                    int before = sat_count;
                    int parsed = ParseOMMCsv(result.data, result.size, satellites, &sat_count,
                                              MAX_SATELLITES, "", FORMAT_OMM_CSV);
                    // tag each parsed satellite with the source name
                    char source_tag[80];
                    snprintf(source_tag, sizeof(source_tag), "retlector:%s", cfg->retlector_groups[i].name);
                    for (int s = before; s < sat_count; s++)
                        strncpy(satellites[s].data_meta.source_name, source_tag,
                                sizeof(satellites[s].data_meta.source_name) - 1);
                    LOG_INFO("Retlector %s: parsed %d satellites", cfg->retlector_groups[i].name, sat_count - before);
                    FreeFetchResult(&result);
                }
            }

            // step 3: fetch selected celestrak sources (CSV format)
            {
                for (int i = 0; i < NUM_CELESTRAK_SOURCES && i < 25; i++)
                {
                    if (!celestrak_sel[i]) continue;

                    FetchResult result = FetchFromSource(&CELESTRAK_SOURCES[i], FORMAT_OMM_CSV);
                    if (result.success)
                    {
                        int before = sat_count;
                        int parsed = ParseOMMCsv(result.data, result.size, satellites, &sat_count,
                                                  MAX_SATELLITES, "", FORMAT_OMM_CSV);
                        char source_tag[80];
                        snprintf(source_tag, sizeof(source_tag), "celestrak:%s", CELESTRAK_SOURCES[i].name);
                        for (int s = before; s < sat_count; s++)
                            strncpy(satellites[s].data_meta.source_name, source_tag,
                                    sizeof(satellites[s].data_meta.source_name) - 1);
                        LOG_INFO("Celestrak %s: parsed %d satellites", CELESTRAK_SOURCES[i].name, sat_count - before);
                        FreeFetchResult(&result);
                    }
                }
            }

            // step 4: fetch selected custom data sources
            for (int i = 0; i < cfg->custom_data_source_count; i++)
            {
                if (!cfg->custom_data_sources[i].selected) continue;

                FetchResult result = FetchFromCustomURL(cfg->custom_data_sources[i].url);
                if (result.success)
                {
                    int before = sat_count;
                    if (result.format == FORMAT_TLE)
                    {
                        char *ptr = result.data;
                        char l0[256], l1[256], l2[256];
                        while (*ptr && sat_count < MAX_SATELLITES)
                        {
                            while (*ptr == '\r' || *ptr == '\n') ptr++;
                            if (!*ptr) break;
                            if (*ptr == '#') { while (*ptr && *ptr != '\n') ptr++; continue; }
                            int j = 0;
                            while (*ptr && *ptr != '\n' && j < 255) l0[j++] = *ptr++;
                            l0[j] = '\0'; if (*ptr == '\n') ptr++;
                            j = 0;
                            while (*ptr && *ptr != '\n' && j < 255) l1[j++] = *ptr++;
                            l1[j] = '\0'; if (*ptr == '\n') ptr++;
                            j = 0;
                            while (*ptr && *ptr != '\n' && j < 255) l2[j++] = *ptr++;
                            l2[j] = '\0'; if (*ptr == '\n') ptr++;
                            OrbitalDataMeta meta = {0};
                            snprintf(meta.source_name, sizeof(meta.source_name), "custom:%.31s",
                                     cfg->custom_data_sources[i].name);
                            meta.format = result.format;
                            meta.fetch_time = time(NULL);
                            add_satellite_from_tle(l0, l1, l2, &meta);
                        }
                    }
                    else if (result.format == FORMAT_OMM_JSON)
                    {
                        ParseOMMJson(result.data, result.size, satellites, &sat_count, MAX_SATELLITES,
                                     cfg->custom_data_sources[i].name, result.format);
                    }
                    else if (result.format == FORMAT_OMM_CSV)
                    {
                        ParseOMMCsv(result.data, result.size, satellites, &sat_count, MAX_SATELLITES,
                                    cfg->custom_data_sources[i].name, result.format);
                    }
                    LOG_INFO("Custom URL %s: parsed %d satellites", cfg->custom_data_sources[i].url,
                             sat_count - before);
                    FreeFetchResult(&result);
                }
            }

            // step 5: parse selected custom entries
            for (int i = 0; i < cfg->custom_entry_count; i++)
            {
                if (!cfg->custom_entries[i].selected) continue;

                CustomEntry *e = &cfg->custom_entries[i];
                int before = sat_count;

                if (e->detected_format == FORMAT_TLE)
                {
                    char *ptr = e->data;
                    char l0[256], l1[256], l2[256];
                    while (*ptr && sat_count < MAX_SATELLITES)
                    {
                        while (*ptr == '\r' || *ptr == '\n') ptr++;
                        if (!*ptr) break;
                        if (*ptr == '#') { while (*ptr && *ptr != '\n') ptr++; continue; }
                        int j = 0;
                        while (*ptr && *ptr != '\n' && j < 255) l0[j++] = *ptr++;
                        l0[j] = '\0'; if (*ptr == '\n') ptr++;
                        j = 0;
                        while (*ptr && *ptr != '\n' && j < 255) l1[j++] = *ptr++;
                        l1[j] = '\0'; if (*ptr == '\n') ptr++;
                        j = 0;
                        while (*ptr && *ptr != '\n' && j < 255) l2[j++] = *ptr++;
                        l2[j] = '\0'; if (*ptr == '\n') ptr++;
                        OrbitalDataMeta meta = {0};
                        snprintf(meta.source_name, sizeof(meta.source_name), "paste:%d", i);
                        meta.format = e->detected_format;
                        meta.fetch_time = time(NULL);
                        add_satellite_from_tle(l0, l1, l2, &meta);
                    }
                }
                else if (e->detected_format == FORMAT_OMM_JSON)
                {
                    ParseOMMJson(e->data, strlen(e->data), satellites, &sat_count, MAX_SATELLITES,
                                 "paste", e->detected_format);
                }
                else if (e->detected_format == FORMAT_OMM_CSV)
                {
                    ParseOMMCsv(e->data, strlen(e->data), satellites, &sat_count, MAX_SATELLITES,
                                "paste", e->detected_format);
                }

                LOG_INFO("Custom entry %d: parsed %d satellites", i, sat_count - before);
            }

            SaveOrbitalData("data.json", satellites, sat_count);
            LOG_INFO("Pull complete: %d satellites total", sat_count);
        }
    }
    ImGui::End();
}

/* -- Passes Dialog --------------------------------------------------------- */

static void DrawPassesDialog(UIContext *ctx, AppConfig *cfg)
{
    if (!show_passes_dialog) return;

    ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Satellite Passes", &show_passes_dialog))
    {
        static char min_el_buf[8] = "0";
        ImGui::InputText("Min Elevation", min_el_buf, sizeof(min_el_buf));

        if (ImGui::Button("Calculate Passes"))
        {
            if (*ctx->selected_sat)
                CalculatePasses(*ctx->selected_sat, *ctx->current_epoch);
            else
                CalculatePasses(NULL, *ctx->current_epoch);
        }

        ImGui::Separator();
        ImGui::BeginChild("PassList");

        for (int i = 0; i < num_passes; i++)
        {
            char label[128];
            snprintf(label, sizeof(label), "%s - El: %.1f",
                     passes[i].sat ? passes[i].sat->name : "Unknown",
                     passes[i].max_el);

            if (ImGui::Selectable(label, i == g_ui.selected_pass_idx))
            {
                g_ui.selected_pass_idx = i;
            }
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

/* -- Polar Plot Dialog ----------------------------------------------------- */

static void DrawPolarPlotDialog(UIContext *ctx, AppConfig *cfg)
{
    if (!show_polar_dialog) return;

    ImGui::SetNextWindowSize(ImVec2(400, 450), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Polar Plot", &show_polar_dialog))
    {
        ImGui::Checkbox("Lunar Mode", &g_ui.polar_lunar_mode);

        if (g_ui.selected_pass_idx >= 0 && g_ui.selected_pass_idx < num_passes)
        {
            SatPass *pass = &passes[g_ui.selected_pass_idx];
            ImGui::Text("Satellite: %s", pass->sat ? pass->sat->name : "N/A");
            ImGui::Text("Max Elevation: %.1f", pass->max_el);
            ImGui::Text("AOS: %.2f", pass->aos_epoch);
            ImGui::Text("LOS: %.2f", pass->los_epoch);
        }
        else
        {
            ImGui::Text("Select a pass from the Passes dialog");
        }

        ImGui::Separator();
        if (ImGui::Button("Jump to AOS"))
        {
            if (g_ui.selected_pass_idx >= 0 && g_ui.selected_pass_idx < num_passes)
            {
                *ctx->current_epoch = passes[g_ui.selected_pass_idx].aos_epoch;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Doppler Analysis"))
        {
            show_doppler_dialog = true;
        }
    }
    ImGui::End();
}

/* -- Doppler Analysis Dialog ------------------------------------------------ */

static void DrawDopplerDialog(UIContext *ctx, AppConfig *cfg)
{
    if (!show_doppler_dialog) return;

    ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Doppler Analysis", &show_doppler_dialog))
    {
        static float freq = 145800000.0f; /* default: 2m band */
        static float csv_res = 1.0f;
        static char csv_path[128] = "doppler_export.csv";

        ImGui::InputFloat("Frequency (Hz)", &freq, 1000.0f, 1000000.0f, "%.0f");
        ImGui::InputFloat("CSV Resolution (s)", &csv_res, 0.1f, 10.0f);
        ImGui::InputText("Export Path", csv_path, sizeof(csv_path));

        if (ImGui::Button("Export CSV"))
        {
            /* TODO: Implement CSV export */
            ImGui::Text("Export triggered!");
        }
    }
    ImGui::End();
}

/* -- Rotator Control Dialog ------------------------------------------------ */

static void DrawRotatorDialog(UIContext *ctx, AppConfig *cfg)
{
    if (!rot_show_window) return;

    ImGui::SetNextWindowSize(ImVec2(350, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Rotator Control", &rot_show_window))
    {
        if (ImGui::Button(RotatorIsConnected() ? "Disconnect" : "Connect"))
        {
            if (RotatorIsConnected())
                RotatorDisconnect();
            else
                RotatorConnect();
        }

        ImGui::Separator();
        if (RotatorIsConnected())
        {
            ImGui::Text("Status: Connected");
            ImGui::Text("Az: %.1f  El: %.1f", RotatorGetAz(), RotatorGetEl());

            if (ImGui::Button("Auto Steer"))
            {
                RotatorSetAutoSteer(!RotatorGetAutoSteer());
            }

            ImGui::SameLine();
            if (ImGui::Button("Poll"))
            {
                RotatorPollNow();
            }

            ImGui::Separator();
            ImGui::Text("Raw Commands:");
            static char cmd_buf[64] = "";
            ImGui::InputText("##cmd", cmd_buf, sizeof(cmd_buf));
            if (ImGui::Button("Send"))
            {
                RotatorSendCustomNow();
            }
        }
        else
        {
            ImGui::Text("Status: Disconnected");
        }
    }
    ImGui::End();
}

/* -- TLE Warning Dialog ---------------------------------------------------- */

static void DrawDataWarning(UIContext *ctx, AppConfig *cfg)
{
    if (!show_tle_warning) return;

    // check if any satellite data is older than the configured threshold
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

    // format the threshold for display
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
            show_tle_mgr_dialog = true;
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

/* -- Log Window ------------------------------------------------------------ */

static const char *LogLevelFilterLabel(int idx)
{
    switch (idx)
    {
        case 0:  return "ALL";
        case 1:  return "INFO+";
        case 2:  return "WARN+";
        case 3:  return "ERROR";
        default: return "ALL";
    }
}

static LogLevel LogLevelFilterMinLevel(int idx)
{
    switch (idx)
    {
        case 0:  return LOG_LEVEL_DEBUG;  /* show everything */
        case 1:  return LOG_LEVEL_INFO;   /* INFO and above */
        case 2:  return LOG_LEVEL_WARN;   /* WARN and above */
        case 3:  return LOG_LEVEL_ERROR;  /* ERROR only */
        default: return LOG_LEVEL_DEBUG;
    }
}

static void DrawLogWindow(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    (void)cfg;

    if (!show_log_window) return;

    ImGui::SetNextWindowSize(ImVec2(600, 300), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(50, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Log", &show_log_window))
    {
        /* toolbar row inside the log window */
        if (ImGui::Button("Clear"))
        {
            LogClear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &log_auto_scroll);
        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();

        /* level filter dropdown */
        static int log_level_filter = 0;
        ImGui::SetNextItemWidth(100.0f);
        ImGui::Combo("##filter", &log_level_filter, "ALL\0INFO+\0WARN+\0ERROR\0");
        LogLevel min_level = LogLevelFilterMinLevel(log_level_filter);

        ImGui::Separator();

        /* scrollable log area */
        ImGui::BeginChild("LogEntries", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar);

        /* get head index before locking to avoid deadlock with LogLock */
        int head = LogGetHeadIndex();

        int count;
        const LogEntry *entries = LogLock(&count);

        /* the ring buffer stores entries in chronological order starting from
         * the oldest at (head - count) mod capacity, wrapping around. */
        int head_for_read = (head - count + LOG_RING_CAPACITY) % LOG_RING_CAPACITY;

        for (int i = 0; i < count; i++)
        {
            int idx = (head_for_read + i) % LOG_RING_CAPACITY;
            const LogEntry *e = &entries[idx];

            /* skip entries below the selected filter level */
            if (e->level < min_level)
                continue;

            /* choose colour based on level */
            ImVec4 color;
            switch (e->level)
            {
                case LOG_LEVEL_DEBUG: color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f); break; /* grey */
                case LOG_LEVEL_INFO:  color = ImVec4(0.8f, 0.9f, 1.0f, 1.0f); break; /* light blue */
                case LOG_LEVEL_WARN:  color = ImVec4(1.0f, 0.9f, 0.4f, 1.0f); break; /* yellow */
                case LOG_LEVEL_ERROR: color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); break; /* red */
                default:             color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            }

            /* build a single selectable line: "HH:MM:SS message" */
            char line_buf[576];
            snprintf(line_buf, sizeof(line_buf), "%s %s", e->timestamp, e->message);

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::Selectable(line_buf);
            ImGui::PopStyleColor();
        }

        /* auto-scroll to bottom */
        if (log_auto_scroll && count > 0)
        {
            ImGui::SetScrollHereY(1.0f);
        }

        LogUnlock();
        ImGui::EndChild();
    }
    ImGui::End();
}

/* -- Main DrawGUI ---------------------------------------------------------- */

void DrawGUI(UIContext *ctx, AppConfig *cfg, Font customFont)
{
    /* one-time ImGui initialization */
    static bool imgui_inited = false;
    if (!imgui_inited) {
        rlImGuiSetup(true);

        /* custom ImGui style: subtle rounding */
        ImGuiStyle &style = ImGui::GetStyle();
        style.WindowRounding = 3.0f;
        style.FrameRounding = 2.0f;
        style.ChildRounding = 3.0f;
        style.PopupRounding = 3.0f;
        style.GrabRounding = 2.0f;
        style.ScrollbarRounding = 2.0f;
        style.TabRounding = 2.0f;
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.WindowPadding = ImVec2(10, 10);
        style.FramePadding = ImVec2(6, 4);
        style.ItemSpacing = ImVec2(8, 6);

        /* load FontAwesome icons as a second font, merge with default */
        ImGuiIO& io = ImGui::GetIO();
        ImFontConfig icons_config;
        icons_config.MergeMode = false;
        icons_config.PixelSnapH = true;
        icons_config.FontDataOwnedByAtlas = false;
        // load default font first
        io.Fonts->AddFontDefault();
        // then merge FontAwesome icons
        icons_config.MergeMode = true;
        icons_config.FontDataOwnedByAtlas = true;  // we don't own the data
        static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
        io.Fonts->AddFontFromMemoryCompressedTTF(
            fa_solid_900_compressed_data,
            fa_solid_900_compressed_size,
            14.0f, &icons_config, icon_ranges);

        /* build font atlas and upload to GPU */
        io.Fonts->Build();
        unsigned char *pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        Image img = { pixels, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        Texture tex = LoadTextureFromImage(img);
        io.Fonts->SetTexID((ImTextureID)(intptr_t)tex.id);

        imgui_inited = true;
    }

    /* begin rlImGui frame */
    rlImGuiBegin();

    /* draw floating bars (top icons + bottom time controls) */
    DrawTopBar(ctx, cfg);
    DrawBottomBar(ctx, cfg);

    /* draw dialogs */
    DrawFirstRunDialog(ctx, cfg);
    DrawDataWarning(ctx, cfg);
    DrawUpdateCheck(ctx, cfg);
    DrawSatelliteManager(ctx, cfg);
    DrawSatelliteInfo(ctx, cfg);
    DrawDataSourcesDialog(ctx, cfg);
    DrawSettingsWindow(ctx, cfg);
    DrawTimeDialog(ctx, cfg);
    DrawHelpWindow(ctx, cfg);
    DrawScopeDialog(ctx, cfg);
    DrawPassesDialog(ctx, cfg);
    DrawPolarPlotDialog(ctx, cfg);
    DrawDopplerDialog(ctx, cfg);
    DrawRotatorDialog(ctx, cfg);
    DrawLogWindow(ctx, cfg);
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