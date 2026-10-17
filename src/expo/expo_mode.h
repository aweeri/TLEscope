#ifndef EXPO_MODE_H
#define EXPO_MODE_H
#include "core/types.h"
#include <raylib.h>

#define EXPO_KEY_ENABLED  "expo.enabled"
#define EXPO_KEY_SIZE     "expo.size"
#define EXPO_SIZE_DEFAULT 1.0f
#define EXPO_SIZE_MIN     0.25f
#define EXPO_SIZE_MAX     3.0f

bool  ExpoModeEnabled(AppConfig *cfg);
void  ExpoModeSetEnabled(AppConfig *cfg, bool on);
float ExpoModeGetSize(AppConfig *cfg);
void  ExpoModeSetSize(AppConfig *cfg, float size);
void  ExpoModeDrawOverlay(Texture2D logo, Font font, AppConfig *cfg, Rectangle viewport);
#endif
