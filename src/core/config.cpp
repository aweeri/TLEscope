#include "config.h"
#include "types.h"
#include "theme.h"
#include "location.h"
#include "util/log.h"
#include "ui/tools/tools_settings.h"
#include "data/curl_diagnostics.h"

#include <nlohmann/json.hpp>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_str(char *dst, size_t dst_size, const std::string &src)
{
    snprintf(dst, dst_size, "%s", src.c_str());
}

// read the json file and grab our settings
void LoadAppConfig(const char *filename, AppConfig *config)
{
    // default theme configuration
    strcpy(config->theme, "default");
    config->show_markers = true;      // default
    config->show_statistics = false;  // default
    config->highlight_sunlit = false; // default
    config->show_slant_range = false; // default
    config->show_scattering = false;  // default
    config->show_skybox = true;       // default
    config->show_ground_coverage = true; // default
    config->show_apsides = true;      // default
    config->show_earth_texture = true;  // default
    config->show_latlon_grid = false;   // default
    config->show_country_borders = false; // default
    config->show_coast_lines = true;   // default
    config->map_center_lon = 0.0f;     // default
    config->show_first_run_dialog = false; // default
    config->hint_vsync = true;       // default
    config->use_local_time = true;   // default: display in system local timezone
    config->night_mode = false;      // default: full-screen red post-process off
    config->first_day_of_week = 1;   // default: Monday first in the date-picker calendar
    config->custom_data_source_count = 0;
    config->retlector_group_count = 0;
    config->retlector_groups_fetched = false;
    config->custom_entry_count = 0;
    config->data_stale_threshold_seconds = STALE_THRESHOLD_DEFAULT;
    config->network_timeout_seconds = 45;
    config->active_sat_count = 0;
    config->has_saved_selection = false;
    config->tool_settings.count = 0;  // tool-owned settings start empty

    // default rotator settings (mirror the static defaults in rotator.cpp)
    {
        RotatorSettings *R = &config->rotator_settings;
        strcpy(R->host, "127.0.0.1");
        strcpy(R->port, "4533");
        strcpy(R->get_fmt, "p");
        strcpy(R->set_fmt, "P %.1f %.1f");
        R->custom_cmd[0] = '\0';
        strcpy(R->park_az, "180.0");
        strcpy(R->park_el, "0.0");
        strcpy(R->lead_time, "30");
        R->auto_steer = true;
        R->steer_mode = 0; // ROTATOR_STEER_POLAR
    }

    // default UI layout (first-run state)
    {
        UILayoutPersist *L = &config->ui_layout;
        L->left_sidebar_width = 300.0f;
        L->right_sidebar_width = 300.0f;
        L->left_sidebar_visible = true;
        L->right_sidebar_visible = true;
        L->left_sidebar_hidden = false;
        L->right_sidebar_hidden = false;
        L->left_restore_width = 300.0f;
        L->right_restore_width = 300.0f;

        // left sidebar: core functions
        int left_defaults[MAX_PANELS] = {0, 1, 2, 3, 4, -1, -1, -1, -1, -1, -1}; // SAT_MGR, DATA_SOURCES, LAYERS, SCOPE, ROTATOR
        bool left_open_defaults[MAX_PANELS] = {true, true, true, false, false, false, false, false, false, false, false};
        for (int i = 0; i < MAX_PANELS; i++)
        {
            L->left_panel_order[i] = left_defaults[i];
            L->left_panel_open[i] = left_open_defaults[i];
        }

        // right sidebar: inspector + scientific tools
        int right_defaults[MAX_PANELS] = {5, 6, 7, 8, 9, 10, -1, -1, -1, -1, -1}; // SAT_INFO, PASSES, POLAR_PLOT, DOPPLER, LOG, TRXDB
        bool right_open_defaults[MAX_PANELS] = {true, false, false, false, false, false, false, false, false, false, false};
        for (int i = 0; i < MAX_PANELS; i++)
        {
            L->right_panel_order[i] = right_defaults[i];
            L->right_panel_open[i] = right_open_defaults[i];
        }

        // all panels disabled by default (Tools dropdown); only the
        // essential core/inspector tools are turned on for first run.
        // New tools added to the enum stay off automatically.
        for (int i = 0; i < MAX_PANELS; i++)
            L->panel_enabled[i] = false;
        L->panel_enabled[PANEL_SAT_MGR]      = true;
        L->panel_enabled[PANEL_DATA_SOURCES] = true;
        L->panel_enabled[PANEL_LAYERS]       = true;
        L->panel_enabled[PANEL_SAT_INFO]     = true;
        L->panel_enabled[PANEL_PASSES]       = true;
        L->panel_enabled[PANEL_POLAR_PLOT]   = true;
    }

    if (FileExists(filename))
    {
        LOG_INFO("Loading config from %s", filename);
        char *text = LoadFileText(filename);
        if (text)
        {
            nlohmann::json root = nlohmann::json::object();
            try
            {
                root = nlohmann::json::parse(text);
            }
            catch (const std::exception &)
            {
                UnloadFileText(text);
                text = NULL;
            }

            if (text)
            {
                if (root.is_object())
                {
                    auto get_bool = [&root](const char *key, bool def) {
                        auto it = root.find(key);
                        if (it != root.end() && it->is_boolean())
                            return it->get<bool>();
                        return def;
                    };
                    auto get_int = [&root](const char *key, int def) {
                        auto it = root.find(key);
                        if (it != root.end() && it->is_number_integer())
                            return it->get<int>();
                        return def;
                    };
                    auto get_float = [&root](const char *key, float def) {
                        auto it = root.find(key);
                        if (it != root.end() && it->is_number())
                            return it->get<float>();
                        return def;
                    };
                    auto get_str = [&root](const char *key, const char *def, char *dst, size_t dst_size) {
                        auto it = root.find(key);
                        if (it != root.end() && it->is_string())
                            copy_str(dst, dst_size, it->get<std::string>());
                        else if (def)
                            copy_str(dst, dst_size, def);
                    };

                    get_str("theme", "default", config->theme, sizeof(config->theme));
                    config->window_width = get_int("window_width", config->window_width);
                    config->window_height = get_int("window_height", config->window_height);
                    config->target_fps = get_int("target_fps", config->target_fps);
                    config->ui_scale = get_float("ui_scale", config->ui_scale);
                    config->earth_rotation_offset = get_float("earth_rotation_offset", config->earth_rotation_offset);
                    config->map_center_lon = get_float("map_center_lon", config->map_center_lon);
                    config->orbits_to_draw = get_float("orbits_to_draw", config->orbits_to_draw);
                    config->data_stale_threshold_seconds = get_int("data_stale_threshold_seconds", config->data_stale_threshold_seconds);
                    config->network_timeout_seconds = get_int("network_timeout_seconds", config->network_timeout_seconds);
                    if (config->network_timeout_seconds < 15) config->network_timeout_seconds = 15;
                    if (config->network_timeout_seconds > 300) config->network_timeout_seconds = 300;
                    config->first_day_of_week = get_int("first_day_of_week", config->first_day_of_week);

                    config->show_clouds = get_bool("show_clouds", config->show_clouds);
                    config->show_night_lights = get_bool("show_night_lights", config->show_night_lights);
                    config->show_markers = get_bool("show_markers", config->show_markers);
                    config->show_statistics = get_bool("show_statistics", config->show_statistics);
                    config->highlight_sunlit = get_bool("highlight_sunlit", config->highlight_sunlit);
                    config->show_slant_range = get_bool("show_slant_range", config->show_slant_range);
                    config->show_skybox = get_bool("show_skybox", config->show_skybox);
                    config->show_ground_coverage = get_bool("show_ground_coverage", config->show_ground_coverage);
                    config->show_apsides = get_bool("show_apsides", config->show_apsides);
                    config->show_earth_texture = get_bool("show_earth_texture", config->show_earth_texture);
                    config->show_latlon_grid = get_bool("show_latlon_grid", config->show_latlon_grid);
                    config->show_country_borders = get_bool("show_country_borders", config->show_country_borders);
                    config->show_coast_lines = get_bool("show_coast_lines", config->show_coast_lines);
                    config->show_scattering = get_bool("show_scattering", config->show_scattering);
                    config->hint_vsync = get_bool("hint_vsync", config->hint_vsync);
                    config->show_first_run_dialog = get_bool("show_first_run_dialog", config->show_first_run_dialog);
                    config->use_local_time = get_bool("use_local_time", config->use_local_time);
                    config->night_mode = get_bool("night_mode", config->night_mode);

                    // load manual orbital data entries
                    auto me = root.find("manual_entries");
                    if (me != root.end() && me->is_array())
                    {
                        for (size_t i = 0; i < me->size() && config->manual_entry_count < MAX_MANUAL_ENTRIES; i++)
                        {
                            if (me->at(i).is_string())
                            {
                                std::string v = me->at(i).get<std::string>();
                                int len = (int)v.size();
                                if (len >= 512) len = 511;
                                memcpy(config->manual_entries[config->manual_entry_count], v.c_str(), (size_t)len);
                                config->manual_entries[config->manual_entry_count][len] = '\0';
                                config->manual_entry_count++;
                            }
                        }
                    }

                    // load custom data sources
                    auto cds = root.find("custom_data_sources");
                    if (cds != root.end() && cds->is_array())
                    {
                        for (size_t i = 0; i < cds->size() && config->custom_data_source_count < MAX_CUSTOM_DATA_SOURCES; i++)
                        {
                            if (!cds->at(i).is_object())
                                continue;
                            const nlohmann::json &o = cds->at(i);
                            auto nm = o.find("name");
                            auto ur = o.find("url");
                            if (nm == o.end() || ur == o.end())
                                continue;
                            CustomDataSource *s = &config->custom_data_sources[config->custom_data_source_count];
                            if (nm->is_string())
                                copy_str(s->name, sizeof(s->name), nm->get<std::string>());
                            if (ur->is_string())
                                copy_str(s->url, sizeof(s->url), ur->get<std::string>());

                            // parse preferred format
                            s->preferred_format = FORMAT_TLE;
                            auto fmt = o.find("preferred_format");
                            if (fmt != o.end() && fmt->is_string())
                            {
                                std::string fmt_buf = fmt->get<std::string>();
                                if (fmt_buf == "OMM_JSON")
                                    s->preferred_format = FORMAT_OMM_JSON;
                                else if (fmt_buf == "OMM_CSV")
                                    s->preferred_format = FORMAT_OMM_CSV;
                            }
                            s->selected = false;
                            config->custom_data_source_count++;
                        }
                    }

                    // load retlector groups (cached from API)
                    auto rg = root.find("retlector_groups");
                    if (rg != root.end() && rg->is_array())
                    {
                        for (size_t i = 0; i < rg->size() && config->retlector_group_count < MAX_RETLECTOR_GROUPS; i++)
                        {
                            if (!rg->at(i).is_object())
                                continue;
                            const nlohmann::json &o = rg->at(i);
                            RetlectorGroup *g = &config->retlector_groups[config->retlector_group_count];
                            memset(g, 0, sizeof(RetlectorGroup));
                            auto nm = o.find("name");
                            if (nm != o.end() && nm->is_string())
                                copy_str(g->name, sizeof(g->name), nm->get<std::string>());
                            auto csv = o.find("csv_endpoint");
                            if (csv != o.end() && csv->is_string())
                                copy_str(g->csv_endpoint, sizeof(g->csv_endpoint), csv->get<std::string>());
                            auto sel = o.find("selected");
                            if (sel != o.end() && sel->is_boolean())
                                g->selected = sel->get<bool>();
                            config->retlector_group_count++;
                        }
                        config->retlector_groups_fetched = (config->retlector_group_count > 0);
                    }

                    // load custom entries (pasted orbital data)
                    auto ce = root.find("custom_entries");
                    if (ce != root.end() && ce->is_array())
                    {
                        for (size_t i = 0; i < ce->size() && config->custom_entry_count < MAX_CUSTOM_ENTRIES; i++)
                        {
                            if (!ce->at(i).is_object())
                                continue;
                            const nlohmann::json &o = ce->at(i);
                            auto data = o.find("data");
                            if (data == o.end())
                                continue;
                            CustomEntry *e = &config->custom_entries[config->custom_entry_count];
                            memset(e, 0, sizeof(CustomEntry));
                            if (data->is_string())
                                copy_str(e->data, sizeof(e->data), data->get<std::string>());
                            auto fmt = o.find("detected_format");
                            if (fmt != o.end() && fmt->is_number_integer())
                                e->detected_format = (OrbitalDataFormat)fmt->get<int>();
                            auto sel = o.find("selected");
                            if (sel != o.end() && sel->is_boolean())
                                e->selected = sel->get<bool>();
                            config->custom_entry_count++;
                        }
                    }

                    // load unified locations (markers + home merged into one list)
                    location_count = 0;
                    auto locs = root.find("locations");
                    if (locs != root.end() && locs->is_array())
                    {
                        for (size_t i = 0; i < locs->size() && location_count < MAX_LOCATIONS; i++)
                        {
                            if (!locs->at(i).is_object())
                                continue;
                            const nlohmann::json &o = locs->at(i);
                            auto nm = o.find("name");
                            auto la = o.find("lat");
                            auto lo = o.find("lon");
                            if (nm == o.end() || la == o.end() || lo == o.end())
                                continue;
                            Location *loc = &locations[location_count];
                            memset(loc, 0, sizeof(Location));
                            if (nm->is_string())
                                copy_str(loc->name, sizeof(loc->name), nm->get<std::string>());
                            if (la->is_number())
                                loc->lat = la->get<float>();
                            if (lo->is_number())
                                loc->lon = lo->get<float>();
                            loc->alt = 0.0f;
                            auto alt = o.find("alt");
                            if (alt != o.end() && alt->is_number())
                                loc->alt = alt->get<float>();
                            loc->is_home = false;
                            auto home = o.find("is_home");
                            if (home != o.end() && home->is_boolean())
                                loc->is_home = home->get<bool>();
                            location_count++;
                        }
                    }

                    // migrate legacy home_location + markers into the unified list
                    if (location_count == 0)
                    {
                        auto hl = root.find("home_location");
                        if (hl != root.end() && hl->is_object())
                        {
                            const nlohmann::json &o = *hl;
                            char name[64] = "Home";
                            float lat = 0.0f, lon = 0.0f, alt = 0.0f;
                            auto nm = o.find("name");
                            if (nm != o.end() && nm->is_string())
                                copy_str(name, sizeof(name), nm->get<std::string>());
                            auto la = o.find("lat");
                            if (la != o.end() && la->is_number())
                                lat = la->get<float>();
                            auto lo = o.find("lon");
                            if (lo != o.end() && lo->is_number())
                                lon = lo->get<float>();
                            auto at = o.find("alt");
                            if (at != o.end() && at->is_number())
                                alt = at->get<float>();

                            int idx = AddLocation(name, lat, lon, alt);
                            if (idx >= 0)
                                SetHomeLocation(idx);
                        }

                        // migrate legacy markers (non-home) into the list
                        auto ms = root.find("markers");
                        if (ms != root.end() && ms->is_array())
                        {
                            for (size_t i = 0; i < ms->size(); i++)
                            {
                                if (!ms->at(i).is_object())
                                    continue;
                                const nlohmann::json &o = ms->at(i);
                                auto nm = o.find("name");
                                auto la = o.find("lat");
                                auto lo = o.find("lon");
                                if (nm == o.end() || la == o.end() || lo == o.end())
                                    continue;
                                char name[64] = "";
                                float lat = 0.0f, lon = 0.0f, alt = 0.0f;
                                if (nm->is_string())
                                    copy_str(name, sizeof(name), nm->get<std::string>());
                                if (la->is_number())
                                    lat = la->get<float>();
                                if (lo->is_number())
                                    lon = lo->get<float>();
                                auto at = o.find("alt");
                                if (at != o.end() && at->is_number())
                                    alt = at->get<float>();
                                AddLocation(name, lat, lon, alt);
                            }
                        }
                    }

                    /* Enforce the documented invariant: when at least one
                     * location exists, exactly one of them is the home location.
                     * Older/malformed settings may contain none (or several). */
                    if (location_count == 0)
                    {
                        int idx = AddLocation("Home", 0.0f, 0.0f, 0.0f);
                        if (idx >= 0)
                            SetHomeLocation(idx);
                        LOG_WARN("No saved locations found; created default Home location");
                    }
                    else
                    {
                        int home_idx = GetHomeLocationIndex();
                        if (home_idx < 0)
                        {
                            SetHomeLocation(0);
                            LOG_WARN("No saved home location found; promoted '%s'",
                                     locations[0].name);
                        }
                        else
                        {
                            /* SetHomeLocation also clears duplicate home flags. */
                            SetHomeLocation(home_idx);
                        }
                    }

                    // load UI layout (sidebar geometry + panel arrangement)
                    {
                        UILayoutPersist *L = &config->ui_layout;
                        auto ul = root.find("ui_layout");
                        if (ul != root.end() && ul->is_object())
                        {
                            auto get_val = [&ul](const char *key, float def) -> float {
                                auto it = ul->find(key);
                                if (it != ul->end() && it->is_number())
                                    return it->get<float>();
                                return def;
                            };
                            auto get_b = [&ul](const char *key, bool def) -> bool {
                                auto it = ul->find(key);
                                if (it != ul->end() && it->is_boolean())
                                    return it->get<bool>();
                                return def;
                            };

                            L->left_sidebar_width = get_val("left_sidebar_width", L->left_sidebar_width);
                            L->right_sidebar_width = get_val("right_sidebar_width", L->right_sidebar_width);
                            L->left_sidebar_visible = get_b("left_sidebar_visible", L->left_sidebar_visible);
                            L->right_sidebar_visible = get_b("right_sidebar_visible", L->right_sidebar_visible);
                            L->left_sidebar_hidden = get_b("left_sidebar_hidden", L->left_sidebar_hidden);
                            L->right_sidebar_hidden = get_b("right_sidebar_hidden", L->right_sidebar_hidden);
                            L->left_restore_width = get_val("left_restore_width", L->left_restore_width);
                            L->right_restore_width = get_val("right_restore_width", L->right_restore_width);

                            auto read_int_array = [&ul](const char *key, int *dst) {
                                auto it = ul->find(key);
                                if (it == ul->end() || !it->is_array())
                                    return;
                                int n = (int)it->size();
                                if (n > MAX_PANELS) n = MAX_PANELS;
                                for (int i = 0; i < n; i++)
                                    if (it->at(i).is_number_integer())
                                        dst[i] = it->at(i).get<int>();
                            };
                            auto read_bool_array = [&ul](const char *key, bool *dst) {
                                auto it = ul->find(key);
                                if (it == ul->end() || !it->is_array())
                                    return;
                                int n = (int)it->size();
                                if (n > MAX_PANELS) n = MAX_PANELS;
                                for (int i = 0; i < n; i++)
                                    if (it->at(i).is_boolean())
                                        dst[i] = it->at(i).get<bool>();
                            };

                            read_int_array("left_panel_order", L->left_panel_order);
                            read_int_array("right_panel_order", L->right_panel_order);
                            read_bool_array("left_panel_open", L->left_panel_open);
                            read_bool_array("right_panel_open", L->right_panel_open);
                            read_bool_array("panel_enabled", L->panel_enabled);
                        }
                    }

                    // load tool-owned settings (generic key-value store)
                    {
                        auto ts = root.find("tool_settings");
                        if (ts != root.end() && ts->is_array())
                        {
                            config->tool_settings.count = 0;
                            for (size_t i = 0; i < ts->size() && config->tool_settings.count < MAX_TOOL_SETTINGS; i++)
                            {
                                if (!ts->at(i).is_object())
                                    continue;
                                const nlohmann::json &o = ts->at(i);
                                ToolSetting *s = &config->tool_settings.entries[config->tool_settings.count];
                                auto k = o.find("key");
                                auto v = o.find("value");
                                if (k == o.end() || v == o.end())
                                    continue;
                                if (k->is_string())
                                    copy_str(s->key, sizeof(s->key), k->get<std::string>());
                                if (v->is_string())
                                    copy_str(s->value, sizeof(s->value), v->get<std::string>());
                                config->tool_settings.count++;
                            }
                        }
                    }

                    // load rotator settings
                    {
                        RotatorSettings *R = &config->rotator_settings;
                        auto rot = root.find("rotator_settings");
                        if (rot != root.end() && rot->is_object())
                        {
                            auto get_s = [rot](const char *key, char *dst, size_t size) {
                                auto it = rot->find(key);
                                if (it != rot->end() && it->is_string())
                                    copy_str(dst, size, it->get<std::string>());
                            };
                            get_s("host", R->host, sizeof(R->host));
                            get_s("port", R->port, sizeof(R->port));
                            get_s("get_fmt", R->get_fmt, sizeof(R->get_fmt));
                            get_s("set_fmt", R->set_fmt, sizeof(R->set_fmt));
                            get_s("custom_cmd", R->custom_cmd, sizeof(R->custom_cmd));
                            get_s("park_az", R->park_az, sizeof(R->park_az));
                            get_s("park_el", R->park_el, sizeof(R->park_el));
                            get_s("lead_time", R->lead_time, sizeof(R->lead_time));
                            auto a = rot->find("auto_steer");
                            if (a != rot->end() && a->is_boolean())
                                R->auto_steer = a->get<bool>();
                            auto sm = rot->find("steer_mode");
                            if (sm != rot->end() && sm->is_number_integer())
                                R->steer_mode = sm->get<int>();
                        }
                    }

                    // load active satellite selection (NORAD ids)
                    config->active_sat_count = 0;
                    {
                        auto as = root.find("active_sat_ids");
                        if (as != root.end() && as->is_array())
                        {
                            config->has_saved_selection = true;
                            for (size_t i = 0; i < as->size() && config->active_sat_count < MAX_SATELLITES; i++)
                            {
                                if (as->at(i).is_number_unsigned())
                                    config->active_sat_ids[config->active_sat_count++] = (uint32_t)as->at(i).get<uint64_t>();
                                else if (as->at(i).is_number_integer())
                                    config->active_sat_ids[config->active_sat_count++] = (uint32_t)as->at(i).get<int64_t>();
                            }
                        }
                    }
                }

                UnloadFileText(text);
                LOG_INFO("Config loaded: theme=%s, %dx%d, %d locations, %d custom sources, %d retlector groups, %d custom entries, stale_threshold=%d",
                         config->theme, config->window_width, config->window_height,
                         location_count, config->custom_data_source_count,
                         config->retlector_group_count, config->custom_entry_count,
                         config->data_stale_threshold_seconds);
            }
        }
    }
    else {
        LOG_INFO("No config file found at %s -- showing first-run dialog", filename);
        strcpy(config->theme, "default");
        config->window_width = 1920;
        config->window_height = 1080;
        config->target_fps = 120;
        config->ui_scale = 1.15;
        config->earth_rotation_offset = 0.00;
        config->map_center_lon = 0.0f;
        config->orbits_to_draw = 3.00;
        config->show_clouds = true;
        config->show_night_lights = true;
        config->show_markers = true;
        config->show_statistics = false;
        config->highlight_sunlit = false;
        config->show_slant_range = false;
        config->show_scattering = false;
        config->show_skybox = true;
        config->show_ground_coverage = true;
        config->show_apsides = true;
        config->show_earth_texture = true;
        config->show_latlon_grid = false;
        config->show_country_borders = false;
        config->show_coast_lines = true;
        config->hint_vsync = true;
        config->night_mode = false;
        // first run: a single default home location, no forced example marker
        location_count = 0;
        int home_idx = AddLocation("Home", 0.0f, 0.0f, 0.0f);
        if (home_idx >= 0)
            SetHomeLocation(home_idx);

        config->show_first_run_dialog = true;

        SaveAppConfig(filename, config);
    }

    TLEscopeSetCurlTimeoutSeconds(config->network_timeout_seconds);

    // load theme from the selected theme directory
    ThemeInitDefaults(&g_theme);
    if (!ThemeLoad(config->theme, &g_theme))
    {
        LOG_WARN("Failed to load theme '%s', using defaults", config->theme);
    }
}

void SaveAppConfig(const char *filename, AppConfig *config)
{
    nlohmann::json root = nlohmann::json::object();

    root["theme"] = config->theme;
    root["window_width"] = config->window_width;
    root["window_height"] = config->window_height;
    root["target_fps"] = config->target_fps;
    root["ui_scale"] = config->ui_scale;
    root["earth_rotation_offset"] = config->earth_rotation_offset;
    root["map_center_lon"] = config->map_center_lon;
    root["orbits_to_draw"] = config->orbits_to_draw;
    root["show_clouds"] = config->show_clouds;
    root["show_night_lights"] = config->show_night_lights;
    root["show_markers"] = config->show_markers;
    root["show_statistics"] = config->show_statistics;
    root["highlight_sunlit"] = config->highlight_sunlit;
    root["show_slant_range"] = config->show_slant_range;
    root["show_scattering"] = config->show_scattering;
    root["show_skybox"] = config->show_skybox;
    root["show_ground_coverage"] = config->show_ground_coverage;
    root["show_apsides"] = config->show_apsides;
    root["show_earth_texture"] = config->show_earth_texture;
    root["show_latlon_grid"] = config->show_latlon_grid;
    root["show_country_borders"] = config->show_country_borders;
    root["show_coast_lines"] = config->show_coast_lines;
    root["hint_vsync"] = config->hint_vsync;
    root["show_first_run_dialog"] = config->show_first_run_dialog;
    root["use_local_time"] = config->use_local_time;
    root["night_mode"] = config->night_mode;
    root["first_day_of_week"] = config->first_day_of_week;
    root["data_stale_threshold_seconds"] = config->data_stale_threshold_seconds;
    root["network_timeout_seconds"] = config->network_timeout_seconds;

    if (config->custom_data_source_count > 0)
    {
        nlohmann::json arr = nlohmann::json::array();
        for (int i = 0; i < config->custom_data_source_count; i++)
        {
            const CustomDataSource &s = config->custom_data_sources[i];
            const char *fmt = "TLE";
            if (s.preferred_format == FORMAT_OMM_JSON) fmt = "OMM_JSON";
            else if (s.preferred_format == FORMAT_OMM_CSV) fmt = "OMM_CSV";
            arr.push_back(nlohmann::json{
                {"name", s.name},
                {"url", s.url},
                {"preferred_format", fmt}});
        }
        root["custom_data_sources"] = arr;
    }

    if (config->manual_entry_count > 0)
    {
        nlohmann::json arr = nlohmann::json::array();
        for (int i = 0; i < config->manual_entry_count; i++)
            arr.push_back(config->manual_entries[i]);
        root["manual_entries"] = arr;
    }

    // save retlector groups (cached from API)
    if (config->retlector_group_count > 0)
    {
        nlohmann::json arr = nlohmann::json::array();
        for (int i = 0; i < config->retlector_group_count; i++)
        {
            const RetlectorGroup &g = config->retlector_groups[i];
            arr.push_back(nlohmann::json{
                {"name", g.name},
                {"csv_endpoint", g.csv_endpoint},
                {"selected", g.selected}});
        }
        root["retlector_groups"] = arr;
    }

    // save custom entries (pasted orbital data)
    if (config->custom_entry_count > 0)
    {
        nlohmann::json arr = nlohmann::json::array();
        for (int i = 0; i < config->custom_entry_count; i++)
        {
            const CustomEntry &e = config->custom_entries[i];
            arr.push_back(nlohmann::json{
                {"data", e.data},
                {"detected_format", (int)e.detected_format},
                {"selected", e.selected}});
        }
        root["custom_entries"] = arr;
    }

    {
        nlohmann::json arr = nlohmann::json::array();
        for (int i = 0; i < location_count; i++)
        {
            arr.push_back(nlohmann::json{
                {"name", locations[i].name},
                {"lat", locations[i].lat},
                {"lon", locations[i].lon},
                {"alt", locations[i].alt},
                {"is_home", locations[i].is_home}});
        }
        root["locations"] = arr;
    }

    // -- UI layout (sidebar geometry + panel arrangement) -----------------
    {
        const UILayoutPersist *L = &config->ui_layout;
        nlohmann::json ul = nlohmann::json::object();
        ul["left_sidebar_width"] = L->left_sidebar_width;
        ul["right_sidebar_width"] = L->right_sidebar_width;
        ul["left_sidebar_visible"] = L->left_sidebar_visible;
        ul["right_sidebar_visible"] = L->right_sidebar_visible;
        ul["left_sidebar_hidden"] = L->left_sidebar_hidden;
        ul["right_sidebar_hidden"] = L->right_sidebar_hidden;
        ul["left_restore_width"] = L->left_restore_width;
        ul["right_restore_width"] = L->right_restore_width;

        nlohmann::json left_order = nlohmann::json::array();
        nlohmann::json right_order = nlohmann::json::array();
        nlohmann::json left_open = nlohmann::json::array();
        nlohmann::json right_open = nlohmann::json::array();
        nlohmann::json panel_enabled = nlohmann::json::array();
        for (int i = 0; i < MAX_PANELS; i++)
        {
            left_order.push_back(L->left_panel_order[i]);
            right_order.push_back(L->right_panel_order[i]);
            left_open.push_back(L->left_panel_open[i]);
            right_open.push_back(L->right_panel_open[i]);
            panel_enabled.push_back(L->panel_enabled[i]);
        }
        ul["left_panel_order"] = left_order;
        ul["right_panel_order"] = right_order;
        ul["left_panel_open"] = left_open;
        ul["right_panel_open"] = right_open;
        ul["panel_enabled"] = panel_enabled;

        root["ui_layout"] = ul;
    }

    // -- rotator settings ------------------------------------------------
    {
        const RotatorSettings *R = &config->rotator_settings;
        root["rotator_settings"] = nlohmann::json{
            {"host", R->host},
            {"port", R->port},
            {"get_fmt", R->get_fmt},
            {"set_fmt", R->set_fmt},
            {"custom_cmd", R->custom_cmd},
            {"park_az", R->park_az},
            {"park_el", R->park_el},
            {"lead_time", R->lead_time},
            {"auto_steer", R->auto_steer},
            {"steer_mode", R->steer_mode}};
    }

    // -- tool-owned settings (generic key-value store) --------------------
    if (config->tool_settings.count > 0)
    {
        nlohmann::json arr = nlohmann::json::array();
        for (int i = 0; i < config->tool_settings.count; i++)
        {
            const ToolSetting &s = config->tool_settings.entries[i];
            arr.push_back(nlohmann::json{{"key", s.key}, {"value", s.value}});
        }
        root["tool_settings"] = arr;
    }

    // -- active satellite selection (NORAD ids) ---------------------------
    {
        nlohmann::json arr = nlohmann::json::array();
        for (int i = 0; i < config->active_sat_count; i++)
            arr.push_back(config->active_sat_ids[i]);
        root["active_sat_ids"] = arr;
    }

    FILE *file = fopen(filename, "w");
    if (!file)
    {
        LOG_ERROR("Failed to save config to %s", filename);
        return;
    }
    LOG_INFO("Saving config to %s", filename);
    std::string out = root.dump(4);
    fwrite(out.c_str(), 1, out.size(), file);
    fwrite("\n", 1, 1, file);
    fclose(file);
}