/*
 * ui_imgui.cpp — Dear ImGui UI implementation
 *
 * This file implements the UI layer using Dear ImGui + rlImGui.
 */

#include "ui.h"
#include "astro.h"
#include "rotator.h"
#include "config.h"
#include "provider.h"
#include "cache.h"
#include "storage.h"
#include "log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>

#include <raylib.h>
#include <raymath.h>

#include "imgui.h"
#include "rlImGui.h"

/* ── UIState instance ────────────────────────────────────────────────────── */

static UIState g_ui = {0};

/* ── Helpers ─────────────────────────────────────────────────────────────── */

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

/* ── UI State (previously static vars in ui.c) ──────────────────────────── */

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
static bool log_auto_scroll = true;

/* ── Toolbar ─────────────────────────────────────────────────────────────── */

static void DrawToolbar(UIContext *ctx, AppConfig *cfg)
{
    if (ImGui::BeginMainMenuBar())
    {
        /* Time controls */
        ImGui::Text("Time: %.2fx", *ctx->time_multiplier);
        ImGui::SameLine();

        if (ImGui::Button("||")) { *ctx->saved_multiplier = *ctx->time_multiplier; *ctx->time_multiplier = 0.0; }
        ImGui::SameLine();
        if (ImGui::Button(">")) { if (*ctx->time_multiplier == 0.0) *ctx->time_multiplier = (*ctx->saved_multiplier != 0.0) ? *ctx->saved_multiplier : 1.0; }
        ImGui::SameLine();
        if (ImGui::Button(">>")) { *ctx->time_multiplier = StepTimeMultiplier(*ctx->time_multiplier, true); }
        ImGui::SameLine();
        if (ImGui::Button("<<")) { *ctx->time_multiplier = StepTimeMultiplier(*ctx->time_multiplier, false); }
        ImGui::SameLine();
        if (ImGui::Button("Now")) { *ctx->current_epoch = get_current_real_time_epoch(); *ctx->time_multiplier = 1.0; }

        ImGui::Separator();
        ImGui::SameLine();

        /* View controls */
        if (ImGui::Button(*ctx->is_2d_view ? "3D" : "2D")) { *ctx->is_2d_view = !*ctx->is_2d_view; }
        ImGui::SameLine();
        if (ImGui::Button("Frame")) { *ctx->is_ecliptic_frame = !*ctx->is_ecliptic_frame; }
        ImGui::SameLine();
        if (ImGui::Button("POV")) { *ctx->is_pov_mode = !*ctx->is_pov_mode; }

        ImGui::Separator();
        ImGui::SameLine();

        /* Dialog toggles */
        if (ImGui::Button("Sats")) show_sat_mgr_dialog = !show_sat_mgr_dialog;
        ImGui::SameLine();
        if (ImGui::Button("TLE")) show_tle_mgr_dialog = !show_tle_mgr_dialog;
        ImGui::SameLine();
        if (ImGui::Button("Passes")) show_passes_dialog = !show_passes_dialog;
        ImGui::SameLine();
        if (ImGui::Button("Polar")) show_polar_dialog = !show_polar_dialog;
        ImGui::SameLine();
        if (ImGui::Button("Scope")) show_scope_dialog = !show_scope_dialog;
        ImGui::SameLine();
        if (ImGui::Button("Rotator")) rot_show_window = !rot_show_window;
        ImGui::SameLine();
        if (ImGui::Button("Time")) show_time_dialog = !show_time_dialog;
        ImGui::SameLine();
        if (ImGui::Button("Settings")) show_settings = !show_settings;
        ImGui::SameLine();
        if (ImGui::Button("Log")) show_log_window = !show_log_window;
        ImGui::SameLine();
        if (ImGui::Button("Help")) show_help = !show_help;

        ImGui::EndMainMenuBar();
    }
}

/* ── Satellite Info Panel ────────────────────────────────────────────────── */

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

        // Velocity display requires a velocity field in Satellite struct (not yet available)

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

/* ── Satellite Manager ───────────────────────────────────────────────────── */

static void DrawSatelliteManager(UIContext *ctx, AppConfig *cfg)
{
    if (!show_sat_mgr_dialog) return;

    ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Satellite Manager", &show_sat_mgr_dialog))
    {
        static char search_buf[64] = "";
        ImGui::InputText("Search", search_buf, sizeof(search_buf));

        ImGui::Separator();
        ImGui::BeginChild("SatList");

        for (int i = 0; i < sat_count; i++)
        {
            if (search_buf[0] != '\0' && !strstr(satellites[i].name, search_buf))
                continue;

            bool active = satellites[i].is_active;
            ImGui::PushID(i);

            char label[128];
            snprintf(label, sizeof(label), "%s##%d", satellites[i].name, i);

            if (ImGui::Selectable(label, *ctx->selected_sat == &satellites[i], ImGuiSelectableFlags_None, ImVec2(0, 20)))
            {
                *ctx->selected_sat = &satellites[i];
                show_sat_info_dialog = true;
            }

            ImGui::SameLine(ImGui::GetWindowWidth() - 30);
            if (ImGui::SmallButton(active ? "H" : "S"))
                satellites[i].is_active = !active;

            ImGui::PopID();
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

/* ── Settings Window ─────────────────────────────────────────────────────── */

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
            /* Scan themes directory */
            static char theme_names[1024] = "";
            static int active_theme = 0;
            if (theme_names[0] == '\0')
            {
                /* Simple theme list */
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
                /* Extract theme name from semicolon-separated list */
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

        if (ImGui::Button("Save Settings"))
        {
            SaveAppConfig("settings.json", cfg);
        }
    }
    ImGui::End();
}

/* ── Time Dialog ─────────────────────────────────────────────────────────── */

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

/* ── Help Window ─────────────────────────────────────────────────────────── */

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

/* ── Scope Dialog ────────────────────────────────────────────────────────── */

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

/* ── Data Sources Dialog ─────────────────────────────────────────────────── */

static bool celestrak_selected[25] = {false};
static bool retlector_selected[25] = {false};
static int celestrak_format_idx[25] = {0};  // 0=TLE, 1=JSON, 2=CSV
static int retlector_format_idx[25] = {0};

static void DrawDataSourcesDialog(UIContext *ctx, AppConfig *cfg)
{
    if (!show_tle_mgr_dialog) return;

    ImGui::SetNextWindowSize(ImVec2(550, 450), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Data Sources", &show_tle_mgr_dialog))
    {
        if (ImGui::CollapsingHeader("Celestrak Sources", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Columns(3, "celestrak_cols");
            ImGui::Separator();
            ImGui::Text("Source"); ImGui::NextColumn();
            ImGui::Text("Format"); ImGui::NextColumn();
            ImGui::Text("Fetch");  ImGui::NextColumn();
            ImGui::Separator();

            for (int i = 0; i < NUM_CELESTRAK_SOURCES; i++)
            {
                ImGui::PushID(i);
                ImGui::Checkbox(CELESTRAK_SOURCES[i].name, &celestrak_selected[i]);
                ImGui::NextColumn();

                const char* formats[] = {"TLE", "JSON", "CSV"};
                ImGui::Combo("##fmt", &celestrak_format_idx[i], formats, 3);
                ImGui::NextColumn();

                if (ImGui::SmallButton("Fetch"))
                {
                    OrbitalDataFormat fmt = FORMAT_TLE;
                    if (celestrak_format_idx[i] == 1) fmt = FORMAT_OMM_JSON;
                    else if (celestrak_format_idx[i] == 2) fmt = FORMAT_OMM_CSV;
                    FetchResult result = FetchFromSource(&CELESTRAK_SOURCES[i], fmt);
                    if (result.success)
                    {
                        // Parse TLE data
                        int before = sat_count;
                        if (fmt == FORMAT_TLE)
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
                                strncpy(meta.source_name, CELESTRAK_SOURCES[i].name, sizeof(meta.source_name) - 1);
                                meta.format = fmt;
                                meta.fetch_time = time(NULL);
                                add_satellite_from_tle(l0, l1, l2, &meta);
                            }
                        }
                        if (sat_count > before)
                            SaveOrbitalData("data.json", satellites, sat_count);
                        FreeFetchResult(&result);
                    }
                }
                ImGui::NextColumn();
                ImGui::PopID();
            }
            ImGui::Columns(1);
        }

        if (ImGui::CollapsingHeader("Retlector Sources"))
        {
            ImGui::Columns(3, "retlector_cols");
            ImGui::Separator();
            ImGui::Text("Source"); ImGui::NextColumn();
            ImGui::Text("Format"); ImGui::NextColumn();
            ImGui::Text("Fetch");  ImGui::NextColumn();
            ImGui::Separator();

            for (int i = 0; i < NUM_RETLECTOR_SOURCES; i++)
            {
                ImGui::PushID(i + 100);
                ImGui::Checkbox(RETLECTOR_SOURCES[i].name, &retlector_selected[i]);
                ImGui::NextColumn();

                const char* formats[] = {"TLE", "JSON", "CSV"};
                ImGui::Combo("##fmt", &retlector_format_idx[i], formats, 3);
                ImGui::NextColumn();

                if (ImGui::SmallButton("Fetch"))
                {
                    OrbitalDataFormat fmt = FORMAT_TLE;
                    if (retlector_format_idx[i] == 1) fmt = FORMAT_OMM_JSON;
                    else if (retlector_format_idx[i] == 2) fmt = FORMAT_OMM_CSV;
                    FetchResult result = FetchFromSource(&RETLECTOR_SOURCES[i], fmt);
                    if (result.success)
                    {
                        int before = sat_count;
                        if (fmt == FORMAT_TLE)
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
                                strncpy(meta.source_name, RETLECTOR_SOURCES[i].name, sizeof(meta.source_name) - 1);
                                meta.format = fmt;
                                meta.fetch_time = time(NULL);
                                add_satellite_from_tle(l0, l1, l2, &meta);
                            }
                        }
                        if (sat_count > before)
                            SaveOrbitalData("data.json", satellites, sat_count);
                        FreeFetchResult(&result);
                    }
                }
                ImGui::NextColumn();
                ImGui::PopID();
            }
            ImGui::Columns(1);
        }

        if (ImGui::CollapsingHeader("Custom Data Sources"))
        {
            for (int i = 0; i < cfg->custom_data_source_count; i++)
            {
                ImGui::Text("%s", cfg->custom_data_sources[i].name);
                ImGui::SameLine();
                if (ImGui::SmallButton("X"))
                {
                    for (int j = i; j < cfg->custom_data_source_count - 1; j++)
                        cfg->custom_data_sources[j] = cfg->custom_data_sources[j + 1];
                    cfg->custom_data_source_count--;
                }
            }

            static char new_name[64] = "", new_url[256] = "";
            static int new_fmt = 0;
            ImGui::InputText("Name", new_name, sizeof(new_name));
            ImGui::InputText("URL", new_url, sizeof(new_url));
            const char* formats[] = {"TLE", "JSON", "CSV"};
            ImGui::Combo("Format", &new_fmt, formats, 3);
            if (ImGui::Button("Add Source") && cfg->custom_data_source_count < MAX_CUSTOM_DATA_SOURCES)
            {
                strncpy(cfg->custom_data_sources[cfg->custom_data_source_count].name, new_name, 63);
                strncpy(cfg->custom_data_sources[cfg->custom_data_source_count].url, new_url, 255);
                cfg->custom_data_sources[cfg->custom_data_source_count].preferred_format =
                    (new_fmt == 1) ? FORMAT_OMM_JSON : (new_fmt == 2) ? FORMAT_OMM_CSV : FORMAT_TLE;
                cfg->custom_data_source_count++;
                new_name[0] = '\0';
                new_url[0] = '\0';
            }
        }

        if (ImGui::CollapsingHeader("Manual Entry"))
        {
            static char entry_buf[512] = "";
            ImGui::InputTextMultiline("##entry", entry_buf, sizeof(entry_buf), ImVec2(0, 80));
            if (ImGui::Button("Add Entry") && strlen(entry_buf) > 0)
            {
                if (cfg->manual_entry_count < MAX_MANUAL_ENTRIES)
                {
                    strncpy(cfg->manual_entries[cfg->manual_entry_count], entry_buf, 511);
                    cfg->manual_entry_count++;
                    entry_buf[0] = '\0';
                }
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Pull All Selected"))
        {
            // Trigger background pull - will be handled by the existing pull system
            // For now, fetch each selected source synchronously
            for (int i = 0; i < NUM_CELESTRAK_SOURCES; i++)
            {
                if (!celestrak_selected[i]) continue;
                OrbitalDataFormat fmt = FORMAT_TLE;
                if (celestrak_format_idx[i] == 1) fmt = FORMAT_OMM_JSON;
                else if (celestrak_format_idx[i] == 2) fmt = FORMAT_OMM_CSV;
                FetchResult result = FetchFromSource(&CELESTRAK_SOURCES[i], fmt);
                if (result.success)
                {
                    int before = sat_count;
                    if (fmt == FORMAT_TLE)
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
                            strncpy(meta.source_name, CELESTRAK_SOURCES[i].name, sizeof(meta.source_name) - 1);
                            meta.format = fmt;
                            meta.fetch_time = time(NULL);
                            add_satellite_from_tle(l0, l1, l2, &meta);
                        }
                    }
                    FreeFetchResult(&result);
                }
            }
            SaveOrbitalData("data.json", satellites, sat_count);
        }
    }
    ImGui::End();
}

/* ── Passes Dialog ───────────────────────────────────────────────────────── */

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

/* ── Polar Plot Dialog ───────────────────────────────────────────────────── */

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

/* ── Doppler Analysis Dialog ─────────────────────────────────────────────── */

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

/* ── Rotator Control Dialog ──────────────────────────────────────────────── */

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

/* ── TLE Warning Dialog ──────────────────────────────────────────────────── */

static void DrawDataWarning(UIContext *ctx, AppConfig *cfg)
{
    if (!show_tle_warning) return;

    ImGui::OpenPopup("Orbital Data Warning");
    if (ImGui::BeginPopupModal("Orbital Data Warning", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Your orbital data is old.");
        ImGui::Text("Would you like to update it now?");

        if (ImGui::Button("Update All", ImVec2(120, 0)))
        {
            show_tle_warning = false;
            show_tle_mgr_dialog = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Manage", ImVec2(120, 0)))
        {
            show_tle_warning = false;
            show_tle_mgr_dialog = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Ignore", ImVec2(120, 0)))
        {
            show_tle_warning = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

/* ── First Run Dialog ────────────────────────────────────────────────────── */

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

/* ── Update Check Notification ───────────────────────────────────────────── */

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

/* ── Exit Dialog ─────────────────────────────────────────────────────────── */

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

/* ── Log Window ──────────────────────────────────────────────────────────── */

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
        /* Toolbar row inside the log window */
        if (ImGui::Button("Clear"))
        {
            LogClear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &log_auto_scroll);
        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();

        /* Level filter dropdown */
        static int log_level_filter = 0;
        ImGui::SetNextItemWidth(100.0f);
        ImGui::Combo("##filter", &log_level_filter, "ALL\0INFO+\0WARN+\0ERROR\0");
        LogLevel min_level = LogLevelFilterMinLevel(log_level_filter);

        ImGui::Separator();

        /* Scrollable log area */
        ImGui::BeginChild("LogEntries", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar);

        /* Get head index before locking to avoid deadlock with LogLock */
        int head = LogGetHeadIndex();

        int count;
        const LogEntry *entries = LogLock(&count);

        /* The ring buffer stores entries in chronological order starting from
         * the oldest at (head - count) mod capacity, wrapping around. */
        int head_for_read = (head - count + LOG_RING_CAPACITY) % LOG_RING_CAPACITY;

        for (int i = 0; i < count; i++)
        {
            int idx = (head_for_read + i) % LOG_RING_CAPACITY;
            const LogEntry *e = &entries[idx];

            /* Skip entries below the selected filter level */
            if (e->level < min_level)
                continue;

            /* Choose colour based on level */
            ImVec4 color;
            switch (e->level)
            {
                case LOG_LEVEL_DEBUG: color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f); break; /* grey */
                case LOG_LEVEL_INFO:  color = ImVec4(0.8f, 0.9f, 1.0f, 1.0f); break; /* light blue */
                case LOG_LEVEL_WARN:  color = ImVec4(1.0f, 0.9f, 0.4f, 1.0f); break; /* yellow */
                case LOG_LEVEL_ERROR: color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); break; /* red */
                default:             color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            }

            /* Build a single selectable line: "HH:MM:SS message" */
            char line_buf[576];
            snprintf(line_buf, sizeof(line_buf), "%s %s", e->timestamp, e->message);

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::Selectable(line_buf);
            ImGui::PopStyleColor();
        }

        /* Auto-scroll to bottom */
        if (log_auto_scroll && count > 0)
        {
            ImGui::SetScrollHereY(1.0f);
        }

        LogUnlock();
        ImGui::EndChild();
    }
    ImGui::End();
}

/* ── Main DrawGUI ────────────────────────────────────────────────────────── */

void DrawGUI(UIContext *ctx, AppConfig *cfg, Font customFont)
{
    /* One-time ImGui initialization */
    static bool imgui_inited = false;
    if (!imgui_inited) {
        rlImGuiSetup(true);

        /* Build font atlas and upload to GPU */
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Build();
        unsigned char *pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        Image img = { pixels, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        Texture tex = LoadTextureFromImage(img);
        io.Fonts->SetTexID((ImTextureID)(intptr_t)tex.id);

        imgui_inited = true;
    }

    /* Begin rlImGui frame */
    rlImGuiBegin();

    /* Draw toolbar */
    DrawToolbar(ctx, cfg);

    /* Draw dialogs */
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

    /* End rlImGui frame */
    rlImGuiEnd();
}

/* ── Required stubs (to match ui.h declarations) ─────────────────────────── */

void SaveSatSelection(void) {}
void LoadSatSelection(void) {}
bool IsUITyping(void) { return ImGui::GetCurrentContext() ? ImGui::IsAnyItemActive() : false; }
void ToggleTLEWarning(void) { show_tle_warning = !show_tle_warning; }
bool IsMouseOverUI(AppConfig *cfg) { (void)cfg; return ImGui::GetCurrentContext() ? (ImGui::IsWindowHovered(ImGuiFocusedFlags_AnyWindow) || ImGui::IsAnyItemHovered()) : false; }

/* simple earth occlusion test using dot product */
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