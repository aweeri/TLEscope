/*
 * tools_settings.cpp - Generic persisted key-value settings store for tools.
 *
 * Tools read/write their own namespaced keys via this API. The whole map is
 * serialized generically by config.cpp, so adding a toggle never requires a
 * new AppConfig field or a config.cpp edit.
 */

#include "tools_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -- internal helpers ------------------------------------------------------ */

static ToolSetting *FindSetting(AppConfig *cfg, const char *key)
{
    for (int i = 0; i < cfg->tool_settings.count; i++)
    {
        if (strcmp(cfg->tool_settings.entries[i].key, key) == 0)
            return &cfg->tool_settings.entries[i];
    }
    return NULL;
}

static ToolSetting *FindOrCreateSetting(AppConfig *cfg, const char *key)
{
    ToolSetting *s = FindSetting(cfg, key);
    if (s)
        return s;

    if (cfg->tool_settings.count >= MAX_TOOL_SETTINGS)
        return NULL;

    s = &cfg->tool_settings.entries[cfg->tool_settings.count++];
    strncpy(s->key, key, sizeof(s->key) - 1);
    s->key[sizeof(s->key) - 1] = '\0';
    s->value[0] = '\0';
    return s;
}

/* -- public API ------------------------------------------------------------ */

bool ToolSettingGetBool(AppConfig *cfg, const char *key, bool def)
{
    ToolSetting *s = FindSetting(cfg, key);
    if (!s)
        return def;
    return (strcmp(s->value, "true") == 0);
}

void ToolSettingSetBool(AppConfig *cfg, const char *key, bool val)
{
    ToolSetting *s = FindOrCreateSetting(cfg, key);
    if (!s)
        return;
    strncpy(s->value, val ? "true" : "false", sizeof(s->value) - 1);
    s->value[sizeof(s->value) - 1] = '\0';
}

int ToolSettingGetInt(AppConfig *cfg, const char *key, int def)
{
    ToolSetting *s = FindSetting(cfg, key);
    if (!s)
        return def;
    return atoi(s->value);
}

void ToolSettingSetInt(AppConfig *cfg, const char *key, int val)
{
    ToolSetting *s = FindOrCreateSetting(cfg, key);
    if (!s)
        return;
    snprintf(s->value, sizeof(s->value), "%d", val);
}

float ToolSettingGetFloat(AppConfig *cfg, const char *key, float def)
{
    ToolSetting *s = FindSetting(cfg, key);
    if (!s)
        return def;
    return (float)atof(s->value);
}

void ToolSettingSetFloat(AppConfig *cfg, const char *key, float val)
{
    ToolSetting *s = FindOrCreateSetting(cfg, key);
    if (!s)
        return;
    snprintf(s->value, sizeof(s->value), "%g", (double)val);
}

const char *ToolSettingGetString(AppConfig *cfg, const char *key, const char *def)
{
    ToolSetting *s = FindSetting(cfg, key);
    if (!s)
        return def;
    return s->value;
}

void ToolSettingSetString(AppConfig *cfg, const char *key, const char *val)
{
    ToolSetting *s = FindOrCreateSetting(cfg, key);
    if (!s)
        return;
    strncpy(s->value, val, sizeof(s->value) - 1);
    s->value[sizeof(s->value) - 1] = '\0';
}