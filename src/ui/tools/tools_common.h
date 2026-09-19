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

/* master switch for the 2D "Future Orbits" track (bool, default true) */
#define LAYERS_KEY_FUTURE_ORBITS "layers.future_orbits"

/* Future Orbits scope: focused track only, or a bounded multi-track set. */
#define LAYERS_KEY_FUTURE_ORBITS_MODE       "layers.future_orbits_mode"
#define LAYERS_FUTURE_ORBITS_FOCUSED        0
#define LAYERS_FUTURE_ORBITS_MULTI          1
#define LAYERS_FUTURE_ORBITS_MAX_TRACKS     8

/* universal master switch for the Earth surface texture (bool, default true).
 * When off, the Earth renders as a plain black body in both the 2D map and the
 * 3D globe (see main.cpp). */
#define LAYERS_KEY_EARTH_TEXTURE "layers.earth_texture"

/* Ground Coverage scope: 0 = Sel (active satellite only), 1 = All (every active satellite) */
#define LAYERS_KEY_GC_MODE       "layers.ground_coverage_mode"
#define LAYERS_GC_MODE_SELECTED  0
#define LAYERS_GC_MODE_ALL       1

/* -- Shared UI helpers ----------------------------------------------------- */

/** convert a raylib Color to an ImGui ImVec4 */
ImVec4 ThemeColor(const Color &c);

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

#endif /* TOOLS_COMMON_H */
