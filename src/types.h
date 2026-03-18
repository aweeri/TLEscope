#ifndef TYPES_H
#define TYPES_H

#include "../lib/csgp4.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

//THIS SHOULD BE CHANGED!!!
#define BUILD_FOR_DIST

// basic limits and math constants
#define MAX_SATELLITES 15000
#define MAX_MARKERS 100
#define EARTH_RADIUS_KM 6371.0f
#define MOON_RADIUS_KM 1737.4f
#define MU 398600.4418f
#define DRAW_SCALE 3000.0f

#define ORBIT_CACHE_SIZE 361
#define MAX_CUSTOM_TLE_SOURCES 20

// keeps track of satellite data
typedef struct
{
    char name[32];
    char norad_id[6];
    char intl_designator[8];
    double epoch_days;
    double epoch_unix;
    double inclination;
    double raan;
    double eccentricity;
    double arg_perigee;
    double mean_anomaly;
    double mean_motion;
    double semi_major_axis;
    Vector3 current_pos;

    struct elsetrec satrec;

    Vector3 orbit_cache[ORBIT_CACHE_SIZE];
    bool orbit_cached;
    bool is_active;
} Satellite;

typedef struct
{
    char name[64];
    float lat;
    float lon;
    float alt;
} Marker;

typedef struct
{
    char name[64];
    char url[256];
    bool selected;
} CustomTLESource;

extern Satellite satellites[MAX_SATELLITES];
extern int sat_count;

extern Marker home_location;
extern Marker markers[MAX_MARKERS];
extern int marker_count;

#define MAX_MANUAL_TLES 20

/* visual settings and colors */ 
typedef struct
{
    char theme[64];
    int window_width;
    int window_height;
    int target_fps;
    float ui_scale;
    float earth_rotation_offset;
    float orbits_to_draw;
    bool show_clouds;
    bool show_night_lights;
    bool show_markers;
    bool show_statistics;
    bool highlight_sunlit;
    bool show_slant_range;
    bool show_scattering;
    bool hint_vsync;
    bool show_skybox;
    bool show_first_run_dialog;
    bool reload_theme;

    CustomTLESource custom_tle_sources[MAX_CUSTOM_TLE_SOURCES];
    int custom_tle_source_count;

    char manual_tles[MAX_MANUAL_TLES][512];
    int manual_tle_count;

    Color bg_color;
    Color orbit_normal;
    Color orbit_highlighted;
    Color sat_normal;
    Color sat_highlighted;
    Color sat_selected;
    Color text_main;
    Color text_secondary;
    Color ui_bg;
    Color periapsis;
    Color apoapsis;
    Color footprint_bg;
    Color footprint_border;

    Color ui_primary;
    Color ui_secondary;
    Color ui_accent;
    Color window_border;
    Color window_border_focus;
    Color scope_bg;
    Color scope_horizon;
    Color overlay_dim;
} AppConfig;

#endif // TYPES_H
