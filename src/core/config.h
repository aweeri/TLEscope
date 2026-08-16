#ifndef CONFIG_H
#define CONFIG_H

#include "types.h"

/**
 * @file config.h
 * @brief Application configuration load/save
 */

/** load settings from a JSON file into the config struct */
void LoadAppConfig(const char *filename, AppConfig *config);

/** save the current config to a JSON file */
void SaveAppConfig(const char *filename, AppConfig *config);

#endif // CONFIG_H
