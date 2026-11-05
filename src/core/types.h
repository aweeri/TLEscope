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

// include windows.h before raylib.h to avoid symbol conflicts (Rectangle, CloseWindow, ShowCursor, LoadImage, DrawText, DrawTextEx)
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

// compile-time fixed-array capacity; runtime limit is GetMaxSatellites()
#define MAX_SATELLITES_HARD_CAP 100000
#define DEFAULT_MAX_SATELLITES  50000
#define MIN_MAX_SATELLITES      1000
#define MAX_MAX_SATELLITES      MAX_SATELLITES_HARD_CAP

#define MAX_LOCATIONS 256
#define EARTH_RADIUS_KM 6371.0f
#define MOON_RADIUS_KM 1737.4f
#define MU 398600.4418f
#define DRAW_SCALE 3000.0f

/* symmetric timewarp magnitude limit: 524288 == 2^19 */
#define MAX_TIME_MULTIPLIER 524288.0

// max wall-clock drift (s) for the sim to count as "live"; shared by the main-loop time lock and the live badge
#define LIVE_WINDOW_SEC 5.0

#define ORBIT_CACHE_SIZE 361
#define MAX_CUSTOM_DATA_SOURCES 20
#define MAX_RETLECTOR_GROUPS 64
#define MAX_CUSTOM_ENTRIES 20

/* sidebar layout limits */
#define MAX_LEFT_PANELS   6
#define MAX_RIGHT_PANELS  5

/* -- Panel identity -------------------------------------------------------- */

// numeric identity of every tool panel; PANEL_COUNT sizes the layout arrays
typedef enum
{
    PANEL_SAT_MGR = 0,      /* Satellite Manager            -> left  */
    PANEL_DATA_SOURCES,     /* Data Sources                 -> left  */
    PANEL_LAYERS,           /* Layer Controls               -> left  */
    PANEL_ROTATOR,          // retired: controls live in the Transit Rotator tab
    PANEL_SAT_INFO,         /* Satellite Info (inspector)   -> right */
    PANEL_TRANSIT,          /* Transit Tracking             -> right */
    PANEL_POLAR_PLOT,       /* Polar Plot                   -> right */
    PANEL_LOG,              /* Log                          -> right */
    PANEL_TRXDB,            /* TRXDB NORAD TEST             -> right */
    PANEL_EXAMPLE,          /* Example Tool                 -> right */
    PANEL_COUNT
} PanelId;

/* derived from the PanelId enum so adding a panel never requires a bump */
#define MAX_PANELS PANEL_COUNT

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

// cheap per-frame propagation state; angles in rad, rates per minute (SGP4 native)
typedef struct {
    // epoch mean elements (SGP4 native: rad, rad/min)
    double mo, argpo, nodeo, inclo, ecco, no_unkozai;

    // secular rates (rad/min)
    double mdot, argpdot, nodedot, nodecf;

    // drag coefficients (SGP4 native)
    double bstar, cc1, cc4, cc5, t2cof, omgcof, xmcof, eta, delmo, sinmao;
    double d2, d3, d4, t3cof, t4cof, t5cof;
    int    isimp;

    // short-period constants
    double radiusearthkm, xke, j2, j3oj2, con41, x1mth2, x7thm1, aycof, xlcof;

    bool   is_deep_space;  // satrec->method == 'd' -> use full SGP4 per frame
    bool   valid;
    bool   prop_failed;    // init reported an error
} SatPropState;

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
    SatPropState prop;          // cheap per-frame propagation state

    Vector3 orbit_cache[ORBIT_CACHE_SIZE];
    int orbit_cache_resolution;  // how many points are valid
    float orbit_cache_phase[ORBIT_CACHE_SIZE];  // per-sample orbit phase (fraction of a period from the cache start)
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
    bool is_home;   // exactly one location is home at a time
} Location;

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
    SOURCE_CUSTOM_FILE,
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

// persisted UI layout state; mirrors UILayoutState so it can be saved without the UI layer
typedef struct {
    /* sidebar geometry */
    float left_sidebar_width;
    float right_sidebar_width;
    bool left_sidebar_visible;
    bool right_sidebar_visible;
    bool left_sidebar_hidden;   /* snap-hidden: pull-tab shown instead */
    bool right_sidebar_hidden;
    float left_restore_width;   /* width to restore after un-hiding */
    float right_restore_width;

    /* panel order (PanelId values in display order, -1 = unused slot) */
    int left_panel_order[MAX_PANELS];
    int right_panel_order[MAX_PANELS];

    /* open/closed state, parallel to the order arrays above */
    bool left_panel_open[MAX_PANELS];
    bool right_panel_open[MAX_PANELS];

    /* panel enabled (completely shown/hidden in sidebar) indexed by PanelId */
    bool panel_enabled[MAX_PANELS];
} UILayoutPersist;

extern Satellite satellites[MAX_SATELLITES_HARD_CAP];
extern int sat_count;

extern Location locations[MAX_LOCATIONS];
extern int location_count;

#define MAX_MANUAL_ENTRIES 20

// data staleness threshold presets (in seconds)
#define STALE_THRESHOLD_NEVER   0       // disable all staleness checks/prompts
#define STALE_THRESHOLD_6H      21600
#define STALE_THRESHOLD_12H     43200
#define STALE_THRESHOLD_1D      86400
#define STALE_THRESHOLD_2D      172800
#define STALE_THRESHOLD_3D      259200
#define STALE_THRESHOLD_5D      432000
#define STALE_THRESHOLD_7D      604800
#define STALE_THRESHOLD_DEFAULT 172800  // 2 days

/* persisted rotator connection/settings (see section 6 / 11) */
typedef struct
{
    // driver key: rotctld (default), gs232, easycomm, custom
    char protocol[16];
    char host[64];
    char port[16];
    char get_fmt[64];
    char set_fmt[64];
    char custom_cmd[128];
    char park_az[16];
    char park_el[16];
    char lead_time[16];
    char los_hold_s[16];      // post-LOS hold seconds; 0 = off
    bool auto_steer;
    int steer_mode;

    char min_el_track[16];
    char max_el_track[16];
    char az_min[16];
    char az_max[16];
    char rate_limit_deg_s[16]; // inert, config round-trip only
    char slew_tol_deg[16];
    char slew_timeout_s[16];
    char comm_timeout_s[16];
    char send_interval_ms[16]; // transport cadence, never a target limit
    int az_wrap_mode;          // 0=shortest, 1=CW, 2=CCW
    bool park_on_exit;
    bool park_between_passes;
    bool queue_tracking;
} RotatorSettings;

/* -- Tool-owned settings (generic key-value store) -------------------------- */

#define MAX_TOOL_SETTINGS 64

/** a single persisted tool setting (key/value string pair) */
typedef struct
{
    char key[64];
    char value[256];
} ToolSetting;

// generic persisted key-value map owned by tools (see tools_settings.h)
typedef struct
{
    ToolSetting entries[MAX_TOOL_SETTINGS];
    int count;
} ToolSettings;

/* application settings (theme/appearance is managed by Theme in theme.h) */
typedef struct
{
    char theme[64];
    int window_width;
    int window_height;
    int window_x;          // restored (un-maximized) window X position, monitor coords
    int window_y;          // restored (un-maximized) window Y position, monitor coords
    bool window_maximized; // last session maximized state (default true: open maximized)
    bool has_window_placement; // true once a valid restored rect has been stored
    bool fullscreen;       // start in fullscreen mode (default false)
    int target_fps;
    float ui_scale;
    float earth_rotation_offset;
    float orbits_to_draw;
    float orbit_cache_drift_threshold_km;  // recalculate if satellite drifts more than this (default 50 km)
    bool show_clouds;
    bool show_night_lights;
    bool show_markers;
    bool show_statistics;
    bool limit_map_zoomout;  // cap 2D map zoom-out at the viewport fill level (default true)
    bool highlight_sunlit;
    bool show_slant_range;
    bool show_scattering;
    bool hint_vsync;
    bool show_skybox;
    bool show_sun;
    bool show_sun_flare;
    bool show_ecliptic_frame;
    bool show_ground_coverage;
    bool show_apsides;
    bool show_earth_texture;
    bool show_latlon_grid;
    bool show_country_borders;
    bool show_coast_lines;
    bool show_tutorial;   /* first-boot tutorial pending (default false) */
    bool reload_theme;
    bool use_local_time;   // display dates/times in the system local timezone (default true)
    bool night_mode;       // full-screen monochrome-red post-process for dark adaptation
    int first_day_of_week; // first day of the week in date pickers: 0 = Sunday, 1 = Monday (default)

    bool prop_use_short_period;   // include J2 short-period terms in the cheap path (default true)

    RotatorSettings rotator_settings;  // persisted rotator connection config

    /* persisted active satellite selection (NORAD ids) */
    uint32_t active_sat_ids[MAX_SATELLITES_HARD_CAP];
    int active_sat_count;
    bool has_saved_selection;  // true once a selection has been persisted

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
    bool auto_repull_stale;            // auto re-pull stale data instead of prompting (default false)
    int network_timeout_seconds;       // HTTP request timeout, 15-300 seconds (default 45)
    int max_satellites;                // runtime limit, MIN_MAX_SATELLITES..MAX_MAX_SATELLITES

    UILayoutPersist ui_layout;  // sidebar/panel layout persistence

    ToolSettings tool_settings;  // generic persisted key-value store for tools
} AppConfig;

#endif // TYPES_H
