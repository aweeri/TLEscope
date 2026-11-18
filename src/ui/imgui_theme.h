#ifndef IMGUI_THEME_H
#define IMGUI_THEME_H

#include "core/theme.h"

/* apply theme colors/style to ImGui; ui_scale scales all sizes and the global font scale */
void ThemeApplyToImGui(const Theme *t, float ui_scale);

/* rebuild the ImGui font atlas; call after backend init and on theme/ui_scale change */
void ThemeRebuildImGuiFonts(const Theme *t, float ui_scale);

float ThemeDevicePixelScale(void);


// Single source of truth for UI scaling.

float UIScale(void);
float UIDeviceScale(void);
float UIPx(float logical);

#endif /* IMGUI_THEME_H */