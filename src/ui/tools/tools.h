#ifndef TOOLS_H
#define TOOLS_H

#include "core/config.h"
#include "ui/ui.h"

/**
 * @file tools.h
 * @brief Declarations for all built-in tool panel draw functions.
 *
 * Each tool lives in its own tool_*.cpp file. To add a new tool, create a
 * tool_my_tool.cpp with a DrawPanelMyTool() function, declare it here, add a
 * PanelId in tools_registry.h, and register it in tools_registry.cpp.
 */

void DrawPanelSatMgr(UIContext *ctx, AppConfig *cfg);
void DrawPanelDataSources(UIContext *ctx, AppConfig *cfg);
void DrawPanelLayers(UIContext *ctx, AppConfig *cfg);
void DrawPanelTimeCtrl(UIContext *ctx, AppConfig *cfg);
void DrawPanelScope(UIContext *ctx, AppConfig *cfg);
void DrawPanelRotator(UIContext *ctx, AppConfig *cfg);
void DrawPanelSatInfo(UIContext *ctx, AppConfig *cfg);
void DrawPanelPasses(UIContext *ctx, AppConfig *cfg);
void DrawPanelPolarPlot(UIContext *ctx, AppConfig *cfg);
void DrawPanelDoppler(UIContext *ctx, AppConfig *cfg);
void DrawPanelLog(UIContext *ctx, AppConfig *cfg);

#endif /* TOOLS_H */
