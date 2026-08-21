#ifndef TOOLS_SETTINGS_H
#define TOOLS_SETTINGS_H

#include "core/config.h"
#include "core/types.h"

/**
 * @file tools_settings.h
 * @brief Generic persisted key-value settings store for tools.
 *
 * Tools read/write their own namespaced keys (e.g. "cubeifier.enabled") via
 * this API. The whole map is serialized generically by config.cpp, so adding a
 * toggle never requires a new AppConfig field or a config.cpp edit.
 *
 * Keys should be namespaced by the tool, e.g. "mytool.some_setting".
 */

/** read a bool setting, returning def if the key is absent */
bool ToolSettingGetBool(AppConfig *cfg, const char *key, bool def);

/** write a bool setting */
void ToolSettingSetBool(AppConfig *cfg, const char *key, bool val);

/** read an int setting, returning def if the key is absent */
int ToolSettingGetInt(AppConfig *cfg, const char *key, int def);

/** write an int setting */
void ToolSettingSetInt(AppConfig *cfg, const char *key, int val);

/** read a float setting, returning def if the key is absent */
float ToolSettingGetFloat(AppConfig *cfg, const char *key, float def);

/** write a float setting */
void ToolSettingSetFloat(AppConfig *cfg, const char *key, float val);

/** read a string setting, returning def if the key is absent */
const char *ToolSettingGetString(AppConfig *cfg, const char *key, const char *def);

/** write a string setting */
void ToolSettingSetString(AppConfig *cfg, const char *key, const char *val);

#endif /* TOOLS_SETTINGS_H */