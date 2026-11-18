#ifndef CONFIG_H
#define CONFIG_H

#define CONFIG_SCHEMA_VERSION 1

#include "types.h"

/** load settings from a JSON file into the config struct */
void LoadAppConfig(const char *filename, AppConfig *config);

/** save the current config to a JSON file */
void SaveAppConfig(const char *filename, AppConfig *config);

int GetLegacyReferenceBodyDefault(void);

/** true when the last LoadAppConfig reset an incompatible config to defaults */
bool AppConfigWasReset(void);

// anchor filename to the exe dir, absolute paths pass through
// returns a rotating static buffer, copy it to keep it
const char *AppConfigPath(const char *name);

#endif // CONFIG_H
