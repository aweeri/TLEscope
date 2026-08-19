#ifndef TYPES_H
#define TYPES_H

#include "../../lib/csgp4.h"

/**
 * @file types.h
 * @brief Core type definitions and shared constants
 *
 * Central header that defines all data structures, enums, and constants
 * used across the application. Must be included before raylib.h on Windows
 * to avoid symbol conflicts.
 */

/*
 * On Windows, ensure windows.h is included BEFORE raylib.h to prevent
 * symbol conflicts between the Windows API and raylib.
 *
 * Conflicts handled:
 *   - Rectangle  (raylib struct vs wingdi.h function)           -> NOGDI
 *   - CloseWindow(raylib void(void) vs winuser.h BOOL(HWND))    -> rename macro
 *   - ShowCursor (raylib void(void) vs winuser.h int(BOOL))     -> rename macro
 *   - LoadImage  (raylib function vs winuser.h macro->LoadImageA)-> undef macro
 *   - DrawText   (raylib function vs winuser.h macro->DrawTextA) -> undef macro
 *   - DrawTextEx (raylib function vs winuser.h macro->DrawTextExA)-> undef macro
 */
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
#define MAX_RETLECTOR_GROUPS 64
#define MAX_CUSTOM_ENTRIES 20

/* sidebar layout limits */
#define MAX_LEFT_PANELS   6
#define MAX_RIGHT_PANELS  5
#define MAX_PANELS        11   /* PANEL_COUNT */

/** supported orbital data formats */
typedef enum {
    FORMAT_UNKNOWN = 0,
    FORMAT_TLE,         // Legacy TLE/3LE
    FORMAT_OMM_JSON,    // CCSDS OMM JSON
    FORMAT_OMM_CSV,     // CCSDS OMM CSV
    FORMAT_OMM_XML,     // CCSDS OMM XML
    FORMAT_OMM_KVN      // CCSDS OMM Key-Value Notation
} OrbitalDataFormat;

/** metadata about where/how orbital data was obtained */
typedef struct {
    char source_name[64];
    OrbitalDataFormat format;
    time_t fetch_time;
    time_t epoch_time;
} OrbitalDataMeta;

/** keeps track of satellite data */
typedef struct
{
    char name[32];
    char norad_id[10];          // up to 9 digits + null (supports 6-9 digit IDs)
    uint32_t norad_id_num;      // numeric form for fast comparison
    char intl_designator[12];   // expanded for full yyyy-nnn format
    double epoch_days;
    double epoch_unix;
    double inclination;
    double raan;
    double eccentricity;
    double arg_perigee;
    double mean_anomaly;
    double mean_motion;
    double semi_major_axis;
    double bstar;               // B* drag term (decimal, not TLE-encoded)
    Vector3 current_pos;

    struct elsetrec satrec;

    Vector3 orbit_cache[ORBIT_CACHE_SIZE];
    int orbit_cache_resolution;  // how many points are valid
    Vector3 cached_orbit_base_pos;  // position when cache was last calculated
    double cached_orbit_epoch;  // epoch when cache was last calculated
    bool orbit_cached;
    bool is_active;

    OrbitalDataMeta data_meta;  // provenance of this satellite's data
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

/** a group/source available from the retlector.eu API */
typedef struct {
    char name[64];
    char csv_endpoint[256];   // full URL to CSV endpoint
    char status[16];          // "fresh", "stale", etc.
    char status_label[32];    // "23m ago", etc.
    char last_updated[32];    // ISO timestamp
    int age_seconds;
    int cache_duration_seconds;
    bool selected;
} RetlectorGroup;

/** a custom pasted orbital data entry with auto-detected format */
typedef struct {
    char data[4096];
    OrbitalDataFormat detected_format;
    bool selected;
} CustomEntry;

// -- Data Source Selection (shopping-cart model) -----------------------------

typedef enum {
    SOURCE_RETLECTOR,
    SOURCE_CELESTRAK,
    SOURCE_CUSTOM_URL,
    SOURCE_CUSTOM_PASTE
} SourceType;

typedef struct {
    SourceType type;
    char name[64];         // display name (group name, URL, or paste preview)
    char identifier[64];   // group name for retlector/celestrak, URL for custom
    char paste_data[4096]; // raw pasted data (only for SOURCE_CUSTOM_PASTE)
    OrbitalDataFormat format; // detected format (for paste entries)
} DataSourceSelection;

#define MAX_DATA_SOURCE_SELECTIONS 64

/**
 * @brief persisted UI layout state (sidebar geometry, panel order, open state)
 *
 * Mirrors the runtime UILayoutState from ui_layout.h so layout can be
 * serialised to settings.json without the UI layer being required at
 * config-parse time.
 */
typedef struct {
    /* sidebar geometry */
    float left_sidebar_width;
    float right_sidebar_width;
    bool left_sidebar_visible;
    bool right_sidebar_visible;
    bool left_sidebar_hidden;   /* snap-hidden: pull-tab shown instead */
    bool right_sidebar_hidden;

    /* panel order (PanelId values in display order, -1 = unused slot) */
    int left_panel_order[MAX_LEFT_PANELS];
    int right_panel_order[MAX_RIGHT_PANELS];

    /* open/closed state, parallel to the order arrays above */
    bool left_panel_open[MAX_LEFT_PANELS];
    bool right_panel_open[MAX_RIGHT_PANELS];
} UILayoutPersist;

extern Satellite satellites[MAX_SATELLITES];
extern int sat_count;

extern Marker home_location;
extern Marker markers[MAX_MARKERS];
extern int marker_count;

#define MAX_MANUAL_ENTRIES 20

// data staleness threshold presets (in seconds)
#define STALE_THRESHOLD_6H      21600
#define STALE_THRESHOLD_12H     43200
#define STALE_THRESHOLD_1D      86400
#define STALE_THRESHOLD_2D      172800
#define STALE_THRESHOLD_3D      259200
#define STALE_THRESHOLD_5D      432000
#define STALE_THRESHOLD_7D      604800
#define STALE_THRESHOLD_DEFAULT 172800  // 2 days

/* application settings (theme/appearance is managed by Theme in theme.h) */
typedef struct
{
    char theme[64];
    int window_width;
    int window_height;
    int target_fps;
    float ui_scale;
    float earth_rotation_offset;
    float orbits_to_draw;
    float orbit_cache_drift_threshold_km;  // recalculate cache if satellite drifts more than this (default 50 km)
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
    bool use_local_time;   // display dates/times in the system local timezone (default true)

    CustomDataSource custom_data_sources[MAX_CUSTOM_DATA_SOURCES];
    int custom_data_source_count;

    char manual_entries[MAX_MANUAL_ENTRIES][512];
    int manual_entry_count;

    RetlectorGroup retlector_groups[MAX_RETLECTOR_GROUPS];
    int retlector_group_count;
    bool retlector_groups_fetched;

    CustomEntry custom_entries[MAX_CUSTOM_ENTRIES];
    int custom_entry_count;

    int data_stale_threshold_seconds;  // default: STALE_THRESHOLD_DEFAULT (2 days)

    UILayoutPersist ui_layout;  // sidebar/panel layout persistence
} AppConfig;

#endif // TYPES_H
