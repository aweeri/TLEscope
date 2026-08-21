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
