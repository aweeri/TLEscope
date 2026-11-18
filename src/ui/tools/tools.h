#ifndef TOOLS_H
#define TOOLS_H

#include "core/config.h"
#include "ui/ui.h"
#include "tools_scene.h"

void DrawPanelSatMgr(UIContext *ctx, AppConfig *cfg);
void DrawPanelDataSources(UIContext *ctx, AppConfig *cfg);
void DrawPanelLayers(UIContext *ctx, AppConfig *cfg);
void DrawPanelTimeCtrl(UIContext *ctx, AppConfig *cfg);
void DrawPanelSatInfo(UIContext *ctx, AppConfig *cfg);
void DrawPanelPasses(UIContext *ctx, AppConfig *cfg);
void DrawPanelPolarPlot(UIContext *ctx, AppConfig *cfg);
void DrawPanelTransit(UIContext *ctx, AppConfig *cfg);
void DrawTransitOverviewModal(UIContext *ctx, AppConfig *cfg);
void DrawTransitSimulateScanTab(UIContext *ctx, AppConfig *cfg);

void DrawTransitRotatorWindow(UIContext *ctx, AppConfig *cfg);

void TransitOpenDopplerTab(AppConfig *cfg);
void TransitOpenRotatorTab(AppConfig *cfg);

// async simulate-scan worker lifecycle (see tool_simulate_scan.cpp)
void SimScanInit(void);
void SimScanShutdown(void);
void SimScanCancel(void);
void DrawPanelTrxdb(UIContext *ctx, AppConfig *cfg);
void DrawPanelExample(UIContext *ctx, AppConfig *cfg); /* reference example tool */
void DrawPanelLog(UIContext *ctx, AppConfig *cfg);

/* scene hooks (draw into the 3D globe / 2D map, see tools_scene.h) */
void DrawSceneLayers(SceneContext *sctx, AppConfig *cfg);
void DrawScenePolarPlot(SceneContext *sctx, AppConfig *cfg);
void DrawSceneTransit(SceneContext *sctx, AppConfig *cfg);

/* screen-space ImGui overlay hook (runs after rlImGuiBegin, see ui.cpp) */
void DrawMapGridLabels(UIContext *ctx, AppConfig *cfg);

#endif /* TOOLS_H */
