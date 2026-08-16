#ifndef IMGUI_THEME_H
#define IMGUI_THEME_H

#include "core/theme.h"

/**
 * @file imgui_theme.h
 * @brief Apply a Theme to the Dear ImGui context
 *
 * These functions translate the theme's UI colors, style variables and font
 * configuration onto the running ImGui context. They are safe to call after
 * the ImGui backend has been initialised (rlImGuiBeginInitImGui).
 */

/**
 * Apply the theme's colors and style variables to ImGui.
 * `ui_scale` scales all sizes (via ImGuiStyle::ScaleAllSizes) and sets the
 * global font scale.
 */
void ThemeApplyToImGui(const Theme *t, float ui_scale);

/**
 * Rebuild the ImGui font atlas from the theme's font file + FontAwesome icons.
 * Must be called after the backend is initialised and whenever the theme or
 * ui_scale changes. Unloads the previous font texture if present.
 */
void ThemeRebuildImGuiFonts(const Theme *t, float ui_scale);

#endif /* IMGUI_THEME_H */