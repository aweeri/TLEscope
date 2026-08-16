#ifndef PANELS_H
#define PANELS_H

#include "core/config.h"
#include "core/types.h"
#include "ui.h"

/**
 * @file panels.h
 * @brief Sidebar panel body renderers (migrated from the old floating dialogs)
 *
 * Each function renders the *body* of an accordion panel only — the accordion
 * chrome (header, collapse toggle, drag handle) is owned by ui_layout.cpp.
 */

extern bool celestrak_sel[25];      /* celestrak selection state (data sources) */
extern bool log_auto_scroll;        /* log auto-scroll preference               */

/* panel body renderers (one per PanelId, ordered to match the registry) */
void DrawPanelSatMgr(UIContext *ctx, AppConfig *cfg);
void DrawPanelDataSources(UIContext *ctx, AppConfig *cfg);
void DrawPanelTimeCtrl(UIContext *ctx, AppConfig *cfg);
void DrawPanelScope(UIContext *ctx, AppConfig *cfg);
void DrawPanelRotator(UIContext *ctx, AppConfig *cfg);
void DrawPanelSatInfo(UIContext *ctx, AppConfig *cfg);
void DrawPanelPasses(UIContext *ctx, AppConfig *cfg);
void DrawPanelPolarPlot(UIContext *ctx, AppConfig *cfg);
void DrawPanelDoppler(UIContext *ctx, AppConfig *cfg);
void DrawPanelLog(UIContext *ctx, AppConfig *cfg);

#endif /* PANELS_H */
