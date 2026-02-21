#ifndef TYPES_H
#define TYPES_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <raylib.h>
#include "csgp4.h"

// basic limits and math constants
#define MAX_SATELLITES 15000
#define MAX_MARKERS 100
#define EARTH_RADIUS_KM 6371.0f 
#define MOON_RADIUS_KM 1737.4f 
#define MU 398600.4418f  
#define DRAW_SCALE 3000.0f 

#define ORBIT_CACHE_SIZE 91

// keeps track of satellite data
typedef struct {
    char name[32];
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
} Satellite;

typedef struct {
    char name[64];
    float lat;
    float lon;
} Marker;

extern Satellite satellites[MAX_SATELLITES];
extern int sat_count;

extern Marker markers[MAX_MARKERS];
extern int marker_count;

// visual settings and colors
typedef struct {
    char theme[64];
    int window_width;
    int window_height;
    int target_fps;
    float ui_scale;
    float earth_rotation_offset;
    float orbits_to_draw;
    bool show_clouds;
    bool show_night_lights;
    
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
} AppConfig;

#endif // TYPES_H