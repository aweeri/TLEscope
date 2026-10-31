#ifndef TOOLS_COMMON_H
#define TOOLS_COMMON_H

#include "core/config.h"
#include "core/types.h"
#include "ui/ui.h"
#include "imgui.h"

/**
 * @file tools_common.h
 * @brief Shared helpers and state used by multiple tool panels.
 */

/* -- Shared state ---------------------------------------------------------- */

extern bool log_auto_scroll;        /* log auto-scroll preference               */
extern bool log_show_timestamps;    /* show timestamps in log entries (default off) */

/* -- Layers panel persistence keys (see tool_layers.cpp) ------------------- */

/* universal Orbits layer (see tool_layers.cpp) */
#define LAYERS_KEY_ORBITS          "layers.orbits"            /* master: kill ALL orbit drawing, both views */
#define LAYERS_KEY_ORBITS_DIMMED   "layers.orbits_unselected" /* draw unselected (dimmed) orbit lines */

/* number of predicted (future) orbit steps drawn for each ground track in 2D
 * mode (float, in 0.25-orbit increments, default 2.0). */
#define LAYERS_KEY_FUTURE_ORBITS_STEPS "layers.orbits_steps"
#define LAYERS_FUTURE_ORBITS_STEPS_DEFAULT 2.0f
#define LAYERS_FUTURE_ORBITS_STEPS_MIN    0.25f
#define LAYERS_FUTURE_ORBITS_STEPS_MAX    5.0f

/* sunlit-highlight scope for orbit drawing: 0 = Sel (active satellite only), 1 = All (every active satellite), 2 = fav (favorites) */
#define LAYERS_KEY_ORBITS_SUNLIT_SCOPE      "layers.orbits_sunlit_scope"
#define LAYERS_ORBITS_SUNLIT_SELECTED       0
#define LAYERS_ORBITS_SUNLIT_ALL            1
#define LAYERS_ORBITS_SUNLIT_FAV            2

/* favourite (starred) satellite orbit coloring (checkbox 2, bool default false) */
#define LAYERS_KEY_FAV_ORBITS_3D "layers.favorite_orbits_3d"

/* Moon geocentric orbital path (bool, default true) */
#define LAYERS_KEY_MOON_ORBIT "layers.moon_orbit"

#define LAYERS_KEY_ORBIT_DIRECTION "layers.orbit_direction"
#define LAYERS_KEY_ORBIT_DIRECTION_ANIM "layers.orbit_direction_anim"

/* Ground Coverage scope: 0 = Sel (active satellite only), 1 = All (every active satellite), 2 = fav (favorites) */
#define LAYERS_KEY_GC_MODE       "layers.ground_coverage_mode"
#define LAYERS_GC_MODE_SELECTED  0
#define LAYERS_GC_MODE_ALL       1
#define LAYERS_GC_MODE_FAV       2

/* layer visibility preset selected in the Layers panel (int, -1 = Custom) */
#define LAYERS_KEY_PRESET "layers.preset"
#define LAYERS_PRESET_CUSTOM (-1)

/* -- Layer visibility presets (see tool_layers.cpp) ------------------------ */

/* named presets managed by the Layers panel "Preset:" combo, in combo order */
typedef enum
{
    LAYER_PRESET_VISUAL = 0,
    LAYER_PRESET_SIMPLIFIED,
    LAYER_PRESET_MINIMAL,
    LAYER_PRESET_COUNT
} LayerPresetId;

/**
 * Apply a named layer preset (a LayerPresetId) and persist the selection.
 *
 * @param is_ecliptic_frame  optional live ecliptic flag; the panel passes its
 *                           UI pointer, first-boot callers pass NULL.
 */
void ApplyLayerPreset(AppConfig *cfg, int preset, bool *is_ecliptic_frame = nullptr);

/* -- Shared UI helpers ----------------------------------------------------- */

/** convert a raylib Color to an ImGui ImVec4 */
ImVec4 ThemeColor(const Color &c);

void ToolsDrawBanner(const char *title, const char *detail, Color accent);

/* -- Data source selection (shopping-cart) persistence --------------------- */

void SaveDataSelections(void);
void LoadDataSelections(void);

/* accessors for the in-memory selection list (used by the Data Sources tool) */
int DataSelectionCount(void);
DataSourceSelection *DataSelectionAt(int idx);
bool DataSelectionAdd(SourceType type, const char *name, const char *identifier,
                      const char *paste_data, OrbitalDataFormat format);
void DataSelectionRemove(int idx);

/* -- Shared UI helpers ----------------------------------------------------- */

/** case-insensitive substring search; returns true if substr is found in str */
bool str_contains_ic(const char *str, const char *substr);

/** draw a two-column table row (label | value) */
void InfoRow(const char *label, const char *fmt, ...);

/** draw a clickable RA or Dec value that cycles format on click */
void DrawClickableRADec(const char *label, double deg_value, int *format_var, bool is_ra);

/* -- Orbit direction-of-motion styling ------------------------------------- */

/* tuning constants (see OrbitDirectionIntensity) */
#define ORBIT_DIR_MIN          0.18f /* base alpha multiplier for dimmed/unselected orbits */
#define ORBIT_DIR_MIN_SELECTED 0.45f /* base alpha multiplier for the selected orbit: keeps its
                                      * dim (ahead) portion clearly brighter than a dimmed orbit
                                      * while still leaving a visible trail gradient */
#define ORBIT_DIR_TRAIL_LEN    0.30  /* highlighted trail length behind the satellite, in orbits */
#define ORBIT_DIR_ANIM_TAU     0.10  /* animated pulse tail length, in orbits      */
#define ORBIT_DIR_ANIM_SPEED   0.30  /* animated pulse sweeps per second           */
#define ORBIT_DIR_WIDTH_MAX    2.5f  /* thickest line-width multiplier (2D only)   */

/**
 * Direction-of-motion intensity for one point on an orbit line.
 *
 * @param phase     signed fraction of an orbit from the satellite/moon's current
 *                  position: positive = ahead (future), negative = behind
 *                  (trail). Values outside [-0.5, 0.5] are wrapped.
 * @param animated  when true, a bright pulse sweeps the orbit in the direction
 *                  of motion (driven by anim_time) instead of the static
 *                  trail/future ramp.
 * @param anim_time seconds; drives the pulse position when animated.
 * @param floor     base alpha multiplier for the dim (ahead) portion; pass
 *                  ORBIT_DIR_MIN_SELECTED for the selected orbit so it stays
 *                  clearly brighter than dimmed unselected orbits.
 *
 * @return an alpha multiplier in [floor, 1] to scale a segment's color.
 */
float OrbitDirectionIntensity(double phase, bool animated, double anim_time,
                              float floor = ORBIT_DIR_MIN);

/**
 * Line-width multiplier paired with OrbitDirectionIntensity (used by the 2D
 * ground tracks, where per-segment thickness is cheap). Returns a value in
 * [1, ORBIT_DIR_WIDTH_MAX].
 */
float OrbitDirectionWidth(double phase, bool animated, double anim_time,
                          float floor = ORBIT_DIR_MIN);

#endif /* TOOLS_COMMON_H */
