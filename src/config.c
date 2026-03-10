#include "config.h"
#include "types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Marker home_location;

// turn hex strings into real colors
Color ParseHexColor(const char *hexStr, Color fallback)
{
    if (!hexStr || hexStr[0] != '#')
        return fallback;
    unsigned int r = 0, g = 0, b = 0, a = 255;
    int len = strlen(hexStr);
    if (len == 7)
    {
        sscanf(hexStr, "#%02x%02x%02x", &r, &g, &b);
    }
    else if (len >= 9)
    {
        sscanf(hexStr, "#%02x%02x%02x%02x", &r, &g, &b, &a);
    }
    else
    {
        return fallback;
    }
    return (Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
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
    config->show_first_run_dialog = false; //default
    config->hint_vsync = false;       // default
    config->custom_tle_source_count = 0;

    if (FileExists(filename))
    {
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

            // parse the global cloud toggle
            char *sc_ptr = strstr(text, "\"show_clouds\"");
            if (sc_ptr)
            {
                char *comma = strchr(sc_ptr, ',');
                char *false_ptr = strstr(sc_ptr, "false");
                if (false_ptr && (!comma || false_ptr < comma))
                {
                    config->show_clouds = false;
                }
                else
                {
                    config->show_clouds = true;
                }
            }

            // parse the global night lights toggle (opt-out)
            char *snl_ptr = strstr(text, "\"show_night_lights\"");
            if (snl_ptr)
            {
                char *comma = strchr(snl_ptr, ',');
                char *false_ptr = strstr(snl_ptr, "false");
                if (false_ptr && (!comma || false_ptr < comma))
                {
                    config->show_night_lights = false;
                }
                else
                {
                    config->show_night_lights = true;
                }
            }
            else
            {
                config->show_night_lights = true;
            }

            // parse the global markers toggle
            char *sm_ptr = strstr(text, "\"show_markers\"");
            if (sm_ptr)
            {
                char *comma = strchr(sm_ptr, ',');
                char *false_ptr = strstr(sm_ptr, "false");
                if (false_ptr && (!comma || false_ptr < comma))
                {
                    config->show_markers = false;
                }
            }

            // parse the statistics toggle
            char *stat_ptr = strstr(text, "\"show_statistics\"");
            if (stat_ptr)
            {
                char *comma = strchr(stat_ptr, ',');
                char *true_ptr = strstr(stat_ptr, "true");
                if (true_ptr && (!comma || true_ptr < comma))
                {
                    config->show_statistics = true;
                }
            }

            // parse sunlit toggle
            char *hsl_ptr = strstr(text, "\"highlight_sunlit\"");
            if (hsl_ptr)
            {
                char *comma = strchr(hsl_ptr, ',');
                char *true_ptr = strstr(hsl_ptr, "true");
                if (true_ptr && (!comma || true_ptr < comma))
                {
                    config->highlight_sunlit = true;
                }
            }

            // parse slant range toggle
            char *ssr_ptr = strstr(text, "\"show_slant_range\"");
            if (ssr_ptr)
            {
                char *comma = strchr(ssr_ptr, ',');
                char *true_ptr = strstr(ssr_ptr, "true");
                if (true_ptr && (!comma || true_ptr < comma))
                {
                    config->show_slant_range = true;
                }
            }

            // parse scattering toggle
            char *sscat_ptr = strstr(text, "\"show_scattering\"");
            if (sscat_ptr)
            {
                char *comma = strchr(sscat_ptr, ',');
                char *true_ptr = strstr(sscat_ptr, "true");
                if (true_ptr && (!comma || true_ptr < comma))
                {
                    config->show_scattering = true;
                }
            }

            // parse vsync toggle
            char *vsync_ptr = strstr(text, "\"hint_vsync\"");
            if (vsync_ptr)
            {
                char *comma = strchr(vsync_ptr, ',');
                char *true_ptr = strstr(vsync_ptr, "true");
                if (true_ptr && (!comma || true_ptr < comma))
                {
                    config->hint_vsync = true;
                }
            }

            // parse first run dialog toggle
            char *sfr_ptr = strstr(text, "\"show_first_run_dialog\"");
            if (sfr_ptr)
            {
                char *comma = strchr(sfr_ptr, ',');
                char *true_ptr = strstr(sfr_ptr, "true");
                if (true_ptr && (!comma || true_ptr < comma))
                {
                    config->show_first_run_dialog = true;
                }
            }

            // load manual TLEs
            char *mt_ptr = strstr(text, "\"manual_tles\"");
            if (mt_ptr)
            {
                char *array_start = strchr(mt_ptr, '[');
                char *array_end = array_start ? strchr(array_start, ']') : NULL;
                if (array_start && array_end)
                {
                    char *curr = array_start + 1;
                    while (curr < array_end && config->manual_tle_count < MAX_MANUAL_TLES)
                    {
                        char *quote_start = strchr(curr, '"');
                        if (!quote_start || quote_start > array_end) break;
                        char *quote_end = strchr(quote_start + 1, '"');
                        if (!quote_end || quote_end > array_end) break;

                        int len = quote_end - (quote_start + 1);
                        if (len >= 512) len = 511;
                        strncpy(config->manual_tles[config->manual_tle_count], quote_start + 1, len);
                        config->manual_tles[config->manual_tle_count][len] = '\0';
                        config->manual_tle_count++;

                        curr = quote_end + 1;
                    }
                }
            }

            // load custom TLE sources
            char *cts_ptr = strstr(text, "\"custom_tle_sources\"");
            if (cts_ptr)
            {
                char *block_end = strchr(cts_ptr, ']');
                if (!block_end)
                    block_end = text + strlen(text);

                while ((cts_ptr = strstr(cts_ptr, "{")) && cts_ptr < block_end)
                {
                    if (config->custom_tle_source_count >= MAX_CUSTOM_TLE_SOURCES)
                        break;

                    char *obj_end = strchr(cts_ptr, '}');
                    if (!obj_end || obj_end > block_end)
                        obj_end = block_end;

                    char *name_ptr = strstr(cts_ptr, "\"name\"");
                    char *url_ptr = strstr(cts_ptr, "\"url\"");

                    if (name_ptr && name_ptr < obj_end && url_ptr && url_ptr < obj_end)
                    {
                        char *colon_name = strchr(name_ptr, ':');
                        if (colon_name && colon_name < obj_end)
                        {
                            char *quote_start = strchr(colon_name, '"');
                            if (quote_start && quote_start < obj_end)
                                sscanf(quote_start + 1, "%63[^\"]", config->custom_tle_sources[config->custom_tle_source_count].name);
                        }

                        char *colon_url = strchr(url_ptr, ':');
                        if (colon_url && colon_url < obj_end)
                        {
                            char *quote_start = strchr(colon_url, '"');
                            if (quote_start && quote_start < obj_end)
                                sscanf(quote_start + 1, "%255[^\"]", config->custom_tle_sources[config->custom_tle_source_count].url);
                        }

                        config->custom_tle_sources[config->custom_tle_source_count].selected = false;
                        config->custom_tle_source_count++;
                    }
                    cts_ptr = obj_end + 1;
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
        }
    }
    else {
        printf("INFO: No config file found at %s! Showing first run dialog!\n", filename);
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
        config->hint_vsync = false;
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

    // load colors from the selected theme file
    char theme_path[256];
    snprintf(theme_path, sizeof(theme_path), "themes/%s/theme.json", config->theme);

    if (FileExists(theme_path))
    {
        char *theme_text = LoadFileText(theme_path);
        if (theme_text)
        {
            char hex[32];
            char *ptr;

#define PARSE_COLOR(key, field)                                                                                                                                                                        \
    ptr = strstr(theme_text, "\"" key "\"");                                                                                                                                                           \
    if (ptr)                                                                                                                                                                                           \
    {                                                                                                                                                                                                  \
        ptr = strchr(ptr, ':');                                                                                                                                                                        \
        if (ptr)                                                                                                                                                                                       \
        {                                                                                                                                                                                              \
            ptr = strchr(ptr, '\"');                                                                                                                                                                   \
            if (ptr)                                                                                                                                                                                   \
            {                                                                                                                                                                                          \
                sscanf(ptr + 1, "%31[^\"]", hex);                                                                                                                                                      \
                config->field = ParseHexColor(hex, config->field);                                                                                                                                     \
            }                                                                                                                                                                                          \
        }                                                                                                                                                                                              \
    }

            PARSE_COLOR("bg_color", bg_color);
            PARSE_COLOR("orbit_normal", orbit_normal);
            PARSE_COLOR("orbit_highlighted", orbit_highlighted);
            PARSE_COLOR("sat_normal", sat_normal);
            PARSE_COLOR("sat_highlighted", sat_highlighted);
            PARSE_COLOR("sat_selected", sat_selected);
            PARSE_COLOR("text_main", text_main);
            PARSE_COLOR("text_secondary", text_secondary);
            PARSE_COLOR("ui_bg", ui_bg);
            PARSE_COLOR("periapsis", periapsis);
            PARSE_COLOR("apoapsis", apoapsis);
            PARSE_COLOR("footprint_bg", footprint_bg);
            PARSE_COLOR("footprint_border", footprint_border);

            PARSE_COLOR("ui_primary", ui_primary);
            PARSE_COLOR("ui_secondary", ui_secondary);
            PARSE_COLOR("ui_accent", ui_accent);

            PARSE_COLOR("scope_bg", scope_bg);
            PARSE_COLOR("scope_horizon", scope_horizon);
            PARSE_COLOR("overlay_dim", overlay_dim);

            UnloadFileText(theme_text);
        }
    }
}

void SaveAppConfig(const char *filename, AppConfig *config)
{
    FILE *file = fopen(filename, "w");
    if (!file)
        return;

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
    fprintf(file, "    \"hint_vsync\": %s,\n", config->hint_vsync ? "true" : "false");
    fprintf(file, "    \"show_first_run_dialog\": %s,\n", config->show_first_run_dialog ? "true" : "false");

    if (config->custom_tle_source_count > 0)
    {
        fprintf(file, "    \"custom_tle_sources\": [\n");
        for (int i = 0; i < config->custom_tle_source_count; i++)
        {
            fprintf(file, "    {\"name\": \"%s\", \"url\": \"%s\"}%s\n", config->custom_tle_sources[i].name, config->custom_tle_sources[i].url, (i == config->custom_tle_source_count - 1) ? "" : ",");
        }
        fprintf(file, "    ],\n");
    }

    if (config->manual_tle_count > 0)
    {
        fprintf(file, "    \"manual_tles\": [\n");
        for (int i = 0; i < config->manual_tle_count; i++)
        {
            fprintf(file, "        \"%s\"%s\n", config->manual_tles[i], (i == config->manual_tle_count - 1) ? "" : ",");
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
