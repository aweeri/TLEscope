#include "config.h"
#include "types.h"
#include "theme.h"
#include "util/log.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Marker home_location;

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
    config->show_first_run_dialog = false; //default
    config->hint_vsync = true;       // default
    config->custom_data_source_count = 0;
    config->retlector_group_count = 0;
    config->retlector_groups_fetched = false;
    config->custom_entry_count = 0;
    config->data_stale_threshold_seconds = STALE_THRESHOLD_DEFAULT;

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
            config->show_scattering = ParseJsonBool(text, "show_scattering", config->show_scattering);
            config->hint_vsync = ParseJsonBool(text, "hint_vsync", config->hint_vsync);
            config->show_first_run_dialog = ParseJsonBool(text, "show_first_run_dialog", config->show_first_run_dialog);

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

            // load home location
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
                    char *colon_name = strchr(name_ptr, ':');
                    if (colon_name)
                    {
                        char *quote_start = strchr(colon_name, '"');
                        if (quote_start)
                            sscanf(quote_start + 1, "%63[^\"]", home_location.name);
                    }
                    char *colon_lat = strchr(lat_ptr, ':');
                    if (colon_lat)
                        sscanf(colon_lat + 1, "%f", &home_location.lat);

                    char *colon_lon = strchr(lon_ptr, ':');
                    if (colon_lon)
                        sscanf(colon_lon + 1, "%f", &home_location.lon);

                    home_location.alt = 0.0f;
                    if (alt_ptr && alt_ptr < obj_end)
                    {
                        char *colon_alt = strchr(alt_ptr, ':');
                        if (colon_alt)
                            sscanf(colon_alt + 1, "%f", &home_location.alt);
                    }
                }
            }

            // load the map markers safely to avoid silent parsing failures
            marker_count = 0;
            char *m_ptr = strstr(text, "\"markers\"");
            if (m_ptr)
            {
                char *block_end = strchr(m_ptr, ']');
                if (!block_end)
                    block_end = text + strlen(text);

                while ((m_ptr = strstr(m_ptr, "{")) && m_ptr < block_end)
                {
                    if (marker_count >= MAX_MARKERS)
                        break;

                    char *obj_end = strchr(m_ptr, '}');
                    if (!obj_end || obj_end > block_end)
                        obj_end = block_end;

                    char *name_ptr = strstr(m_ptr, "\"name\"");
                    char *lat_ptr = strstr(m_ptr, "\"lat\"");
                    char *lon_ptr = strstr(m_ptr, "\"lon\"");
                    char *alt_ptr = strstr(m_ptr, "\"alt\"");

                    // alt_ptr is optional now
                    if (name_ptr && name_ptr < obj_end && lat_ptr && lat_ptr < obj_end && lon_ptr && lon_ptr < obj_end)
                    {

                        char *colon_name = strchr(name_ptr, ':');
                        if (colon_name && colon_name < obj_end)
                        {
                            char *quote_start = strchr(colon_name, '"');
                            if (quote_start && quote_start < obj_end)
                                sscanf(quote_start + 1, "%63[^\"]", markers[marker_count].name);
                        }

                        char *colon_lat = strchr(lat_ptr, ':');
                        if (colon_lat && colon_lat < obj_end)
                            sscanf(colon_lat + 1, "%f", &markers[marker_count].lat);

                        char *colon_lon = strchr(lon_ptr, ':');
                        if (colon_lon && colon_lon < obj_end)
                            sscanf(colon_lon + 1, "%f", &markers[marker_count].lon);

                        markers[marker_count].alt = 0.0f;
                        if (alt_ptr && alt_ptr < obj_end)
                        {
                            char *colon_alt = strchr(alt_ptr, ':');
                            if (colon_alt && colon_alt < obj_end)
                                sscanf(colon_alt + 1, "%f", &markers[marker_count].alt);
                        }

                        marker_count++;
                    }
                    m_ptr = obj_end + 1;
                }
            }
            UnloadFileText(text);
            LOG_INFO("Config loaded: theme=%s, %dx%d, %d markers, %d custom sources, %d retlector groups, %d custom entries, stale_threshold=%d",
                     config->theme, config->window_width, config->window_height,
                     marker_count, config->custom_data_source_count,
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
        config->hint_vsync = true;
        sscanf("Home", "%63[^\"]", home_location.name);
        home_location.lat = 0.00;
        home_location.lon = 0.00;

        marker_count = 1;
        sscanf("Cape Canaveral", "%63[^\"]", markers[0].name);
        markers[0].lat = 28.3922f;
        markers[0].lon = -80.6077f;
        markers[0].alt = 0.0f;

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
    fprintf(file, "    \"hint_vsync\": %s,\n", config->hint_vsync ? "true" : "false");
    fprintf(file, "    \"show_first_run_dialog\": %s,\n", config->show_first_run_dialog ? "true" : "false");
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

    fprintf(file, "    \"home_location\": {\"name\": \"%s\", \"lat\": %.4f, \"lon\": %.4f, \"alt\": %.4f},\n", home_location.name, home_location.lat, home_location.lon, home_location.alt);

    fprintf(file, "    \"markers\": [\n");
    for (int i = 0; i < marker_count; i++)
    {
        fprintf(file, "    {\"name\": \"%s\", \"lat\": %.4f, \"lon\": %.4f, \"alt\": %.4f}%s\n", markers[i].name, markers[i].lat, markers[i].lon, markers[i].alt, (i == marker_count - 1) ? "" : ",");
    }
    fprintf(file, "    ]\n");
    fprintf(file, "}\n");
    fclose(file);
}
