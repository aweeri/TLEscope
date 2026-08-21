#include "config.h"
#include "types.h"
#include "theme.h"
#include "location.h"
#include "util/log.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool ParseJsonBool(const char *text, const char *key, bool defaultValue)
{
    if (!text || !key)
        return defaultValue;

    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *ptr = strstr(text, needle);
    if (!ptr)
        return defaultValue;

    ptr = strchr(ptr, ':');
    if (!ptr)
        return defaultValue;
    ptr++;

    while (*ptr && isspace((unsigned char)*ptr))
        ptr++;

    if (strncmp(ptr, "true", 4) == 0)
        return true;
    if (strncmp(ptr, "false", 5) == 0)
        return false;

    return defaultValue;
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
    config->show_first_run_dialog = false; //default
    config->hint_vsync = true;       // default
    config->use_local_time = true;   // default: display in system local timezone
    config->custom_data_source_count = 0;
    config->retlector_group_count = 0;
    config->retlector_groups_fetched = false;
    config->custom_entry_count = 0;
    config->data_stale_threshold_seconds = STALE_THRESHOLD_DEFAULT;
    config->active_sat_count = 0;
    config->has_saved_selection = false;

    /* default rotator settings (mirror the static defaults in rotator.cpp) */
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
        R->steer_mode = 0; /* ROTATOR_STEER_POLAR */
    }

    /* default UI layout (first-run state) */
    {
        UILayoutPersist *L = &config->ui_layout;
        L->left_sidebar_width = 300.0f;
        L->right_sidebar_width = 300.0f;
        L->left_sidebar_visible = true;
        L->right_sidebar_visible = true;
        L->left_sidebar_hidden = false;
        L->right_sidebar_hidden = false;

        /* left sidebar: core functions */
        int left_defaults[MAX_LEFT_PANELS] = {0, 1, 2, 3, 4}; /* SAT_MGR, DATA_SOURCES, TIME_CTRL, SCOPE, ROTATOR */
        bool left_open_defaults[MAX_LEFT_PANELS] = {true, true, true, false, false};
        for (int i = 0; i < MAX_LEFT_PANELS; i++)
        {
            L->left_panel_order[i] = left_defaults[i];
            L->left_panel_open[i] = left_open_defaults[i];
        }

        /* right sidebar: inspector + scientific tools */
        int right_defaults[MAX_RIGHT_PANELS] = {5, 6, 7, 8, 9}; /* SAT_INFO, PASSES, POLAR_PLOT, DOPPLER, LOG */
        bool right_open_defaults[MAX_RIGHT_PANELS] = {true, false, false, false, false};
        for (int i = 0; i < MAX_RIGHT_PANELS; i++)
        {
            L->right_panel_order[i] = right_defaults[i];
            L->right_panel_open[i] = right_open_defaults[i];
        }
    }

    if (FileExists(filename))
    {
        LOG_INFO("Loading config from %s", filename);
        char *text = LoadFileText(filename);
        if (text)
        {
            char hex[32];
            char *ptr;

#define PARSE_FLOAT(key, field)                                                                                                                                                                        \
    ptr = strstr(text, "\"" key "\"");                                                                                                                                                                 \
    if (ptr)                                                                                                                                                                                           \
    {                                                                                                                                                                                                  \
        ptr = strchr(ptr, ':');                                                                                                                                                                        \
        if (ptr)                                                                                                                                                                                       \
        {                                                                                                                                                                                              \
            sscanf(ptr + 1, "%f", &config->field);                                                                                                                                                     \
        }                                                                                                                                                                                              \
    }

#define PARSE_INT(key, field)                                                                                                                                                                          \
    ptr = strstr(text, "\"" key "\"");                                                                                                                                                                 \
    if (ptr)                                                                                                                                                                                           \
    {                                                                                                                                                                                                  \
        ptr = strchr(ptr, ':');                                                                                                                                                                        \
        if (ptr)                                                                                                                                                                                       \
        {                                                                                                                                                                                              \
            sscanf(ptr + 1, "%d", &config->field);                                                                                                                                                     \
        }                                                                                                                                                                                              \
    }

            ptr = strstr(text, "\"theme\"");
            if (ptr)
            {
                ptr = strchr(ptr, ':');
                if (ptr)
                {
                    char *quote_start = strchr(ptr, '"');
                    if (quote_start)
                    {
                        sscanf(quote_start + 1, "%63[^\"]", config->theme);
                    }
                }
            }

            PARSE_INT("window_width", window_width);
            PARSE_INT("window_height", window_height);
            PARSE_INT("target_fps", target_fps);
            PARSE_FLOAT("ui_scale", ui_scale);
            PARSE_FLOAT("earth_rotation_offset", earth_rotation_offset);
            PARSE_FLOAT("orbits_to_draw", orbits_to_draw);
            PARSE_INT("data_stale_threshold_seconds", data_stale_threshold_seconds);

            config->show_clouds = ParseJsonBool(text, "show_clouds", config->show_clouds);
            config->show_night_lights = ParseJsonBool(text, "show_night_lights", config->show_night_lights);
            config->show_markers = ParseJsonBool(text, "show_markers", config->show_markers);
            config->show_statistics = ParseJsonBool(text, "show_statistics", config->show_statistics);
            config->highlight_sunlit = ParseJsonBool(text, "highlight_sunlit", config->highlight_sunlit);
            config->show_slant_range = ParseJsonBool(text, "show_slant_range", config->show_slant_range);
            config->show_skybox = ParseJsonBool(text, "show_skybox", config->show_skybox);
            config->show_ground_coverage = ParseJsonBool(text, "show_ground_coverage", config->show_ground_coverage);
            config->show_apsides = ParseJsonBool(text, "show_apsides", config->show_apsides);
            config->show_scattering = ParseJsonBool(text, "show_scattering", config->show_scattering);
            config->hint_vsync = ParseJsonBool(text, "hint_vsync", config->hint_vsync);
            config->show_first_run_dialog = ParseJsonBool(text, "show_first_run_dialog", config->show_first_run_dialog);
            config->use_local_time = ParseJsonBool(text, "use_local_time", config->use_local_time);

            // load manual orbital data entries
            char *mt_ptr = strstr(text, "\"manual_entries\"");
            if (mt_ptr)
            {
                char *array_start = strchr(mt_ptr, '[');
                char *array_end = array_start ? strchr(array_start, ']') : NULL;
                if (array_start && array_end)
                {
                    char *curr = array_start + 1;
                    while (curr < array_end && config->manual_entry_count < MAX_MANUAL_ENTRIES)
                    {
                        char *quote_start = strchr(curr, '"');
                        if (!quote_start || quote_start > array_end) break;
                        char *quote_end = strchr(quote_start + 1, '"');
                        if (!quote_end || quote_end > array_end) break;

                        int len = quote_end - (quote_start + 1);
                        if (len >= 512) len = 511;
                        strncpy(config->manual_entries[config->manual_entry_count], quote_start + 1, len);
                        config->manual_entries[config->manual_entry_count][len] = '\0';
                        config->manual_entry_count++;

                        curr = quote_end + 1;
                    }
                }
            }

            // load custom data sources
            char *cts_ptr = strstr(text, "\"custom_data_sources\"");
            if (cts_ptr)
            {
                char *block_end = strchr(cts_ptr, ']');
                if (!block_end)
                    block_end = text + strlen(text);

                while ((cts_ptr = strstr(cts_ptr, "{")) && cts_ptr < block_end)
                {
                    if (config->custom_data_source_count >= MAX_CUSTOM_DATA_SOURCES)
                        break;

                    char *obj_end = strchr(cts_ptr, '}');
                    if (!obj_end || obj_end > block_end)
                        obj_end = block_end;

                    char *name_ptr = strstr(cts_ptr, "\"name\"");
                    char *url_ptr = strstr(cts_ptr, "\"url\"");
                    char *fmt_ptr = strstr(cts_ptr, "\"preferred_format\"");

                    if (name_ptr && name_ptr < obj_end && url_ptr && url_ptr < obj_end)
                    {
                        char *colon_name = strchr(name_ptr, ':');
                        if (colon_name && colon_name < obj_end)
                        {
                            char *quote_start = strchr(colon_name, '"');
                            if (quote_start && quote_start < obj_end)
                                sscanf(quote_start + 1, "%63[^\"]", config->custom_data_sources[config->custom_data_source_count].name);
                        }

                        char *colon_url = strchr(url_ptr, ':');
                        if (colon_url && colon_url < obj_end)
                        {
                            char *quote_start = strchr(colon_url, '"');
                            if (quote_start && quote_start < obj_end)
                                sscanf(quote_start + 1, "%255[^\"]", config->custom_data_sources[config->custom_data_source_count].url);
                        }

                        // parse preferred format
                        config->custom_data_sources[config->custom_data_source_count].preferred_format = FORMAT_TLE;
                        if (fmt_ptr && fmt_ptr < obj_end)
                        {
                            char *colon_fmt = strchr(fmt_ptr, ':');
                            if (colon_fmt && colon_fmt < obj_end)
                            {
                                char *quote_start = strchr(colon_fmt, '"');
                                if (quote_start && quote_start < obj_end)
                                {
                                    char fmt_buf[32] = {0};
                                    sscanf(quote_start + 1, "%31[^\"]", fmt_buf);
                                    if (strcmp(fmt_buf, "OMM_JSON") == 0)
                                        config->custom_data_sources[config->custom_data_source_count].preferred_format = FORMAT_OMM_JSON;
                                    else if (strcmp(fmt_buf, "OMM_CSV") == 0)
                                        config->custom_data_sources[config->custom_data_source_count].preferred_format = FORMAT_OMM_CSV;
                                }
                            }
                        }

                        config->custom_data_sources[config->custom_data_source_count].selected = false;
                        config->custom_data_source_count++;
                    }
                    cts_ptr = obj_end + 1;
                }
            }

            // load retlector groups (cached from API)
            char *rg_ptr = strstr(text, "\"retlector_groups\"");
            if (rg_ptr)
            {
                char *block_end = strchr(rg_ptr, ']');
                if (!block_end) block_end = text + strlen(text);

                while ((rg_ptr = strstr(rg_ptr, "{")) && rg_ptr < block_end)
                {
                    if (config->retlector_group_count >= MAX_RETLECTOR_GROUPS) break;

                    char *obj_end = strchr(rg_ptr, '}');
                    if (!obj_end || obj_end > block_end) obj_end = block_end;

                    char *name_ptr = strstr(rg_ptr, "\"name\"");
                    char *csv_ptr = strstr(rg_ptr, "\"csv_endpoint\"");
                    char *sel_ptr = strstr(rg_ptr, "\"selected\"");

                    if (name_ptr && name_ptr < obj_end)
                    {
                        RetlectorGroup *g = &config->retlector_groups[config->retlector_group_count];
                        memset(g, 0, sizeof(RetlectorGroup));

                        char *colon = strchr(name_ptr, ':');
                        if (colon && colon < obj_end)
                        {
                            char *q = strchr(colon, '"');
                            if (q && q < obj_end)
                                sscanf(q + 1, "%63[^\"]", g->name);
                        }

                        if (csv_ptr && csv_ptr < obj_end)
                        {
                            char *colon = strchr(csv_ptr, ':');
                            if (colon && colon < obj_end)
                            {
                                char *q = strchr(colon, '"');
                                if (q && q < obj_end)
                                    sscanf(q + 1, "%255[^\"]", g->csv_endpoint);
                            }
                        }

                        if (sel_ptr && sel_ptr < obj_end)
                        {
                            char *colon = strchr(sel_ptr, ':');
                            if (colon && colon < obj_end)
                            {
                                colon++;
                                while (*colon == ' ') colon++;
                                g->selected = (strncmp(colon, "true", 4) == 0);
                            }
                        }

                        config->retlector_group_count++;
                    }
                    rg_ptr = obj_end + 1;
                }
                config->retlector_groups_fetched = (config->retlector_group_count > 0);
            }

            // load custom entries (pasted orbital data)
            char *ce_ptr = strstr(text, "\"custom_entries\"");
            if (ce_ptr)
            {
                char *block_end = strchr(ce_ptr, ']');
                if (!block_end) block_end = text + strlen(text);

                while ((ce_ptr = strstr(ce_ptr, "{")) && ce_ptr < block_end)
                {
                    if (config->custom_entry_count >= MAX_CUSTOM_ENTRIES) break;

                    char *obj_end = strchr(ce_ptr, '}');
                    if (!obj_end || obj_end > block_end) obj_end = block_end;

                    char *data_ptr = strstr(ce_ptr, "\"data\"");
                    char *fmt_ptr = strstr(ce_ptr, "\"detected_format\"");
                    char *sel_ptr = strstr(ce_ptr, "\"selected\"");

                    if (data_ptr && data_ptr < obj_end)
                    {
                        CustomEntry *e = &config->custom_entries[config->custom_entry_count];
                        memset(e, 0, sizeof(CustomEntry));

                        char *colon = strchr(data_ptr, ':');
                        if (colon && colon < obj_end)
                        {
                            char *q = strchr(colon, '"');
                            if (q && q < obj_end)
                            {
                                q++;
                                int i = 0;
                                while (*q && *q != '"' && i < 4095) e->data[i++] = *q++;
                                e->data[i] = '\0';
                            }
                        }

                        if (fmt_ptr && fmt_ptr < obj_end)
                        {
                            char *colon = strchr(fmt_ptr, ':');
                            if (colon && colon < obj_end)
                            {
                                colon++;
                                while (*colon == ' ') colon++;
                                e->detected_format = (OrbitalDataFormat)atoi(colon);
                            }
                        }

                        if (sel_ptr && sel_ptr < obj_end)
                        {
                            char *colon = strchr(sel_ptr, ':');
                            if (colon && colon < obj_end)
                            {
                                colon++;
                                while (*colon == ' ') colon++;
                                e->selected = (strncmp(colon, "true", 4) == 0);
                            }
                        }

                        config->custom_entry_count++;
                    }
                    ce_ptr = obj_end + 1;
                }
            }

            // load unified locations (markers + home merged into one list)
            location_count = 0;
            char *loc_ptr = strstr(text, "\"locations\"");
            if (loc_ptr)
            {
                char *block_end = strchr(loc_ptr, ']');
                if (!block_end)
                    block_end = text + strlen(text);

                while ((loc_ptr = strstr(loc_ptr, "{")) && loc_ptr < block_end)
                {
                    if (location_count >= MAX_LOCATIONS)
                        break;

                    char *obj_end = strchr(loc_ptr, '}');
                    if (!obj_end || obj_end > block_end)
                        obj_end = block_end;

                    char *name_ptr = strstr(loc_ptr, "\"name\"");
                    char *lat_ptr = strstr(loc_ptr, "\"lat\"");
                    char *lon_ptr = strstr(loc_ptr, "\"lon\"");
                    char *alt_ptr = strstr(loc_ptr, "\"alt\"");
                    char *home_ptr = strstr(loc_ptr, "\"is_home\"");

                    if (name_ptr && name_ptr < obj_end && lat_ptr && lat_ptr < obj_end && lon_ptr && lon_ptr < obj_end)
                    {
                        Location *loc = &locations[location_count];
                        memset(loc, 0, sizeof(Location));

                        char *colon_name = strchr(name_ptr, ':');
                        if (colon_name && colon_name < obj_end)
                        {
                            char *quote_start = strchr(colon_name, '"');
                            if (quote_start && quote_start < obj_end)
                                sscanf(quote_start + 1, "%63[^\"]", loc->name);
                        }

                        char *colon_lat = strchr(lat_ptr, ':');
                        if (colon_lat && colon_lat < obj_end)
                            sscanf(colon_lat + 1, "%f", &loc->lat);

                        char *colon_lon = strchr(lon_ptr, ':');
                        if (colon_lon && colon_lon < obj_end)
                            sscanf(colon_lon + 1, "%f", &loc->lon);

                        loc->alt = 0.0f;
                        if (alt_ptr && alt_ptr < obj_end)
                        {
                            char *colon_alt = strchr(alt_ptr, ':');
                            if (colon_alt && colon_alt < obj_end)
                                sscanf(colon_alt + 1, "%f", &loc->alt);
                        }

                        loc->is_home = false;
                        if (home_ptr && home_ptr < obj_end)
                        {
                            char *colon_home = strchr(home_ptr, ':');
                            if (colon_home && colon_home < obj_end)
                            {
                                colon_home++;
                                while (*colon_home == ' ') colon_home++;
                                loc->is_home = (strncmp(colon_home, "true", 4) == 0);
                            }
                        }

                        location_count++;
                    }
                    loc_ptr = obj_end + 1;
                }
            }

            // migrate legacy home_location + markers into the unified list
            if (location_count == 0)
            {
                char *hl_ptr = strstr(text, "\"home_location\"");
                if (hl_ptr)
                {
                    char *name_ptr = strstr(hl_ptr, "\"name\"");
                    char *lat_ptr = strstr(hl_ptr, "\"lat\"");
                    char *lon_ptr = strstr(hl_ptr, "\"lon\"");
                    char *alt_ptr = strstr(hl_ptr, "\"alt\"");
                    char *obj_end = strchr(hl_ptr, '}');

                    if (name_ptr && lat_ptr && lon_ptr && name_ptr < obj_end)
                    {
                        char name[64] = "Home";
                        float lat = 0.0f, lon = 0.0f, alt = 0.0f;

                        char *colon_name = strchr(name_ptr, ':');
                        if (colon_name)
                        {
                            char *quote_start = strchr(colon_name, '"');
                            if (quote_start)
                                sscanf(quote_start + 1, "%63[^\"]", name);
                        }
                        char *colon_lat = strchr(lat_ptr, ':');
                        if (colon_lat)
                            sscanf(colon_lat + 1, "%f", &lat);
                        char *colon_lon = strchr(lon_ptr, ':');
                        if (colon_lon)
                            sscanf(colon_lon + 1, "%f", &lon);
                        if (alt_ptr && alt_ptr < obj_end)
                        {
                            char *colon_alt = strchr(alt_ptr, ':');
                            if (colon_alt)
                                sscanf(colon_alt + 1, "%f", &alt);
                        }

                        int idx = AddLocation(name, lat, lon, alt);
                        if (idx >= 0)
                            SetHomeLocation(idx);
                    }
                }

                // migrate legacy markers (non-home) into the list
                char *m_ptr = strstr(text, "\"markers\"");
                if (m_ptr)
                {
                    char *block_end = strchr(m_ptr, ']');
                    if (!block_end)
                        block_end = text + strlen(text);

                    while ((m_ptr = strstr(m_ptr, "{")) && m_ptr < block_end)
                    {
                        char *obj_end = strchr(m_ptr, '}');
                        if (!obj_end || obj_end > block_end)
                            obj_end = block_end;

                        char *name_ptr = strstr(m_ptr, "\"name\"");
                        char *lat_ptr = strstr(m_ptr, "\"lat\"");
                        char *lon_ptr = strstr(m_ptr, "\"lon\"");
                        char *alt_ptr = strstr(m_ptr, "\"alt\"");

                        if (name_ptr && name_ptr < obj_end && lat_ptr && lat_ptr < obj_end && lon_ptr && lon_ptr < obj_end)
                        {
                            char name[64] = "";
                            float lat = 0.0f, lon = 0.0f, alt = 0.0f;

                            char *colon_name = strchr(name_ptr, ':');
                            if (colon_name && colon_name < obj_end)
                            {
                                char *quote_start = strchr(colon_name, '"');
                                if (quote_start && quote_start < obj_end)
                                    sscanf(quote_start + 1, "%63[^\"]", name);
                            }
                            char *colon_lat = strchr(lat_ptr, ':');
                            if (colon_lat && colon_lat < obj_end)
                                sscanf(colon_lat + 1, "%f", &lat);
                            char *colon_lon = strchr(lon_ptr, ':');
                            if (colon_lon && colon_lon < obj_end)
                                sscanf(colon_lon + 1, "%f", &lon);
                            if (alt_ptr && alt_ptr < obj_end)
                            {
                                char *colon_alt = strchr(alt_ptr, ':');
                                if (colon_alt && colon_alt < obj_end)
                                    sscanf(colon_alt + 1, "%f", &alt);
                            }

                            AddLocation(name, lat, lon, alt);
                        }
                        m_ptr = obj_end + 1;
                    }
                }
            }

            // load UI layout (sidebar geometry + panel arrangement)
            {
                UILayoutPersist *L = &config->ui_layout;
                char *ul_ptr = strstr(text, "\"ui_layout\"");
                if (ul_ptr)
                {
                    char *block_end = strchr(ul_ptr, '}');
                    if (!block_end) block_end = text + strlen(text);

                    // sidebar widths
                    char *w = strstr(ul_ptr, "\"left_sidebar_width\"");
                    if (w && w < block_end) { char *c = strchr(w, ':'); if (c) sscanf(c + 1, "%f", &L->left_sidebar_width); }
                    w = strstr(ul_ptr, "\"right_sidebar_width\"");
                    if (w && w < block_end) { char *c = strchr(w, ':'); if (c) sscanf(c + 1, "%f", &L->right_sidebar_width); }

                    // visibility
                    L->left_sidebar_visible = ParseJsonBool(ul_ptr, "left_sidebar_visible", L->left_sidebar_visible);
                    L->right_sidebar_visible = ParseJsonBool(ul_ptr, "right_sidebar_visible", L->right_sidebar_visible);
                    L->left_sidebar_hidden = ParseJsonBool(ul_ptr, "left_sidebar_hidden", L->left_sidebar_hidden);
                    L->right_sidebar_hidden = ParseJsonBool(ul_ptr, "right_sidebar_hidden", L->right_sidebar_hidden);

                    // panel order arrays
                    char *po = strstr(ul_ptr, "\"left_panel_order\"");
                    if (po && po < block_end)
                    {
                        char *arr = strchr(po, '[');
                        if (arr)
                        {
                            char *cur = arr + 1;
                            for (int i = 0; i < MAX_LEFT_PANELS && cur && *cur != ']'; i++)
                            {
                                while (*cur && (*cur == ' ' || *cur == ',')) cur++;
                                if (*cur == ']' || *cur == '\0') break;
                                L->left_panel_order[i] = atoi(cur);
                                while (*cur && *cur != ',' && *cur != ']') cur++;
                            }
                        }
                    }
                    po = strstr(ul_ptr, "\"right_panel_order\"");
                    if (po && po < block_end)
                    {
                        char *arr = strchr(po, '[');
                        if (arr)
                        {
                            char *cur = arr + 1;
                            for (int i = 0; i < MAX_RIGHT_PANELS && cur && *cur != ']'; i++)
                            {
                                while (*cur && (*cur == ' ' || *cur == ',')) cur++;
                                if (*cur == ']' || *cur == '\0') break;
                                L->right_panel_order[i] = atoi(cur);
                                while (*cur && *cur != ',' && *cur != ']') cur++;
                            }
                        }
                    }

                    // panel open arrays
                    char *po2 = strstr(ul_ptr, "\"left_panel_open\"");
                    if (po2 && po2 < block_end)
                    {
                        char *arr = strchr(po2, '[');
                        if (arr)
                        {
                            char *cur = arr + 1;
                            for (int i = 0; i < MAX_LEFT_PANELS && cur && *cur != ']'; i++)
                            {
                                while (*cur && (*cur == ' ' || *cur == ',')) cur++;
                                if (*cur == ']' || *cur == '\0') break;
                                L->left_panel_open[i] = (strncmp(cur, "true", 4) == 0);
                                while (*cur && *cur != ',' && *cur != ']') cur++;
                            }
                        }
                    }
                    po2 = strstr(ul_ptr, "\"right_panel_open\"");
                    if (po2 && po2 < block_end)
                    {
                        char *arr = strchr(po2, '[');
                        if (arr)
                        {
                            char *cur = arr + 1;
                            for (int i = 0; i < MAX_RIGHT_PANELS && cur && *cur != ']'; i++)
                            {
                                while (*cur && (*cur == ' ' || *cur == ',')) cur++;
                                if (*cur == ']' || *cur == '\0') break;
                                L->right_panel_open[i] = (strncmp(cur, "true", 4) == 0);
                                while (*cur && *cur != ',' && *cur != ']') cur++;
                            }
                        }
                    }
                }
            }

            // load rotator settings
            {
                RotatorSettings *R = &config->rotator_settings;
                char *rot_ptr = strstr(text, "\"rotator_settings\"");
                if (rot_ptr)
                {
                    char *block_end = strchr(rot_ptr, '}');
                    if (!block_end) block_end = text + strlen(text);

                    char *s = strstr(rot_ptr, "\"host\"");
                    if (s && s < block_end) { char *c = strchr(s, ':'); if (c) { char *q = strchr(c, '"'); if (q && q < block_end) sscanf(q + 1, "%63[^\"]", R->host); } }
                    s = strstr(rot_ptr, "\"port\"");
                    if (s && s < block_end) { char *c = strchr(s, ':'); if (c) { char *q = strchr(c, '"'); if (q && q < block_end) sscanf(q + 1, "%15[^\"]", R->port); } }
                    s = strstr(rot_ptr, "\"get_fmt\"");
                    if (s && s < block_end) { char *c = strchr(s, ':'); if (c) { char *q = strchr(c, '"'); if (q && q < block_end) sscanf(q + 1, "%63[^\"]", R->get_fmt); } }
                    s = strstr(rot_ptr, "\"set_fmt\"");
                    if (s && s < block_end) { char *c = strchr(s, ':'); if (c) { char *q = strchr(c, '"'); if (q && q < block_end) sscanf(q + 1, "%63[^\"]", R->set_fmt); } }
                    s = strstr(rot_ptr, "\"custom_cmd\"");
                    if (s && s < block_end) { char *c = strchr(s, ':'); if (c) { char *q = strchr(c, '"'); if (q && q < block_end) sscanf(q + 1, "%127[^\"]", R->custom_cmd); } }
                    s = strstr(rot_ptr, "\"park_az\"");
                    if (s && s < block_end) { char *c = strchr(s, ':'); if (c) { char *q = strchr(c, '"'); if (q && q < block_end) sscanf(q + 1, "%15[^\"]", R->park_az); } }
                    s = strstr(rot_ptr, "\"park_el\"");
                    if (s && s < block_end) { char *c = strchr(s, ':'); if (c) { char *q = strchr(c, '"'); if (q && q < block_end) sscanf(q + 1, "%15[^\"]", R->park_el); } }
                    s = strstr(rot_ptr, "\"lead_time\"");
                    if (s && s < block_end) { char *c = strchr(s, ':'); if (c) { char *q = strchr(c, '"'); if (q && q < block_end) sscanf(q + 1, "%15[^\"]", R->lead_time); } }
                    R->auto_steer = ParseJsonBool(rot_ptr, "auto_steer", R->auto_steer);
                    R->steer_mode = 0;
                    s = strstr(rot_ptr, "\"steer_mode\"");
                    if (s && s < block_end) { char *c = strchr(s, ':'); if (c) sscanf(c + 1, "%d", &R->steer_mode); }
                }
            }

            // load active satellite selection (NORAD ids)
            config->active_sat_count = 0;
            {
                char *as_ptr = strstr(text, "\"active_sat_ids\"");
                if (as_ptr)
                {
                    config->has_saved_selection = true;
                    char *arr = strchr(as_ptr, '[');
                    if (arr)
                    {
                        char *cur = arr + 1;
                        while (cur && config->active_sat_count < MAX_SATELLITES)
                        {
                            while (*cur && (*cur == ' ' || *cur == ',' || *cur == '\n' || *cur == '\r' || *cur == '\t'))
                                cur++;
                            if (*cur == ']' || *cur == '\0')
                                break;
                            config->active_sat_ids[config->active_sat_count++] = (uint32_t)strtoul(cur, NULL, 10);
                            while (*cur && *cur != ',' && *cur != ']')
                                cur++;
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
    else {
        LOG_INFO("No config file found at %s -- showing first-run dialog", filename);
        sscanf("default","%63[^\"]",config->theme);
        config->window_width = 1920;
        config->window_height = 1080;
        config->target_fps = 120;
        config->ui_scale = 1.15;
        config->earth_rotation_offset = 0.00;
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
        config->hint_vsync = true;
        /* first run: a single default home location, no forced example marker */
        location_count = 0;
        int home_idx = AddLocation("Home", 0.0f, 0.0f, 0.0f);
        if (home_idx >= 0)
            SetHomeLocation(home_idx);

        config->show_first_run_dialog = true;

        SaveAppConfig(filename, config);
        

    }

    // load theme from the selected theme directory
    ThemeInitDefaults(&g_theme);
    if (!ThemeLoad(config->theme, &g_theme))
    {
        LOG_WARN("Failed to load theme '%s', using defaults", config->theme);
    }
}

void SaveAppConfig(const char *filename, AppConfig *config)
{
    FILE *file = fopen(filename, "w");
    if (!file)
    {
        LOG_ERROR("Failed to save config to %s", filename);
        return;
    }
    LOG_INFO("Saving config to %s", filename);

    fprintf(file, "{\n");
    fprintf(file, "    \"theme\": \"%s\",\n", config->theme);
    fprintf(file, "    \"window_width\": %d,\n", config->window_width);
    fprintf(file, "    \"window_height\": %d,\n", config->window_height);
    fprintf(file, "    \"target_fps\": %d,\n", config->target_fps);
    fprintf(file, "    \"ui_scale\": %.2f,\n", config->ui_scale);
    fprintf(file, "    \"earth_rotation_offset\": %.2f,\n", config->earth_rotation_offset);
    fprintf(file, "    \"orbits_to_draw\": %.2f,\n", config->orbits_to_draw);
    fprintf(file, "    \"show_clouds\": %s,\n", config->show_clouds ? "true" : "false");
    fprintf(file, "    \"show_night_lights\": %s,\n", config->show_night_lights ? "true" : "false");
    fprintf(file, "    \"show_markers\": %s,\n", config->show_markers ? "true" : "false");
    fprintf(file, "    \"show_statistics\": %s,\n", config->show_statistics ? "true" : "false");
    fprintf(file, "    \"highlight_sunlit\": %s,\n", config->highlight_sunlit ? "true" : "false");
    fprintf(file, "    \"show_slant_range\": %s,\n", config->show_slant_range ? "true" : "false");
    fprintf(file, "    \"show_scattering\": %s,\n", config->show_scattering ? "true" : "false");
    fprintf(file, "    \"show_skybox\": %s,\n", config->show_skybox ? "true" : "false");
    fprintf(file, "    \"show_ground_coverage\": %s,\n", config->show_ground_coverage ? "true" : "false");
    fprintf(file, "    \"show_apsides\": %s,\n", config->show_apsides ? "true" : "false");
    fprintf(file, "    \"hint_vsync\": %s,\n", config->hint_vsync ? "true" : "false");
    fprintf(file, "    \"show_first_run_dialog\": %s,\n", config->show_first_run_dialog ? "true" : "false");
    fprintf(file, "    \"use_local_time\": %s,\n", config->use_local_time ? "true" : "false");
    fprintf(file, "    \"data_stale_threshold_seconds\": %d,\n", config->data_stale_threshold_seconds);

    if (config->custom_data_source_count > 0)
    {
        fprintf(file, "    \"custom_data_sources\": [\n");
        for (int i = 0; i < config->custom_data_source_count; i++)
        {
            fprintf(file, "    {\"name\": \"%s\", \"url\": \"%s\", \"preferred_format\": \"%s\"}%s\n",
                    config->custom_data_sources[i].name,
                    config->custom_data_sources[i].url,
                    config->custom_data_sources[i].preferred_format == FORMAT_OMM_JSON ? "OMM_JSON" :
                    config->custom_data_sources[i].preferred_format == FORMAT_OMM_CSV ? "OMM_CSV" : "TLE",
                    (i == config->custom_data_source_count - 1) ? "" : ",");
        }
        fprintf(file, "    ],\n");
    }

    if (config->manual_entry_count > 0)
    {
        fprintf(file, "    \"manual_entries\": [\n");
        for (int i = 0; i < config->manual_entry_count; i++)
        {
            fprintf(file, "        \"%s\"%s\n", config->manual_entries[i], (i == config->manual_entry_count - 1) ? "" : ",");
        }
        fprintf(file, "    ],\n");
    }

    // save retlector groups (cached from API)
    if (config->retlector_group_count > 0)
    {
        fprintf(file, "    \"retlector_groups\": [\n");
        for (int i = 0; i < config->retlector_group_count; i++)
        {
            RetlectorGroup *g = &config->retlector_groups[i];
            fprintf(file, "    {\"name\": \"%s\", \"csv_endpoint\": \"%s\", \"selected\": %s}%s\n",
                    g->name, g->csv_endpoint,
                    g->selected ? "true" : "false",
                    (i == config->retlector_group_count - 1) ? "" : ",");
        }
        fprintf(file, "    ],\n");
    }

    // save custom entries (pasted orbital data)
    if (config->custom_entry_count > 0)
    {
        fprintf(file, "    \"custom_entries\": [\n");
        for (int i = 0; i < config->custom_entry_count; i++)
        {
            CustomEntry *e = &config->custom_entries[i];
            fprintf(file, "    {\"data\": \"%s\", \"detected_format\": %d, \"selected\": %s}%s\n",
                    e->data, (int)e->detected_format,
                    e->selected ? "true" : "false",
                    (i == config->custom_entry_count - 1) ? "" : ",");
        }
        fprintf(file, "    ],\n");
    }

    fprintf(file, "    \"locations\": [\n");
    for (int i = 0; i < location_count; i++)
    {
        fprintf(file, "    {\"name\": \"%s\", \"lat\": %.4f, \"lon\": %.4f, \"alt\": %.4f, \"is_home\": %s}%s\n",
                locations[i].name, locations[i].lat, locations[i].lon, locations[i].alt,
                locations[i].is_home ? "true" : "false",
                (i == location_count - 1) ? "" : ",");
    }
    fprintf(file, "    ],\n");

    /* -- UI layout (sidebar geometry + panel arrangement) ----------------- */
    {
        const UILayoutPersist *L = &config->ui_layout;
        fprintf(file, "    \"ui_layout\": {\n");
        fprintf(file, "        \"left_sidebar_width\": %.1f,\n", L->left_sidebar_width);
        fprintf(file, "        \"right_sidebar_width\": %.1f,\n", L->right_sidebar_width);
        fprintf(file, "        \"left_sidebar_visible\": %s,\n", L->left_sidebar_visible ? "true" : "false");
        fprintf(file, "        \"right_sidebar_visible\": %s,\n", L->right_sidebar_visible ? "true" : "false");
        fprintf(file, "        \"left_sidebar_hidden\": %s,\n", L->left_sidebar_hidden ? "true" : "false");
        fprintf(file, "        \"right_sidebar_hidden\": %s,\n", L->right_sidebar_hidden ? "true" : "false");

        fprintf(file, "        \"left_panel_order\": [");
        for (int i = 0; i < MAX_LEFT_PANELS; i++)
            fprintf(file, "%s%d", i ? "," : "", L->left_panel_order[i]);
        fprintf(file, "],\n");

        fprintf(file, "        \"right_panel_order\": [");
        for (int i = 0; i < MAX_RIGHT_PANELS; i++)
            fprintf(file, "%s%d", i ? "," : "", L->right_panel_order[i]);
        fprintf(file, "],\n");

        fprintf(file, "        \"left_panel_open\": [");
        for (int i = 0; i < MAX_LEFT_PANELS; i++)
            fprintf(file, "%s%s", i ? "," : "", L->left_panel_open[i] ? "true" : "false");
        fprintf(file, "],\n");

        fprintf(file, "        \"right_panel_open\": [");
        for (int i = 0; i < MAX_RIGHT_PANELS; i++)
            fprintf(file, "%s%s", i ? "," : "", L->right_panel_open[i] ? "true" : "false");
        fprintf(file, "]\n");

        fprintf(file, "    }\n");
    }

    /* -- rotator settings ------------------------------------------------ */
    {
        const RotatorSettings *R = &config->rotator_settings;
        fprintf(file, "    \"rotator_settings\": {\n");
        fprintf(file, "        \"host\": \"%s\",\n", R->host);
        fprintf(file, "        \"port\": \"%s\",\n", R->port);
        fprintf(file, "        \"get_fmt\": \"%s\",\n", R->get_fmt);
        fprintf(file, "        \"set_fmt\": \"%s\",\n", R->set_fmt);
        fprintf(file, "        \"custom_cmd\": \"%s\",\n", R->custom_cmd);
        fprintf(file, "        \"park_az\": \"%s\",\n", R->park_az);
        fprintf(file, "        \"park_el\": \"%s\",\n", R->park_el);
        fprintf(file, "        \"lead_time\": \"%s\",\n", R->lead_time);
        fprintf(file, "        \"auto_steer\": %s,\n", R->auto_steer ? "true" : "false");
        fprintf(file, "        \"steer_mode\": %d\n", R->steer_mode);
        fprintf(file, "    }\n");
    }

    /* -- active satellite selection (NORAD ids) --------------------------- */
    fprintf(file, "    \"active_sat_ids\": [");
    for (int i = 0; i < config->active_sat_count; i++)
        fprintf(file, "%s%u", i ? "," : "", config->active_sat_ids[i]);
    fprintf(file, "]\n");

    fprintf(file, "}\n");
    fclose(file);
}
