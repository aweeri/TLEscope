#ifndef TOOLS_PLOT_H
#define TOOLS_PLOT_H

#include "core/types.h"
#include "core/astro.h"
#include "imgui.h"

/**
 * @file tools_plot.h
 * @brief Shared, theme-aware drawing and sampling primitives for tool plots.
 *
 * These helpers are used by the Transit Tracking panel and its overview modal
 * so the polar canvas, the XY charts and the pass sampling all have a single
 * implementation. Every color comes from g_theme.ui.* and every size is scaled
 * through UIPx().
 */

// state is a RotatorQueueState int or a pseudo-state below
#define PLOT_ROT_STATE_NONE      (-1)
#define PLOT_ROT_STATE_COMM_LOST (-2)
#define PLOT_ROT_STATE_PARK      (-3)

// never NULL, unknown values fall back to "Unknown"
const char *PlotRotatorStateLabel(int state, bool connected, bool slewing);
ImU32 PlotRotatorStateColor(int state, bool connected);
// same colour as a raylib Color, for banner and scene sinks
Color PlotRotatorStateColorC(int state, bool connected);

void PlotFormatAzEl(char *out, size_t n, double az, double el);
void PlotFormatAzElDelta(char *out, size_t n, double d_az, double d_el);

void PlotDrawRotatorStatusLine(const char *prefix, int state, bool connected,
                               bool slewing);

/* -- Polar canvas ---------------------------------------------------------- */

/** draw the elevation-ring polar grid (rings at 0/30/60 deg + cardinals) */
void PlotDrawPolarGrid(ImDrawList *dl, ImVec2 center, float radius);

/** draw a pass sky track: az/el polyline (broken at the north wrap), AOS/LOS markers, max-el arrow, current-time marker. `arrow_scale` scales the max-el arrow size (1.0 = default) */
void PlotDrawPolarTrack(ImDrawList *dl, ImVec2 center, float radius,
                        const SatPass *pass, double now_epoch,
                        float arrow_scale = 1.0f);

/** draw the max-elevation direction arrow for a pass. `px` scales the arrow size; it points along the direction of travel (azimuth-wrap handled) */
void PlotDrawMaxElArrow(ImDrawList *dl, ImVec2 center, float radius,
                        const SatPass *pass, float px);

/** draw the live satellite sky position dot (filled above horizon, hollow below) */
void PlotDrawSkyDot(ImDrawList *dl, ImVec2 center, float radius,
                    double az_deg, double el_deg);

// draw last, nothing without a hardware position
void PlotDrawRotatorMarker(ImDrawList *dl, ImVec2 center, float radius);

/** project an az/el sky position to canvas pixels (shared by the polar track and hover) */
ImVec2 PlotPolarProject(ImVec2 center, float radius, double az_deg, double el_deg);

/* -- XY charts ------------------------------------------------------------- */

/** hover readout formatter: fill `buf` from the hovered x fraction [0..1] and the series values (y_b is 0 for single-series); return true to show the tooltip. `user` is the caller's opaque pointer */
typedef bool (*PlotHoverFmt)(void *user, float x_frac, float y_a, float y_b,
                             char *buf, int buf_n);

/** draw the shared hover readout box (same look as the XY chart tooltip), anchored near `anchor` and kept inside `bounds_max` */
void PlotDrawHoverLabel(ImDrawList *dl, ImVec2 anchor, ImVec2 bounds_max, const char *text);

/** single-series themed XY chart. `marker_frac` in [0..1] draws a current-position guide line + dot at that x fraction (negative draws nothing); `hover_fmt`/`hover_user` enable the hover guide line, dots and tooltip */
void PlotDrawXYChart(ImDrawList *dl, ImVec2 min, ImVec2 max,
                     const float *samples, int count,
                     float y_min, float y_max,
                     const char *title, const char *y_unit,
                     float marker_frac = -1.0f,
                     PlotHoverFmt hover_fmt = nullptr, void *hover_user = nullptr);

/** dual-series themed XY chart (two colors + legend). `marker_frac`/`hover_fmt`/`hover_user` behave as in the single-series overload; the formatter receives both series values */
void PlotDrawXYChart(ImDrawList *dl, ImVec2 min, ImVec2 max,
                     const float *s1, const float *s2, int count,
                     float y_min, float y_max,
                     const char *title, const char *y_unit,
                     const char *label1, const char *label2,
                     float marker_frac = -1.0f,
                     PlotHoverFmt hover_fmt = nullptr, void *hover_user = nullptr);

/* -- Sampling -------------------------------------------------------------- */

/** sample the Doppler offset (Hz) across [aos, los] into out[0..count-1] */
void PlotSampleDoppler(Satellite *sat, double aos, double los, double base_freq,
                       Location obs, float *out, int count);

/** resample a pass's az/el path into out_el/out_az[0..count-1] */
void PlotSampleElevAz(const SatPass *pass, float *out_el, float *out_az, int count);

#endif /* TOOLS_PLOT_H */
