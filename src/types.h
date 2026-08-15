#ifndef TYPES_H
#define TYPES_H

#include "../lib/csgp4.h"

// On Windows, ensure windows.h is included BEFORE raylib.h to prevent
// symbol conflicts between the Windows API and raylib.
//
// Conflicts handled:
//   - Rectangle  (raylib struct vs wingdi.h function)           → NOGDI
//   - CloseWindow(raylib void(void) vs winuser.h BOOL(HWND))    → rename macro
//   - ShowCursor (raylib void(void) vs winuser.h int(BOOL))     → rename macro
//   - LoadImage  (raylib function vs winuser.h macro→LoadImageA)→ undef macro
//   - DrawText   (raylib function vs winuser.h macro→DrawTextA) → undef macro
//   - DrawTextEx (raylib function vs winuser.h macro→DrawTextExA)→ undef macro
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOGDI               // Suppress wingdi.h Rectangle() function
        #define NOGDI
    #endif
    #ifndef NOMB                // Suppress MessageBox
        #define NOMB
    #endif
    #ifndef NOMSG               // Suppress message-related declarations
        #define NOMSG
    #endif
    #ifndef NOIME               // Suppress IME
        #define NOIME
    #endif
    #ifndef NOMCX               // Suppress modem-control extensions
        #define NOMCX
    #endif

    // Rename Windows API *functions* (not macros) that conflict with raylib's
    // declarations.  These must be defined BEFORE windows.h is first included
    // so the preprocessor rewrites the Windows declarations to unique names.
    #define CloseWindow Win32_CloseWindow
    #define ShowCursor  Win32_ShowCursor

    #include <windows.h>

    // Restore original names so raylib can declare its own versions.
    #undef CloseWindow
    #undef ShowCursor

    // Undefine Windows *macros* that would otherwise be expanded by the
    // preprocessor when raylib declares functions with the same names.
    #ifdef LoadImage
        #undef LoadImage
    #endif
    #ifdef DrawText
        #undef DrawText
    #endif
    #ifdef DrawTextEx
        #undef DrawTextEx
    #endif
#endif

#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>

// basic limits and math constants
#define MAX_SATELLITES 15000
#define MAX_MARKERS 100
#define EARTH_RADIUS_KM 6371.0f
#define MOON_RADIUS_KM 1737.4f
#define MU 398600.4418f
#define DRAW_SCALE 3000.0f

#define ORBIT_CACHE_SIZE 361
#define MAX_CUSTOM_DATA_SOURCES 20

// Supported orbital data formats
typedef enum {
    FORMAT_UNKNOWN = 0,
    FORMAT_TLE,         // Legacy TLE/3LE
    FORMAT_OMM_JSON,    // CCSDS OMM JSON
    FORMAT_OMM_CSV,     // CCSDS OMM CSV
    FORMAT_OMM_XML,     // CCSDS OMM XML
    FORMAT_OMM_KVN      // CCSDS OMM Key-Value Notation
} OrbitalDataFormat;

// Metadata about where/how orbital data was obtained
typedef struct {
    char source_name[64];
    OrbitalDataFormat format;
    time_t fetch_time;
    time_t epoch_time;
} OrbitalDataMeta;

// keeps track of satellite data
typedef struct
{
    char name[32];
    char norad_id[10];          // Up to 9 digits + null (supports 6-9 digit IDs)
    uint32_t norad_id_num;      // Numeric form for fast comparison
    char intl_designator[12];   // Expanded for full yyyy-nnn format
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
    int orbit_cache_resolution;  // How many points r valid
    Vector3 cached_orbit_base_pos;  // Position when cache was last calculated
    double cached_orbit_epoch;  // Epoch when cache was last calculated
    bool orbit_cached;
    bool is_active;

    OrbitalDataMeta data_meta;  // Provenance of this satellite's data
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
    OrbitalDataFormat preferred_format;
    bool selected;
} CustomDataSource;

extern Satellite satellites[MAX_SATELLITES];
extern int sat_count;

extern Marker home_location;
extern Marker markers[MAX_MARKERS];
extern int marker_count;

#define MAX_MANUAL_ENTRIES 20

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
    float orbit_cache_drift_threshold_km;  // Recalculate cache if satellite drifts more than this (default 50 km)
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

    CustomDataSource custom_data_sources[MAX_CUSTOM_DATA_SOURCES];
    int custom_data_source_count;

    char manual_entries[MAX_MANUAL_ENTRIES][512];
    int manual_entry_count;

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
