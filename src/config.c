#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

bool show_clouds = false; // Global state

// turn hex strings into real colors
Color ParseHexColor(const char* hexStr, Color fallback) {
    if (!hexStr || hexStr[0] != '#') return fallback;
    unsigned int r = 0, g = 0, b = 0, a = 255;
    int len = strlen(hexStr);
    if (len == 7) {
        sscanf(hexStr, "#%02x%02x%02x", &r, &g, &b);
    } else if (len >= 9) {
        sscanf(hexStr, "#%02x%02x%02x%02x", &r, &g, &b, &a);
    } else {
        return fallback;
    }
    return (Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
}

// read the json file and grab our settings
void LoadAppConfig(const char* filename, AppConfig* config) {
    if (!FileExists(filename)) return;
    char* text = LoadFileText(filename);
    if (!text) return; 

    char hex[32];
    char* ptr;

    #define PARSE_COLOR(key, field) \
        ptr = strstr(text, "\"" key "\""); \
        if (ptr) { ptr = strchr(ptr, ':'); if (ptr) { ptr = strchr(ptr, '\"'); if (ptr) { \
            sscanf(ptr + 1, "%31[^\"]", hex); config->field = ParseHexColor(hex, config->field); } } }

    #define PARSE_FLOAT(key, field) \
        ptr = strstr(text, "\"" key "\""); \
        if (ptr) { ptr = strchr(ptr, ':'); if (ptr) { sscanf(ptr + 1, "%f", &config->field); } }

    #define PARSE_INT(key, field) \
        ptr = strstr(text, "\"" key "\""); \
        if (ptr) { ptr = strchr(ptr, ':'); if (ptr) { sscanf(ptr + 1, "%d", &config->field); } }

    PARSE_INT("window_width", window_width);
    PARSE_INT("window_height", window_height);
    PARSE_INT("target_fps", target_fps);
    PARSE_FLOAT("ui_scale", ui_scale);
    PARSE_FLOAT("earth_rotation_offset", earth_rotation_offset);
    PARSE_FLOAT("orbits_to_draw", orbits_to_draw);

    // parse the global cloud toggle 
    char* sc_ptr = strstr(text, "\"show_clouds\"");
    if (sc_ptr) {
        char* comma = strchr(sc_ptr, ',');
        char* false_ptr = strstr(sc_ptr, "false");
        if (false_ptr && (!comma || false_ptr < comma)) {
            show_clouds = false;
        }
    }

    // grabbing all the colors 
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

    // load the map markers
    marker_count = 0;
    char* m_ptr = strstr(text, "\"markers\"");
    if (m_ptr) {
        char* block_end = strchr(m_ptr, ']');
        if (!block_end) block_end = text + strlen(text);
        
        while ((m_ptr = strstr(m_ptr, "{")) && m_ptr < block_end) {
            if (marker_count >= MAX_MARKERS) break;
            
            char* name_ptr = strstr(m_ptr, "\"name\"");
            char* lat_ptr = strstr(m_ptr, "\"lat\"");
            char* lon_ptr = strstr(m_ptr, "\"lon\"");
            char* obj_end = strchr(m_ptr, '}');
            if (!obj_end) obj_end = block_end;

            if (name_ptr && lat_ptr && lon_ptr && name_ptr < obj_end) {
                char* colon_name = strchr(name_ptr, ':');
                if (colon_name) {
                    char* quote_start = strchr(colon_name, '"');
                    if (quote_start) sscanf(quote_start + 1, "%63[^\"]", markers[marker_count].name);
                }
                char* colon_lat = strchr(lat_ptr, ':');
                if (colon_lat) sscanf(colon_lat + 1, "%f", &markers[marker_count].lat);
                
                char* colon_lon = strchr(lon_ptr, ':');
                if (colon_lon) sscanf(colon_lon + 1, "%f", &markers[marker_count].lon);
                
                marker_count++;
            }
            m_ptr = obj_end + 1;
        }
    }
    UnloadFileText(text);
}